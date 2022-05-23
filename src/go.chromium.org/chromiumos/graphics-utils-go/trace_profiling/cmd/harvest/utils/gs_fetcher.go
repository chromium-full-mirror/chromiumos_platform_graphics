// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package utils

import (
	"fmt"
	"path"
	"go.chromium.org/chromiumos/graphics-utils-go/trace_profiling/cmd/profile/remote"
)

// GSFetcher is a helper class that knows how to fetch trace data from Google
// Storage and extract archives. For each trace-data item, it delivers a
// TraceRecord that, among other things, includes a file path to the local
// trace-data file. GSFetcher uses a local cache to stored downloaded trace data.
// If a files is available in the local cache, it will not be downloaded again.
type GSFetcher struct {
	cacheDir     string            // Path to local cache dir for trace data.
	traceDataOut chan *TraceRecord // Trace records are fed to that out channel once ready.
	errorOut     chan error        // Errors during the download or archive extractions are fed to that channel.
	verbose      bool              // Whether to be verbose during download and extraction.
}

// NewGSFetcher creates and return a new GSFetcher object.
func NewGSFetcher(
	localCacheDir string,
	traceDataOut chan *TraceRecord,
	errorOut chan error,
	verbose bool) *GSFetcher {

	gs := GSFetcher{
		cacheDir:     localCacheDir,
		traceDataOut: traceDataOut,
		errorOut:     errorOut,
		verbose:      verbose,
	}
	return &gs
}

// FetchTraceData downloads and extracts the trace data for the given list of file
// URIs. A URIs usually points to an archive in Google Storage. However, that is
// not mandatory. FetchTraceData can handle any of the followings:
//  - A GS path to an archive,
//  - a GS path to a trace file,
//  - a local path to an archive, or
//  - a local path to a trace file.
// Once a trace is available locally, it is wrapped in a TraceRecord with incidental
// information and fed to channel traceDataOut given to NewGSFetcher.
func (gf *GSFetcher) FetchTraceData(fileUris []string) {
	// Download files in a background goroutine.
	var traceQueue = make(chan *TraceRecord)
	go func() {
		for _, trace := range fileUris {
			if remote.IsGoogleStorageURI(trace) {
				localTrace, err := gf.fetchTraceFromStorage(trace)
				if err != nil {
					// Report error and keep going with the next trace.
					gf.errorOut <- fmt.Errorf("failed to fetch trace <%s>:\n  err=%s", trace, err.Error())
				} else {
					traceID := GenTraceIDFromGoogleStoragePath(trace)
					gf.printIfVerbose("Trace ID: %s\n", traceID)
					traceQueue <- CreateTraceRecord(traceID, localTrace, false /* not from local file */, gf.verbose)
				}
			} else {
				traceQueue <- CreateTraceRecord("", trace, true /* from local file */, gf.verbose)
			}
		}
		close(traceQueue)
	}()

	// Grab files as they are downloaded and extract the trace data as needed.
	// (TraceRecord does the archive extraction and related caching work.)
	for traceRecord := range traceQueue {
		if err := traceRecord.LoadFromTraceData(); err != nil {
			gf.errorOut <- fmt.Errorf("failed to extract data from archive %s, err=%s",
				traceRecord.GetLocalTraceDataPath(), err.Error())
			continue
		}

		gf.traceDataOut <- traceRecord
	}

	gf.traceDataOut <- nil
}

// Download a file from Google Storage, unless it's already cached locally.
func (gf *GSFetcher) fetchTraceFromStorage(trace string) (string, error) {
	_, filename := path.Split(trace)
	filepath := path.Join(gf.cacheDir, filename)
	if !FileExists(filepath) {
		_, err := remote.FetchFromGS(filepath, trace)
		if err != nil {
			return "", err
		}
	}

	return filepath, nil
}

// If verbose mode is enabled, print the formatted string.
func (gf *GSFetcher) printIfVerbose(format string, a ...interface{}) {
	if gf.verbose {
		fmt.Printf(format, a...)
	}
}
