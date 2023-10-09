// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tool for updating expectations file using ResultDB results.
// This script uses a tast package, so it needs to run with
// ~/trunk/src/platform/tast/tools/go.sh.
//
// Prerequisites:
// 1. dev-go/gcp-bigquery package. Use |sudo emerge dev-go/gcp-bigquery| in the
//    ChromiumOS SDK.
// 2. Gcloud application default credentials. On your gLinux host, run the following
//    command and follow the instructions: |gcloud auth application-default login|.
//    Copy ~/.config/gcloud/application_default_credentials.json into the chroot.
//
// Usage:
// $  ~/trunk/src/platform/tast/tools/go.sh run fetch_results_from_lab.go -h

package main

import (
	"context"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"regexp"
	"strings"
	"sync"
	"time"

	"cloud.google.com/go/bigquery"
	"google.golang.org/api/iterator"

	"go.chromium.org/tast/core/errors"
)

// Command line options are stored in the following file scoped variables

var appendOutput bool

// Query options
var boardRegex string
var excludeBoardRegex string
var buildRegex string
var reasonRegex string
var excludeReasonRegex string

const excludeReasonRegexDefault = "deadline exceeded|exit status 127|[Ll]ost SSH connection|GPU hang"

var testRegex string
var fromDate string
var toDate string

// These regular expressions can be used for parameters validation
const buildValidationRegex = `^R[1-9][0-9]*-[1-9][0-9]*\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$`
const testValidationRegex = `^tast\.[a-z]+\.[A-Z][a-zA-Z]*(\.[_a-zA-Z0-9]+)?$`

// Uses the internal Google ResultDB database for querying test results.
const testResultsDatabase = "cros-test-analytics.resultdb.cros_test_results"
const stainlessProjectID = "google.com:stainless-prod"

var skipExpectations = make(map[string][]string)
var skipExpectationsMutex = sync.RWMutex{}

func init() {
	flag.StringVar(&boardRegex, "board", "", "regular expression for boards to match")
	flag.StringVar(&excludeBoardRegex, "exclude_board", "", "regular expression for boards to exclude")
	flag.StringVar(&buildRegex, "build", "", "regular expression for builds to match")
	flag.StringVar(&testRegex, "test", "borealis.Piglit.*", "regular expression for tests to match")
	flag.StringVar(&reasonRegex, "reason", `Failed\W(\d+\W)?test`, "regular expression for failure reasons to match")
	flag.StringVar(&excludeReasonRegex, "exclude_reason", `\[Fixture failure\]`, "regular expression for failure reasons to exclude")
	flag.StringVar(&fromDate, "from_date", "", "the start of the date range. Format: YYYY-MM-DD")
	flag.StringVar(&toDate, "to_date", "", "the end of the date range. Format: YYYY-MM-DD")
	flag.BoolVar(&appendOutput, "append", false, "If set, append new failures to corresponding -borealis-skips.txt")
}

// escapeBigqueryStrings was ported from the stainless frontend code. It
// replaces any character that is not ASCII alphanumeric, space, underscore, or
// hyphen with hex coded (ordinal) representation.
func escapeBigqueryStrings(str string) string {
	specialCharactersRegexp := regexp.MustCompile(`[^A-Za-z0-9 _-]`)
	var output string
	for i := 0; i < len(str); i++ {
		if specialCharactersRegexp.Match([]byte{str[i]}) {
			output = output + fmt.Sprintf("\\x%x", int(str[i]))
		} else {
			output = output + string(str[i])
		}
	}
	return output
}

func addQueryCriteria(query, dimension, regex string) string {
	return query + fmt.Sprintf("\n  AND REGEXP_CONTAINS(IFNULL(%s, \"-\"), \"%s\")", dimension, escapeBigqueryStrings(regex))
}

func addExcludeCriteria(query, dimension, regex string) string {
	return query + fmt.Sprintf("\n  AND NOT REGEXP_CONTAINS(IFNULL(%s, \"-\"), \"%s\")", dimension, escapeBigqueryStrings(regex))
}

// createTestResultsQueryString creates a query for the stainless database
// using the program's command line arguments.
func createTestResultsQueryString() (string, error) {
	queryString := fmt.Sprintf(
		`SELECT
			IFNULL(board, "-") AS board,
			IFNULL(test, "-") AS test,
			status,
			failure_reason AS reason,
			logs_url,
		FROM
		    %s
		WHERE
			queued_time BETWEEN "%s 00:00:00 UTC" AND "%s 00:00:00 UTC"
			AND status = "FAIL"
			AND (suite IS NULL OR NOT REGEXP_CONTAINS(suite, r"^(au$|paygen_au)"))
			AND job_name NOT LIKE "git_%%"`, testResultsDatabase, fromDate, toDate)
	// Board
	if len(boardRegex) > 0 {
		queryString = addQueryCriteria(queryString, "board", boardRegex)
	}
	// Exclude board
	if len(excludeBoardRegex) > 0 {
		queryString = addExcludeCriteria(queryString, "board", excludeBoardRegex)
	}

	// Failure reason
	if len(reasonRegex) > 0 {
		queryString = addQueryCriteria(queryString, "failure_reason", reasonRegex)
	}

	// Exclude failure reason
	if len(excludeReasonRegex) > 0 {
		queryString = addExcludeCriteria(queryString, "failure_reason", excludeReasonRegex)
	}

	// Build
	if len(buildRegex) > 0 {
		queryString = addQueryCriteria(queryString, "build", buildRegex)
	} else {
		// exclude non-release build
		queryString = queryString + "\n  AND REGEXP_CONTAINS(IFNULL(IF(REGEXP_CONTAINS(image, \"-release-\"), REGEXP_EXTRACT(build, \"R[^-]*-[^-]*\"), build), \"-\"), \"\\x5eR\")"
	}

	// Test
	if len(testRegex) > 0 {
		queryString = addQueryCriteria(queryString, "test", testRegex)
	}
	logErr("%v", queryString)
	return queryString, nil
}

// runQuery performs a query and calls |handleRow| for each row.
func runQuery(query, projectID string, handleRow func(map[string]bigquery.Value) error) error {
	ctx := context.Background()
	client, err := bigquery.NewClient(ctx, projectID)
	if err != nil {
		if strings.Contains(err.Error(), "could not find default credentials") {
			fmt.Fprint(os.Stderr, `Missing required gcloud application default credentials. To fix this:
1. On your gLinux host, run the following command and follow the instructions: gcloud auth application-default login
2. Copy ~/.config/gcloud/application_default_credentials.json from your gLinux host to the chroot.
`)
		}
		return err
	}

	q := client.Query(query)
	it, err := q.Read(ctx)
	if err != nil {
		return err
	}

	var wg sync.WaitGroup
	for {
		var r map[string]bigquery.Value
		err := it.Next(&r)
		if err == iterator.Done {
			break
		}
		if err != nil {
			return err
		}

		wg.Add(1)
		go func() {
			defer wg.Done()
			if err := handleRow(r); err != nil {
				logErr("Encountered error: %v while handle row: %v", err, r)
			}
		}()
	}
	wg.Wait()
	return nil
}

// buildRegexStringFromList creates a regular expression string from a list of
// strings that should match. The strings in |items| are not interpreted as
// regular expressions. The strings are also validated against
// |validateRegexString|, so a caller can ensure that they match an expected format
//
// Examples:
//
// buildRegexStringFromList([]string{"foo", "baz", "foo.baz"}, `.*`, true) will return
//
//	"^(foo|baz|foo\.baz)$", nil. The `.*` validation passes for any string. The
//	resulting regular expression will only match: "foo", "baz" or "foo.baz".
//
// buildRegexStringFromList([]string{"R108", "R108-15286.0.0"}, `^R[1-9][0-9]*$`, true)
//
//	will return an error since the second element of |items| fails to validate.
func buildRegexStringFromList(items []string, name, validateRegexString string, matchEntireWord bool) (string, error) {
	re, err := regexp.Compile(validateRegexString)
	if err != nil {
		return "", err
	}
	var result string
	if matchEntireWord {
		result = "^"
	}
	result = result + "("
	for i, item := range items {
		if !re.MatchString(item) {
			return "", errors.Errorf("could not match %v in %v", name, item)
		}
		if i > 0 {
			result = result + "|"
		}
		// QuoteMeta(item) produces a regular expression that matches |item|.
		result = result + regexp.QuoteMeta(item)
	}
	result = result + ")"
	if matchEntireWord {
		result = result + "$"
	}
	return result, nil
}

func formatTime(t time.Time) string {
	return fmt.Sprintf("%04d-%02d-%02d", t.Year(), t.Month(), t.Day())
}

func validateTime(t string) error {
	_, err := time.Parse("2006-01-02", t)
	return err
}

// processArguments converts the list-type command line arguments into
// regular expressions and ensures that they are correctly formatted.
func processArguments() error {
	var err error

	// Date range
	if len(toDate) == 0 && len(fromDate) == 0 {
		now := time.Now()
		toDate = formatTime(now)
		fromDate = formatTime(now.Add(-1 * time.Hour * 24 * time.Duration(7)))
	} else if len(toDate) == 0 {
		if err := validateTime(fromDate); err != nil {
			return errors.Errorf("could not parse from_date: %v", err)
		}
		toDate = formatTime(time.Now())
	} else if len(fromDate) == 0 {
		return errors.New("if --to_date is specified, --from_date must be specified")
	} else {
		if err := validateTime(fromDate); err != nil {
			return errors.Errorf("could not parse from_date: %v", err)
		}
		if err := validateTime(toDate); err != nil {
			return errors.Errorf("could not parse to_date: %v", err)
		}
	}

	// Defaults
	if len(excludeReasonRegex) == 0 {
		excludeReasonRegex = excludeReasonRegexDefault
	}
	return err
}

// editExpectations updates the elements in 'e' based on command line options.
func editExpectationsFromTestDB(exp map[string][]string) error {
	queryString, err := createTestResultsQueryString()
	if err != nil {
		return err
	}

	loadGPUFamily := func(url string) (string, error) {
		outStr, err := exec.Command("gsutil", "ls", url+"/**").Output()
		if err != nil {
			return "", errors.Wrap(err, "failed to run gsutil")
		}
		output := string(outStr)

		failureCSV := regexp.MustCompile(`.*dut-info.txt`)
		match := failureCSV.FindString(output)
		if match == "" {
			return "", errors.Errorf("dut-info.txt not found in %v", url)
		}

		outStr, err = exec.Command("gsutil", "cat", match).Output()
		if err != nil {
			return "", errors.Wrap(err, "failed to read dut-info.txt")
		}
		output = string(outStr)
		gpuRegex := regexp.MustCompile(`gpu_family: "(\w+)"`)
		got := gpuRegex.FindStringSubmatch(output)
		if got == nil {
			return "", errors.New("failed to find gpu_family in dut-info.txt")
		}
		return got[1], nil
	}

	// parsePiglitError := func()
	loadPiglitFailures := func(url string, testName string) ([][]string, error) {
		outStr, err := exec.Command("gsutil", "ls", url+"/**").Output()
		if err != nil {
			return nil, errors.Wrap(err, "failed to run gsutil")
		}
		output := string(outStr)
		failureCSV := regexp.MustCompile(fmt.Sprintf(`.*%v/piglit_results/failures.csv`, testName))
		match := failureCSV.FindString(output)
		if match == "" {
			return nil, errors.Errorf("failures.csv not found in %v", url)
		}
		outStr, err = exec.Command("gsutil", "cat", match).Output()
		if err != nil {
			return nil, errors.Wrap(err, "failed to read failures.csv")
		}
		output = string(outStr)

		failures := [][]string{}
		for _, line := range strings.Split(strings.TrimSpace(output), "\n") {
			split := strings.Split(line, ",")
			if len(split) != 2 {
				continue
			}
			test := split[0]
			reason := split[1]
			failures = append(failures, []string{test, reason})
		}

		return failures, nil
	}

	// Creates a closure to update exp from the stainless database.
	updateExpectationsFromQueryResult := func(r map[string]bigquery.Value) error {
		testName, hasRow := r["test"].(string)
		if !hasRow {
			return errors.Errorf("%v has no test field", r)
		}
		status, hasStatus := r["status"].(string)
		if !hasStatus {
			return errors.Errorf("%v has no status field", r)
		}
		board, hasBoard := r["board"].(string)
		if !hasBoard {
			return errors.Errorf("%v has no board field", r)
		}

		testName = strings.TrimPrefix(testName, "tast.")
		if strings.ToLower(status) == "fail" {
			url, hasURL := r["logs_url"].(string)
			if !hasURL {
				return errors.Errorf("%v has no logs_url field", r)
			}
			index := strings.Index(url, "chromeos-test-logs")
			gsURL := "gs://" + url[index:]

			gpuFamily, err := loadGPUFamily(gsURL)
			if err != nil {
				return errors.Wrap(err, "failed to map to GPU family")
			}

			failures, err := loadPiglitFailures(gsURL, testName)
			if err != nil {
				return errors.Wrapf(err, "failed to load piglit failures in %v", gsURL)
			}

			exp := skipExpectations[gpuFamily]
			for _, failure := range failures {
				if inList(exp, failure[0]) {
					continue
				}
				skipExpectationsMutex.Lock()
				skipExpectations[gpuFamily] = append(skipExpectations[gpuFamily], failure[0])
				skipExpectationsMutex.Unlock()
			}
			logErr("[%v:%v]\t%v\t%d failures", board, gpuFamily, testName, len(failures))
		}
		return nil
	}

	// Runs the query and updates the exp structure.
	return runQuery(queryString, stainlessProjectID, updateExpectationsFromQueryResult)
}

func logErr(format string, data ...interface{}) {
	fmt.Fprintf(os.Stderr, format+"\n", data...)
}

func log(format string, data **interface{}) {
	fmt.Fprintf(os.Stdout, format, data)
}

func inList(list []string, st string) bool {
	for _, v := range list {
		if v == st {
			return true
		}
	}
	return false
}

func main() {
	flag.Parse()

	err := processArguments()
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error with options: %v\n", err)
		os.Exit(1)
	}

	expectations := make(map[string][]string)

	fmt.Fprintf(os.Stderr, "Querying database for test results.\n")
	if err := editExpectationsFromTestDB(expectations); err != nil {
		logErr("%v", err)
		os.Exit(1)
	}

	for k, v := range skipExpectations {
		logErr("Found %d tests failures in the query for gpuFamily: %v", len(v), k)

		fileName := k + "-borealis-skips.txt"
		output, err := os.ReadFile(fileName)
		if err != nil {
			logErr("Failed to read %v", fileName)
		}
		list := strings.Split(strings.TrimSpace(string(output)), "\n")

		newFailure := []string{}
		for _, test := range v {
			if inList(list, test) {
				continue
			}
			newFailure = append(newFailure, test)
		}
		logErr("%d new failures not in %v expecation: ", len(newFailure), fileName)

		outStr := strings.Join(newFailure, "\n")
		if !appendOutput {
			logErr("%v", outStr)
		} else {
			f, err := os.OpenFile(fileName, os.O_CREATE|os.O_APPEND|os.O_RDWR, 0644)
			if err != nil {
				logErr("Failed to open/create %v: %v", fileName, err)
			}
			defer f.Close()
			f.WriteString(outStr)
		}
	}
}
