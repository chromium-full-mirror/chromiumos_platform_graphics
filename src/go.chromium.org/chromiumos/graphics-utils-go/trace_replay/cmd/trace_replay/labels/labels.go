// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package labels filter traces entries based on their labels.
package labels

import (
	"strings"

	"go.chromium.org/chromiumos/graphics-utils-go/trace_replay/cmd/trace_replay/repo"
)

// checks if a set of labels |a| is a subset of labels |b|
func matchLabels(a *[]string, b *[]string) bool {
	if len(*a) == 0 || len(*b) == 0 {
		return false
	}

	for _, aval := range *a {
		bFound := false
		for _, bval := range *b {
			if strings.EqualFold(aval, bval) {
				bFound = true
				break
			}
		}
		if bFound == false {
			return false
		}
	}
	return true
}

// GetTraceEntries function selects the trace entries for the specified labels
func GetTraceEntries(traceList *repo.TraceList, queryLabels *[]string) ([]repo.TraceListEntry, error) {
	var result []repo.TraceListEntry
	for _, entry := range traceList.Entries {
		if matchLabels(queryLabels, &entry.Labels) == true {
			result = append(result, entry)
		}
	}
	return result, nil
}
