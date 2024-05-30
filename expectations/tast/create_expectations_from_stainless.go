// Copyright 2022 The ChromiumOS Authors
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
// $  ~/trunk/src/platform/tast/tools/go.sh run create_expectations_from_stainless.go -h

package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"os"
	"reflect"
	"regexp"
	"sort"
	"strings"
	"syscall"
	"time"

	"cloud.google.com/go/bigquery"
	"golang.org/x/crypto/ssh/terminal"
	"google.golang.org/api/iterator"
	"gopkg.in/yaml.v2"

	"go.chromium.org/tast-tests/cros/local/graphics/expectations"
	"go.chromium.org/tast/core/errors"
)

// stringSlice can be used as the local storage for storing list style
// arguments.
type stringSlice []string

// String is needed to use a stringSlice as argument storage.
func (v *stringSlice) String() string {
	return strings.Join(*v, " ")
}

// Set will append any new argument onto the list of arguments.
func (v *stringSlice) Set(s string) error {
	*v = append(*v, s)
	return nil
}

// Command line options are stored in the following file scoped variables

// SQL option
var rawQuery bool

// Input and output options
var input string
var output string
var updateInput bool

// YAML writing behavior options
var haltOnFlakes bool
var tickets stringSlice
var comments string

// Offline YAML editing options
var editTests bool
var deleteTests bool
var offlineEdit bool // This is a derived option. It equals deleteTests || editTests.

// Query options
var gpuFamilies stringSlice
var gpuFamiliesRegex string
var excludeGpuFamilies stringSlice
var excludeGpuFamiliesRegex string
var boards stringSlice
var boardRegex string
var excludeBoards stringSlice
var excludeBoardRegex string
var builds stringSlice
var buildRegex string
var models stringSlice
var modelRegex string
var excludeModels stringSlice
var excludeModelRegex string
var reasons stringSlice
var reasonRegex string
var excludeReasons stringSlice
var excludeReasonRegex string

const excludeReasonRegexDefault = "deadline exceeded|exit status 127|[Ll]ost SSH connection|GPU hang"

var tests stringSlice
var testRegex string
var fromDate string
var toDate string

var colorEnabled bool
var colorDisabled bool
var colorReset string
var colorRed string
var colorRedBold string
var colorGreen string
var colorGreenBold string
var colorYellow string
var colorYellowBold string
var colorMagentaBold string

// These regular expressions can be used for parameters validation
const modelValidationRegex = `^[a-z]+$`
const buildValidationRegex = `^R[1-9][0-9]*-[1-9][0-9]*\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$`
const testValidationRegex = `^tast\.[a-z]+\.[A-Z][a-zA-Z]*(\.[_a-zA-Z0-9]+)?$`

// Uses the internal Google ResultDB database for querying test results.
const testResultsDatabase = "cros-test-analytics.resultdb.cros_test_results"
const stainlessProjectID = "google.com:stainless-prod"

func init() {
	flag.Usage = func() {
		fmt.Fprintf(os.Stderr,
			"create_expectations_from_stainless updates a test expectations file "+
				"using results stored in stainless. Usage requires Google Cloud "+
				"Platform credentials. See README.md for prerequisites.\n"+
				"\nQuery options:\n",
				"\n--rawQuery: If specified, query the database without yaml editting. Optional\n"+
				"\nInput and output options:\n"+
				"\t--input: Path to YAML file to load. If \"-\", read from standard input. Optional\n"+
				"\t--output: Path to YAML file to write. May equal <input>. If not specified or set to \"-\", write to standard output. Optional\n"+
				"\t--update_input: If specified, write changes to the input file, which cannot be standard input. Optional\n"+
				"\nYAML writing options:\n"+
				"\t--ticket: The triaged issue. I.e. b/1234 or crbug/5678. For multiple tickets, use multiple arguments. Optional\n"+
				"\t--comments: A description of the issue or rationale for expecting the failure. Optional\n"+
				"\t--halt_on_flakes: If specified, halt when a test has both passes and fails. Optional\n"+
				"\nOffline YAML editing options:\n"+
				"\t--edit_tests: Perform an offline edit of tickets or comments for specified tests. Requires --input. Optional.\n"+
				"\t--delete_tests: Perform an offline deletion of expectations for specified tests. Requires --input. Optional.\n"+
				"\nTest result query options:\n"+
				"\t--gpu_family: GPU family to query for expectations. Not a regular expresion. For multiple families, use multiple arguments. Optional\n"+
				"\t--gpu_family_regex: Regular expression for GPU families to match. Optional\n"+
				"\t--exclude_gpu_family: GPU family to exclude from results. Not a regular expression. For multiple families, use multiple arguments. Optional\n"+
				"\t--exclude_gpu_family_regex: Regular expression for GPU families to exclude from results. Optional\n"+
				"\t--board: Board to query for expectations. Not a regular expression. For multiple boards, use multiple arguments Optional\n"+
				"\t--board_regex: Regular expression for boards to match. Optional\n"+
				"\t--exclude_board: Board to exclude from results. Not a regular expression. For multiple boards, use multiple arguments Optional\n"+
				"\t--exclude_board_regex: Regular expression for boards to exclude from results. Optional\n"+
				"\t--model: Model to query for expectations. Not a regular expression. For multiple models, use multiple arguments Optional\n"+
				"\t--model_regex: Regular expression for models to match. Optional\n"+
				"\t--exclude_model: Model to exclude from results. Not a regular expression. For multiple models, use multiple arguments Optional\n"+
				"\t--exclude_model_regex: Regular expression for models to exclude from results. Optional\n"+
				"\t--build: Build to query for expectations. Not a regular expression. For multiple builds, use multiple arguments Optional\n"+
				"\t--build_regex: Regular expression for builds to match. Optional\n"+
				"\t--reason: Failure reason to match for expectations. Not a regular expression. For multiple reasons, use multiple arguments. Optional\n"+
				"\t--reason_regex: Regular expression for failure reasons to match. Optional\n"+
				"\t--exclude_reason: Failure reason to exclude from results. Not a regular expression. For multiple reasons, use multiple arguments. Optional\n"+
				"\t--exclude_reason_regex: Regular expression for failure reason to exclude from results. Optional. Default '%s'\n"+
				"\t--test: Test name to query for expectations. Not a regular expression. For multiple tests, use multiple arguments. Optional\n"+
				"\t--test_regex: Regular expression for tests to match. The user must provide either --test or --test_regex.\n"+
				"\t--from_date: the start of the date range. Format: YYYY-MM-DD. Optional. Default date range is the last 7 days.\n"+
				"\t--to_date: the end of the date range. Format: YYYY-MM-DD. Optional\n"+
				"\t--color: Colorize output log\n"+
				"\t--no_color: Do not colorize output log\n", excludeReasonRegexDefault)
	}

	flag.BoolVar(&rawQuery, "rawQuery", rawQuery, "If specified, output the result of the query.")

	flag.StringVar(&input, "input", "", "Path to YAML file to load. If \"-\", read from standard input.")
	flag.StringVar(&output, "output", "", "Path to YAML file to write. If not specified or set to \"-\", write to standard output.")
	flag.BoolVar(&updateInput, "update_input", updateInput, "If specified, write changes to the input file, which cannot be standard input.")

	flag.Var(&tickets, "ticket", "The triaged issue. I.e. b/1234 or crbug/5678. For multiple tickets, use multiple arguments")
	flag.StringVar(&comments, "comments", "", "A description of the issue or rationale for expecting the failure")
	flag.BoolVar(&haltOnFlakes, "halt_on_flakes", haltOnFlakes, "If specified, halt when a test has both passes and fails.")
	flag.BoolVar(&editTests, "edit_tests", editTests, "Perform an offline edit of tickets or comments for specified tests.")
	flag.BoolVar(&deleteTests, "delete_tests", deleteTests, "Perform an offline deletion of expectations for matching tests.")
	flag.Var(&gpuFamilies, "gpu_family", "GPU family to query for expectations. Not a regular expresion. For multiple families, use multiple arguments")
	flag.StringVar(&gpuFamiliesRegex, "gpu_family_regex", "", "Regular expression for GPU families to match")
	flag.Var(&excludeGpuFamilies, "exclude_gpu_family", "GPU family to exclude from results. Not a regular expression. For multiple families, use multiple arguments")
	flag.StringVar(&excludeGpuFamiliesRegex, "exclude_gpu_family_regex", "", "Regular expression for GPU families to exclude from results")
	flag.Var(&boards, "board", "board to query for expectations. Not a regular expression. For multiple boards, use multiple arguments")
	flag.StringVar(&boardRegex, "board_regex", "", "regular expression for boards to match")
	flag.Var(&excludeBoards, "exclude_board", "board to exclude for expectations. Not a regular expression. For multiple boards, use multiple arguments")
	flag.StringVar(&excludeBoardRegex, "exclude_board_regex", "", "regular expression for boards to exclude")
	flag.Var(&models, "model", "model to query for expectations. Not a regular expression. For multiple models, use multiple arguments")
	flag.StringVar(&modelRegex, "model_regex", "", "regular expression for models to match")
	flag.Var(&excludeModels, "exclude_model", "model to exclude for expectations. Not a regular expression. For multiple models, use multiple arguments")
	flag.StringVar(&excludeModelRegex, "exclude_model_regex", "", "regular expression for models to exclude")
	flag.Var(&builds, "build", "build to query for expectations. Not a regular expression. For multiple builds, use multiple arguments")
	flag.StringVar(&buildRegex, "build_regex", "", "regular expression for builds to match")
	flag.Var(&tests, "test", "test name to query for expectations. Not a regular expression. For multiple tests, use multiple arguments")
	flag.StringVar(&testRegex, "test_regex", "", "regular expression for tests to match")
	flag.Var(&reasons, "reason", "failure reason to match for expectations. Not a regular expression. For multiple reasons, use multiple arguments")
	flag.StringVar(&reasonRegex, "reason_regex", "", "regular expression for failure reasons to match")
	flag.Var(&excludeReasons, "exclude_reason", "failure reason exclude match for expectations. Not a regular expression. For multiple reasons, use multiple arguments")
	flag.StringVar(&excludeReasonRegex, "exclude_reason_regex", "", "regular expression for failure reasons to exclude")
	flag.StringVar(&fromDate, "from_date", "", "the start of the date range. Format: YYYY-MM-DD")
	flag.StringVar(&toDate, "to_date", "", "the end of the date range. Format: YYYY-MM-DD")
	flag.BoolVar(&colorEnabled, "color", colorEnabled, "Colorize the output log")
	flag.BoolVar(&colorDisabled, "no_color", colorDisabled, "Do not colorize the output log")
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
	var queryString string
	if rawQuery {
		queryString = "SELECT * EXCEPT(bot_dimensions)\n"
	} else {
		queryString = `SELECT
  IFNULL(test, "-") AS row,
  "*" AS col,
  SUM(IF(status IN ("PASS"), 1, 0)) AS pass,
  SUM(IF(status IN ("WARN"), 1, 0)) AS warn,
  SUM(IF(status IN ("FAIL", "ERROR", "ABORT") AND NOT REGEXP_CONTAINS(failure_reason, "Test passed! Consider removing FAIL expectation"), 1, 0)) AS fail,
  SUM(IF(status IN ("FAIL", "ERROR", "ABORT") AND REGEXP_CONTAINS(failure_reason, "Test passed! Consider removing FAIL expectation"), 1, 0)) AS unexpected_pass,
  SUM(IF(status NOT IN ("GOOD", "WARN", "FAIL", "ERROR", "ABORT", "NOT_RUN"), 1, 0)) AS other,
  SUM(IF(status IN ("NOT_RUN"), 1, 0)) AS notrun,
  MIN(IFNULL(IF(REGEXP_CONTAINS(image, "-release-"), REGEXP_EXTRACT(build, "R[^-]*-[^-]*"), build), "-")) AS first_build`
	}
	queryString += fmt.Sprintf(`
FROM %s
WHERE
  queued_time BETWEEN "%s 00:00:00 UTC" AND "%s 00:00:00 UTC"
  AND (suite IS NULL OR NOT REGEXP_CONTAINS(suite, r"^(au$|paygen_au)"))
  AND job_name NOT LIKE "git_%%"`, testResultsDatabase, fromDate, toDate)
	// GPU family
	if len(gpuFamiliesRegex) > 0 {
		queryString = addQueryCriteria(queryString, "gpu_family", gpuFamiliesRegex)
	}

	// Exclude GPU family
	if len(excludeGpuFamiliesRegex) > 0 {
		queryString = addExcludeCriteria(queryString, "gpu_family", excludeGpuFamiliesRegex)
	}

	// Board
	if len(boardRegex) > 0 {
		queryString = addQueryCriteria(queryString, "board", boardRegex)
	}

	// Exclude board
	if len(excludeBoardRegex) > 0 {
		queryString = addExcludeCriteria(queryString, "board", excludeBoardRegex)
	}

	// Model
	if len(modelRegex) > 0 {
		queryString = addQueryCriteria(queryString, "model", modelRegex)
	}

	// Exclude model
	if len(excludeModelRegex) > 0 {
		queryString = addExcludeCriteria(queryString, "model", excludeModelRegex)
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

	if !rawQuery {
		queryString += "\nGROUP BY\n  `row`, `col`\nORDER BY row\n;"
	}
	return queryString, nil
}

// runQuery performs a query and calls |handler.handle| for each row.
func runQuery(query, projectID string, handler SQLHandler) error {
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

	for {
		var r map[string]bigquery.Value
		err := it.Next(&r)
		if err == iterator.Done {
			break
		}
		if err != nil {
			return err
		}

		err = handler.handle(r)
		if err != nil {
			return err
		}
	}
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
			return "", errors.Errorf("invalid %s value %s", name, item)
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

	// updateInput
	if (len(input) == 0 || input == "-") && updateInput {
		return errors.New("if 'input' is not a file, then the 'update_input' flag must be false")
	}
	if len(output) > 0 && updateInput {
		return errors.New("if 'output' is specified, then the 'update_input' flag must be false")
	}
	if updateInput {
		output = input
	}

	// Sorts tickets so they will be sorted in the YAML.
	if len(tickets) > 1 {
		sort.Strings(tickets)
	}

	// GPU family
	if len(gpuFamilies) > 0 && len(gpuFamiliesRegex) > 0 {
		return errors.New("cannot specify --gpu_family and --gpu_family_regex")
	}
	if len(gpuFamilies) > 0 {
		gpuFamiliesRegex, err = buildRegexStringFromList(gpuFamilies, "gpu_family", `^[a-z0-9\-_]+$`, true)
		if err != nil {
			return err
		}
	}

	// Exclude GPU family
	if len(excludeGpuFamilies) > 0 && len(excludeGpuFamiliesRegex) > 0 {
		return errors.New("cannot specify --exclude_gpu_family and --exclude_gpu_family_regex")
	}
	if len(excludeGpuFamilies) > 0 {
		excludeGpuFamiliesRegex, err = buildRegexStringFromList(excludeGpuFamilies, "exclude_gpu_family", `^[a-z0-9\-_]+$`, true)
		if err != nil {
			return err
		}
	}

	// Board
	if len(boards) > 0 && len(boardRegex) > 0 {
		return errors.New("cannot specify --board and --board_regex")
	}
	if len(boards) > 0 {
		boardRegex, err = buildRegexStringFromList(boards, "board", `^[a-z]+(-[a-z]+)*$`, true)
		if err != nil {
			return err
		}
	}

	// Exclude board
	if len(excludeBoards) > 0 && len(excludeBoardRegex) > 0 {
		return errors.New("cannot specify --exclude_board and --exclude_board_regex")
	}
	if len(excludeBoards) > 0 {
		excludeBoardRegex, err = buildRegexStringFromList(excludeBoards, "exclude_board", `^[a-z]+(-[a-z]+)*$`, true)
		if err != nil {
			return err
		}
	}

	// Model
	if len(models) > 0 && len(modelRegex) > 0 {
		return errors.New("cannot specify --model and --model_regex")
	}
	if len(models) > 0 {
		modelRegex, err = buildRegexStringFromList(models, "model", modelValidationRegex, true)
		if err != nil {
			return err
		}
	}

	// Exclude model
	if len(excludeModels) > 0 && len(excludeModelRegex) > 0 {
		return errors.New("cannot specify --exclude_model and --exclude_model_regex")
	}
	if len(excludeModels) > 0 {
		excludeModelRegex, err = buildRegexStringFromList(excludeModels, "exclude_model", modelValidationRegex, true)
		if err != nil {
			return err
		}
	}

	// Build
	if len(builds) > 0 && len(buildRegex) > 0 {
		return errors.New("cannot specify --build and --build_regex")
	}
	if len(builds) > 0 {
		buildRegex, err = buildRegexStringFromList(builds, "build", buildValidationRegex, true)
		if err != nil {
			return err
		}
	}

	// Failure reason
	if len(reasons) > 0 && len(reasonRegex) > 0 {
		return errors.New("cannot specify --reason and --reason_regex")
	}
	if len(reasons) > 0 {
		reasonRegex, err = buildRegexStringFromList(reasons, "reason", ".*", false)
		if err != nil {
			return err
		}
	}

	// Exclude reason
	if len(excludeReasons) > 0 && len(excludeReasonRegex) > 0 {
		return errors.New("cannot specify --exclude_reason and --exclude_reason_regex")
	}
	if len(excludeReasons) > 0 {
		excludeReasonRegex, err = buildRegexStringFromList(excludeReasons, "exclude_reason", ".*", false)
		if err != nil {
			return err
		}
	}

	// Tests
	if len(tests) > 0 && len(testRegex) > 0 {
		return errors.New("cannot specify --test and --test_regex")
	}
	if len(tests) > 0 {
		testRegex, err = buildRegexStringFromList(tests, "test", testValidationRegex, true)
		if err != nil {
			return err
		}
	}
	if len(testRegex) == 0 {
		return errors.New("must specify at least one of --test or --test_regex")
	}

	// Date range
	if len(toDate) == 0 && len(fromDate) == 0 {
		now := time.Now()
		toDate = formatTime(now)
		fromDate = formatTime(now.Add(-1 * time.Hour * 24 * time.Duration(7)))
	} else if len(toDate) == 0 {
		if err := validateTime(fromDate); err != nil {
			return fmt.Errorf("could not parse from_date: %v", err)
		}
		toDate = formatTime(time.Now())
	} else if len(fromDate) == 0 {
		return errors.New("if --to_date is specified, --from_date must be specified")
	} else {
		if err := validateTime(fromDate); err != nil {
			return fmt.Errorf("could not parse from_date: %v", err)
		}
		if err := validateTime(toDate); err != nil {
			return fmt.Errorf("could not parse to_date: %v", err)
		}
	}

	// Offline editing options
	if editTests && deleteTests {
		return errors.New("cannot delete expectations and edit them at the same time. Provide at most one of 'edit_tests' and 'delete_tests'")
	}

	if editTests && len(tickets) == 0 && len(comments) == 0 {
		return errors.New("if 'edit_tests' is specified, then one of 'ticket' or 'comments' must be specified")
	}

	offlineEdit = deleteTests || editTests

	if offlineEdit {
		if len(input) == 0 {
			return errors.New("if 'delete_tests' or 'edit_tests' is specified, then 'input' must be specified")
		}
		if len(boardRegex) > 0 || len(excludeBoardRegex) > 0 || len(buildRegex) > 0 || len(modelRegex) > 0 || len(excludeModelRegex) > 0 || len(reasonRegex) > 0 || len(excludeReasonRegex) > 0 || len(gpuFamiliesRegex) > 0 || len(excludeGpuFamiliesRegex) > 0 {
			return errors.New("if one of 'delete_tests' or 'edit_tests' is specified, then 'gpuFamily', 'gpuFamilyRegex', 'excludeGpuFamily', 'excludeGpuFamilyRegex', 'board', 'boardRegex', 'excludeBoard', 'excludeBoardRegex', 'build', 'buildRegex', 'model', 'modelRegex', 'exclude_model', 'exclude_model_regex', 'reason', reason_regex', 'exclude_reason', 'exclude_reason_regex' must be unspecified")
		}
	}

	// Defaults
	if len(excludeReasonRegex) == 0 {
		excludeReasonRegex = excludeReasonRegexDefault
	}

	if colorEnabled && colorDisabled {
		return errors.New("cannot specify --color && --no_color")
	}
	if colorEnabled || (!colorDisabled && terminal.IsTerminal(syscall.Stderr)) {
		colorReset = "\033[0;0m"
		colorRed = "\033[0;31m"
		colorRedBold = "\033[1;31m"
		colorGreen = "\033[0;32m"
		colorGreenBold = "\033[1;32m"
		colorYellow = "\033[0;33m"
		colorYellowBold = "\033[1;33m"
		colorMagentaBold = "\033[1;35m"
	}

	return err
}

// deleteTestExpectations deletes test expectations that match 're' from 'exp'.
func deleteTestExpectations(exp map[string]expectations.Expectation, re *regexp.Regexp) {
	for testName := range exp {
		if !re.MatchString("tast." + testName) {
			continue
		}
		fmt.Fprintf(os.Stderr, colorGreen+"Deleting expectation for "+colorGreenBold+"%s"+colorGreen+".\n"+colorReset, testName)
		delete(exp, testName)
	}
}

// editTestExpectations edits test expectations that match 're' using the tickets
// and comments arguments.
func editTestExpectations(exp map[string]expectations.Expectation, re *regexp.Regexp) {
	for testName, expectation := range exp {
		if !re.MatchString("tast." + testName) {
			continue
		}

		if len(comments) > 0 && comments != expectation.Comments {
			fmt.Fprintf(os.Stderr, "Editing expectation for %s with the specified comments.\n", testName)
			expectation.Comments = comments
		}

		if len(tickets) > 0 && !reflect.DeepEqual([]string(tickets), expectation.Tickets) {
			fmt.Fprintf(os.Stderr, "Editing expectation for %s with the specified tickets.\n", testName)
			expectation.Tickets = tickets
		}

		exp[testName] = expectation
	}
}

// countTrueArgs returns the number of true bool arguments.
func countTrueArgs(bools ...bool) int {
	count := 0
	for _, b := range bools {
		if b {
			count = count + 1
		}
	}
	return count
}

// runAndProcessLabResults fetchs results from the lab and process it with SQLHandler.
func runAndProcessLabResults(handler SQLHandler) error {
	queryString, err := createTestResultsQueryString()
	if err != nil {
		return err
	}
	return runQuery(queryString, stainlessProjectID, handler)
}

// SQLHandler interface defines how to process an SQL result and output the results.
type SQLHandler interface {
	// handle handles an entry of the SQL result.
	handle(r map[string]bigquery.Value) error
	// output outputs the processed results.
	output() error
}

type rawHandler struct {
	results []map[string]bigquery.Value
}

func (y *rawHandler) handle(r map[string]bigquery.Value) error {
	y.results = append(y.results, r)
	return nil
}

func (y *rawHandler) output() error {
	out, err := json.MarshalIndent(y.results, "", "    ")
	if err != nil {
		return errors.Wrap(err, "failed to marshal")
	}
	fmt.Println(string(out))
	return nil
}

type yamlHandler struct {
	exp map[string]expectations.Expectation
}

func (y *yamlHandler) handle(r map[string]bigquery.Value) error {
	testName, hasRow := r["row"].(string)
	if !hasRow {
		return nil
	}

	// A test result can be have at most one of the following states:
	// pass, fail, or unexpected_pass
	// unexpected_pass happens when the test fails, but the reason
	// is "Test passed! Consider removing FAIL expectation". I.e
	// there is a test expectation that is no longer needed.
	pass := r["pass"].(int64) > 0
	fail := r["fail"].(int64) > 0
	unexpectedPass := r["unexpected_pass"].(int64) > 0
	sinceBuild := r["first_build"].(string)

	if countTrueArgs(pass, fail, unexpectedPass) == 0 {
		// This ignore tests that didn't run.
		fmt.Fprintf(os.Stderr, colorYellow+"Test "+colorYellowBold+"%s"+colorYellow+" had no passing or failing results. Skipping.\n"+colorReset, testName)
		return nil
	}

	if countTrueArgs(pass, fail, unexpectedPass) > 1 {
		// There were different types of test results in the query.
		if haltOnFlakes {
			return errors.Errorf("test %s has both passes and fails. Halting due to --halt_on_flakes flag. Please adjust the query range", testName)
		}

		// There is no clear determination for what to do with the expectations file.
		// This skips updating the file.
		colorPass := colorYellow
		colorFail := colorYellow
		colorUnexpectedPass := colorYellow
		if pass {
			colorPass = colorGreenBold
		}
		if fail {
			colorFail = colorRedBold
		}
		if unexpectedPass {
			colorUnexpectedPass = colorMagentaBold
		}
		fmt.Fprintf(os.Stderr, colorYellow+"Test "+colorYellowBold+"%s"+colorYellow+" had "+colorPass+"%d pass, "+colorFail+"%d fail, "+colorUnexpectedPass+"and %d unexpected pass"+colorYellow+" results. Skipping.\n"+colorReset,
			testName, r["pass"], r["fail"], r["unexpected_pass"])
		return nil
	}

	yamlTestName := strings.TrimPrefix(testName, "tast.")
	if _, ok := y.exp[yamlTestName]; ok {
		// There is an existing expectation that may need to be updated.
		if unexpectedPass {
			// Test is failing, but the fail reason is:
			// "Test passed! Consider removing FAIL expectation"
			// This particular error message is generated by the
			// expectations package when there is a FAIL expectation
			// but the test passes.
			//
			// For this case, the FAIL expectation should be deleted.
			fmt.Fprintf(os.Stderr, colorGreen+"Deleting expectation for "+colorGreenBold+"%s"+colorGreen+" since the test passes.\n"+colorReset, testName)
			delete(y.exp, yamlTestName)
		}
	} else if fail {
		// The test only has failures - create an expectation and update exp
		fmt.Fprintf(os.Stderr, colorRed+"Building "+colorRedBold+"%s"+colorRed+" expectation for "+colorRedBold+"%s"+colorRed+"\n"+colorReset, expectations.ExpectFailure, testName)
		y.exp[yamlTestName] = expectations.Expectation{
			Expectation: expectations.ExpectFailure,
			Tickets:     tickets,
			Comments:    comments,
			SinceBuild:  sinceBuild,
		}
	}
	return nil
}

func (y *yamlHandler) configure() error {
	var err error
	// Loads expectations from the specified file.
	// This lets us update any existing expectations.
	if len(input) > 0 {
		var contents []byte
		// If the input is "-" then it is read from standard input
		if input == "-" {
			contents, err = io.ReadAll(os.Stdin)
		} else {
			fmt.Fprintf(os.Stderr, "Loading YAML file %s.\n", input)
			contents, err = os.ReadFile(input)
		}
		if err != nil {
			return errors.Wrap(err, "failed to load yaml")
		}
		if err := yaml.Unmarshal(contents, &y.exp); err != nil {
			return errors.Wrap(err, "failed to unmarshal yaml")
		}
	}
	if offlineEdit {
		re, err := regexp.Compile(testRegex)
		if err != nil {
			return errors.Wrap(err, "invalid test name regular expression")
		}
		fmt.Fprintf(os.Stderr, "Performing an offline edit of the YAML file. Stainless will not be queried.\n")
		if deleteTests {
			deleteTestExpectations(y.exp, re)
		}
		if editTests {
			editTestExpectations(y.exp, re)
		}
	}
	return nil
}

func (y *yamlHandler) output() error {
	// Copy expectations to MapSlice and sort them
	mapSliceExp := yaml.MapSlice{}
	for testName, expectation := range y.exp {
		mapSliceExp = append(mapSliceExp, yaml.MapItem{testName, expectation})
	}
	sort.Slice(mapSliceExp, func(i, j int) bool {
		return mapSliceExp[i].Key.(string) < mapSliceExp[j].Key.(string)
	})
	// Writes the updated YAML
	contents, err := yaml.Marshal(mapSliceExp)
	if err != nil {
		return errors.Wrap(err, "failed to marshal")
	}
	if output == "-" || len(output) == 0 {
		os.Stdout.Write(contents)
	} else {
		fmt.Fprintf(os.Stderr, "Writing YAML file %s.\n", output)
		// Uses the existing file mode if the file already exists
		fileMode := os.FileMode(int(0644))
		if fileData, _ := os.Stat(output); fileData != nil {
			fileMode = fileData.Mode()
		}
		os.WriteFile(output, contents, fileMode)
	}
	return nil
}

func main() {
	flag.Parse()

	if err := processArguments(); err != nil {
		fmt.Fprintf(os.Stderr, "Error with options: %v\n", err)
		os.Exit(1)
	}

	var handler SQLHandler
	if rawQuery {
		// Perform raw query.
		handler = &rawHandler{}
		if err := runAndProcessLabResults(handler); err != nil {
			fmt.Fprintf(os.Stderr, "%v\n", err)
			os.Exit(1)
		}
	} else {
		// Perform yaml query.
		yamlExpectations := make(map[string]expectations.Expectation)
		yamlHandle := yamlHandler{exp: yamlExpectations}
		handler = &yamlHandle
		if err := yamlHandle.configure(); err != nil {
			fmt.Fprintf(os.Stderr, "Failed to configure yamlHandler %v\n", err)
			os.Exit(1)
		}
		if !offlineEdit {
			fmt.Fprintf(os.Stderr, "Querying Stainless for test results.\n")
			if err := runAndProcessLabResults(handler); err != nil {
				fmt.Fprintf(os.Stderr, "%v\n", err)
				os.Exit(1)
			}
		}
	}
	if err := handler.output(); err != nil {
		fmt.Fprintf(os.Stderr, "%v\n", err)
		os.Exit(1)
	}
	return
}
