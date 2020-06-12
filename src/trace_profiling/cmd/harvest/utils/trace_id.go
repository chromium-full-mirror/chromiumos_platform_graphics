// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package utils

import (
	"path/filepath"
	"regexp"
)

var reIsAllDigits = regexp.MustCompile(`^[0-9]+$`)

func extractDirPathAndFilename(gsPath string) (string, string) {
	dirPath, filename := filepath.Split(gsPath)
	filename, _ = SplitFileExt(filename) // Remove all extensions, e.g. .tar.bz2.
	return dirPath, filename
}

func genTVCTraceID(gsPath string) (bool, string) {
	// E.g. gs://chromeos-gfx/traces/Steam/steam.game.reports/steam_10-counter_strike-20200409_144356.tar
	// Trace ID = "tvc_steam_10-counter_strike-20200409_144356"
	if ok, _ := regexp.MatchString(`(?i:^gs://chromeos-gfx/traces/Steam/steam.game.reports/)`, gsPath); ok {
		_, filename := extractDirPathAndFilename(gsPath)
		return true, "tvc_" + filename
	}

	return false, ""
}

func genUncuratedTVCTraceID(gsPath string) (bool, string) {
	if ok, _ := regexp.MatchString(`(?i:^gs://chromeos-gfx-traces-incoming/)`, gsPath); ok {
		dirPath, filename := extractDirPathAndFilename(gsPath)
		parentDir := filepath.Base(dirPath)

		if parentDir != "steam.copied" {
			// E.g. gs://chromeos-gfx-traces-incoming/steam.copied/out/steam_220200-kerbal_space_program-20200406_163848.tar
			// Trace ID = "tvc_uncurated_out_steam_220200-kerbal_space_program-20200406_163848"
			// E.g. gs://chromeos-gfx-traces-incoming/steam.copied/final/steam_10-counter_strike-20200409_144356.tar
			// Trace ID = "tvc_uncurated_final_steam_10-counter_strike-20200409_144356"
			return true, "tvc_uncurated_" + parentDir + "_" + filename
		}

		// E.g. gs://chromeos-gfx-traces-incoming/steam.copied/2020-01-23-121354-322330.tar.bz2
		// Trace ID = "tvc_uncurated_2020-01-23-121354-322330"
		return true, "tvc_uncurated_" + filename
	}

	return false, ""
}

func genUncuratedTVCSonaTraceID(gsPath string) (bool, string) {
	// E.g. gs://chromeos-gfx/traces/Steam/steam.sona.incoming/2020-01-21-122525-105600.tar.bz2
	// Trace ID = "tvc_uncurated_sona_2020-01-21-122525-105600"
	if ok, _ := regexp.MatchString(`(?i:^gs://chromeos-gfx/traces/Steam/steam.sona.incoming/)`, gsPath); ok {
		_, filename := extractDirPathAndFilename(gsPath)
		return true, "tvc_uncurated_sona_" + filename
	}

	return false, ""
}

func genLunarGTraceID(gsPath string) (bool, string) {
	if ok, _ := regexp.MatchString(`(?i:^gs://chromeos-gfx/traces/LunarG/)`, gsPath); ok {
		dirPath, filename := extractDirPathAndFilename(gsPath)
		parentDir := filepath.Base(dirPath)

		if reIsAllDigits.Match([]byte(parentDir)) {
			// E.g. gs://chromeos-gfx/traces/LunarG/mesa_trace_archive/10047/Sam3.trace
			// Trace ID = "lunarg_10047_Sam3"
			return true, "lunarg_" + parentDir + "_" + filename
		}

		// E.g. gs://chromeos-gfx/traces/LunarG/Lightroominteriorday.trace.bz2
		// Trace ID = "lunarg_Lightroominteriorday"
		return true, "lunarg_" + filename
	}

	return false, ""
}

func genMiscTraceID(gsPath string) (bool, string) {
	// E.g. gs://chromeos-gfx/traces/linux/10011_hl2_linux.trace.bz2
	// Trace ID = "misc_linux_10011_hl2_linux"
	// E.g. gs://chromeos-gfx/traces/jbates/steam-730-counter_strike_global_offensive-20200430_095700.trace
	// Trace ID = "misc_jbates_steam-730-counter_strike_global_offensive-20200430_095700"
	if ok, _ := regexp.MatchString(`(?i:^gs://chromeos-gfx/traces/)`, gsPath); ok {
		dirPath, filename := extractDirPathAndFilename(gsPath)
		parentDir := filepath.Base(dirPath)
		return true, "misc_" + parentDir + "_" + filename
	}

	return false, ""
}

// GenTraceIDFromGoogleStoragePath constructs and returns a unique trace ID for
// the given Google-Storage file path. For file paths that it does not know how
// to handle it returns the empty string.
func GenTraceIDFromGoogleStoragePath(gsPath string) string {
	// Note: the order in which these functions are called matters.
	if ok, traceID := genTVCTraceID(gsPath); ok {
		return traceID
	}
	if ok, traceID := genUncuratedTVCTraceID(gsPath); ok {
		return traceID
	}
	if ok, traceID := genUncuratedTVCSonaTraceID(gsPath); ok {
		return traceID
	}
	if ok, traceID := genLunarGTraceID(gsPath); ok {
		return traceID
	}
	if ok, traceID := genMiscTraceID(gsPath); ok {
		return traceID
	}

	return ""
}
