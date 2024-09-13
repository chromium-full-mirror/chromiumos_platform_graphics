// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tool for getting DEQP failures from ResultDB results.
// This tools utilize ../tast/create_expectations_from_stainless.go.
// Read ../tast/README.md for requirement.
//
// Once outside of chroot get credentials
// mkdir -p ~/cros/out/home/$USER/.config/gcloud/ && gcloud auth application-default login && cp ~/.config/gcloud/application_default_credentials.json ~/cros/out/home/$USER/.config/gcloud/
//
// Still outside of chroot query Testhaus
// ~/cros/src/platform/graphics/expectations/deqp$ ~/cros/src/platform/tast/tools/go.sh run fetch_failures_from_lab.go -build="16028.0.0" -gpu_family=mali-g72 | tee g72.txt

package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strings"
	"sync"
)

func errWrap(err error, str string) error {
	return fmt.Errorf("%v: %v", str, err.Error())
}

type DEQPResult struct {
	TestCase string
	Status   string
}

type SQLResult struct {
	Test          string `json:"test"`
	Status        string `json:"status"`
	GPUFamily     string `json:"gpu_family"`
	Board         string `json:"board"`
	Model         string `json:"model"`
	FailureReason string `json:"failure_reason"`
	LogsUrl       string `json:"logs_url"`

	Retried       bool `json:"retried"`
	IsControlFile bool `json:"is_control_file_result"`

	// DEQP testcases in failures.csv
	FailedTestCases []DEQPResult
}

var (
	flagOutput       string
	flagGPUFamily    string
	flagBoard        string
	flagExcludeBoard string
	flagModel        string
	flagExcludeModel string
	flagBuilds       string
)

func init() {
	flag.StringVar(&flagOutput, "output", "", "Output json file")

	flag.StringVar(&flagGPUFamily, "gpu_family", "", "If set, query specific GPU family.")
	flag.StringVar(&flagBoard, "board", "", "If set, query specific board.")
	flag.StringVar(&flagExcludeBoard, "exclude_board", "", "If set, exclude specific board.")
	flag.StringVar(&flagModel, "model", "", "If set, query specific model.")
	flag.StringVar(&flagExcludeModel, "exclude_model", "", "If set, exclude specific model.")
	flag.StringVar(&flagBuilds, "build", "", "build to query. Required.")
}

func getDEQPResults(ctx context.Context) ([]SQLResult, error) {
	scriptDir, err := os.Getwd()
	if err != nil {
		return nil, errWrap(err, "failed to get current working directory")
	}
	tastGoDir := filepath.Join(scriptDir, "../../../tast/tools/go.sh")
	createExpectations := filepath.Join(scriptDir, "../tast/create_expectations_from_stainless.go")
	cmds := []string{
		tastGoDir,
		"run",
		createExpectations,
		"--rawQuery",
		"--test_regex=" + `"parallel_dEQP.*|graphics.DEQP.*"`,
	}
	if flagGPUFamily != "" {
		cmds = append(cmds, "--gpu_family="+flagGPUFamily)
	}
	if flagBoard != "" {
		cmds = append(cmds, "--board="+flagBoard)
	}
	if flagExcludeBoard != "" {
		cmds = append(cmds, "--exclude_board="+flagExcludeBoard)
	}
	if flagModel != "" {
		cmds = append(cmds, "--model="+flagModel)
	}
	if flagExcludeModel != "" {
		cmds = append(cmds, "--exclude_model="+flagExcludeModel)
	}
	if flagBuilds != "" {
		cmds = append(cmds, "--build_regex="+flagBuilds)
	}
	cmdContext := exec.CommandContext(ctx, "bash", "-c", strings.Join(cmds, " "))
	output, err := cmdContext.CombinedOutput()
	if err != nil {
		return nil, errWrap(err, fmt.Sprintf("failed to run %v", cmdContext.String()))
	}
	var results []SQLResult
	if err := json.Unmarshal(output, &results); err != nil {
		return nil, err
	}
	return results, nil
}

func searchCorrectFailuresCSV(result SQLResult, failuresGSUrls []string) (string, error) {
	// Remove the first part of the testName as tauto or tast is prefixed.
	trimLeftDot := func(s string) string {
		for i := range s {
			if s[i] == '.' {
				return s[i+1:]
			}
		}
		return s
	}
	candidates := []string{}
	testNameShort := trimLeftDot(result.Test)

	if result.Retried && strings.Contains(result.Test, "tast") {
		testNameShort += ".1"
	}
	// For autotest, we are searching for results in results-1-$testNameShort/
	// For tast, we are searching for results in tests/$testNameShort/
	for _, gsUrl := range failuresGSUrls {
		if strings.Contains(gsUrl, testNameShort+"/") {
			candidates = append(candidates, gsUrl)
		}
	}
	if len(candidates) != 1 {
		return "", fmt.Errorf("Failed to identify %v's failures.csv among %v", result.Test, failuresGSUrls)
	}
	return candidates[0], nil
}

func fetchFailuresCSV(ctx context.Context, result SQLResult) ([]DEQPResult, error) {
	testName := result.Test
	if result.Status != "FAIL" {
		return nil, nil
	}
	// For autotest, We only cares if IsControlFile is false.
	if !strings.Contains(testName, "tast") && result.IsControlFile {
		return nil, nil
	}
	gsRoot := strings.ReplaceAll(result.LogsUrl, "/p/chromeos/logs/browse/", "gs://")
	searchPath := fmt.Sprintf("%v/**/failures.csv", gsRoot)
	out, err := exec.CommandContext(ctx, "gsutil", "ls", searchPath).Output()
	if err != nil {
		return nil, fmt.Errorf("no failures.csv in %v:%v", testName, gsRoot)
	}

	failuresGSUrls := strings.Split(strings.TrimSpace(string(out)), "\n")
	failuresGSUrl, err := searchCorrectFailuresCSV(result, failuresGSUrls)
	if err != nil {
		return nil, err
	}

	// Read failures.csv.
	output, err := exec.CommandContext(ctx, "gsutil", "cat", failuresGSUrl).Output()
	if err != nil {
		return nil, fmt.Errorf("Failed to read %v", failuresGSUrl)
	}
	var deqpResult []DEQPResult
	for _, line := range strings.Split(strings.TrimSpace(string(output)), "\n") {
		t := strings.Split(line, ",")
		if len(t) != 2 {
			return nil, fmt.Errorf("%v: line %q is not recognizable", failuresGSUrl, line)
		}
		deqpResult = append(deqpResult, DEQPResult{TestCase: t[0], Status: t[1]})
	}
	return deqpResult, nil
}

func main() {
	flag.Parse()
	if flagBuilds == "" {
		fmt.Println("--build is missing, run with -h for usage")
		os.Exit(1)
	}

	ctx := context.Background()
	results, err := getDEQPResults(ctx)
	if err != nil {
		log.Fatal(err)
	}

	var wg sync.WaitGroup
	for i, result := range results {
		wg.Add(1)
		go func() {
			defer wg.Done()
			deqpResult, err := fetchFailuresCSV(ctx, result)
			if err != nil {
				log.Println(result)
				log.Println(err)
				return
			}
			results[i].FailedTestCases = deqpResult
		}()
	}
	wg.Wait()

	inList := func(a string, l []string) bool {
		for _, b := range l {
			if a == b {
				return true
			}
		}
		return false
	}

	summary := make(map[string]map[string][]string)
	for _, result := range results {
		if len(result.FailedTestCases) == 0 {
			continue
		}
		if _, ok := summary[result.GPUFamily]; !ok {
			summary[result.GPUFamily] = make(map[string][]string)
		}
		for _, deqpResult := range result.FailedTestCases {
			failedTests := summary[result.GPUFamily][deqpResult.Status]
			if inList(deqpResult.TestCase, failedTests) {
				continue
			}
			summary[result.GPUFamily][deqpResult.Status] = append(failedTests, deqpResult.TestCase)
		}
	}

	for gpuFamily := range summary {
		for status := range summary[gpuFamily] {
			sort.Strings(summary[gpuFamily][status])
		}
	}

	jsonBytes, err := json.MarshalIndent(summary, "", "    ")
	if err != nil {
		log.Fatal(err)
	}
	if flagOutput != "" {
		if err := os.WriteFile(flagOutput, jsonBytes, 0644); err != nil {
			log.Fatal(err)
		}
	} else {
		fmt.Println(string(jsonBytes))
	}
}
