// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ffmpeg_md5sum wraps ffmpeg to reoutput MD5 sums from decoding, one per line.

package main

import (
	"bytes"
	"context"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"strings"
)

const ffmpegPath = "/usr/local/bin/ffmpeg"

// multiFlags allow an alternative to passing space-separated flags, so that
// --f v1 v2 can instead be passed as --f v1 --f v2.
// The Golang flag package cannot parse space-sparated flags without enclosing
// the values in quotation marks. However, the platform decoding tests nest
// binary calls and flags, complicating quotations.
// This alternative provides a way to unnest/unfold the flags.
type multiFlags []string

// String implements the flag interface.
func (m *multiFlags) String() string {
	return strings.Join(*m, " ")
}

// Set implements the flag interface.
func (m *multiFlags) Set(value string) error {
	*m = append(*m, value)
	return nil
}

// parseHashes parses hashes from ffmpeg output.
func parseHashes(stdout string) ([]string, error) {
	lines := strings.Split(stdout, "\n")
	hashes := make([]string, 0, len(lines))

	for _, l := range lines {
		l = strings.TrimSpace(l)
		if len(l) == 0 || strings.HasPrefix(l, "#") {
			continue
		}

		tok := strings.Split(l, ",")
		hash := strings.TrimSpace(tok[len(tok)-1])
		if len(hash) != 32 {
			return nil, fmt.Errorf("expected MD5 sum, got %v", hash)
		}
		hashes = append(hashes, tok[len(tok)-1])
	}
	return hashes, nil
}

// exitWithError dumps all output to the appropriate streams and exits with errcode 1.
func exitWithError(err error, stdout, stderr string) {
	if err != nil {
		fmt.Fprintln(os.Stderr, err.Error())
	}
	if stdout != "" {
		fmt.Fprintln(os.Stdout, stdout)
	}
	if stderr != "" {
		fmt.Fprintln(os.Stderr, stderr)
	}
	os.Exit(1)
}

func main() {
	flag.Usage = func() {
		fmt.Fprintf(os.Stderr,
			"ffmpeg_md5sum is a wrapper around ffmpeg that generates MD5 sums from "+
				"decoding, and processes that (annotated) output to output instead one "+
				"hash per line.\n\n"+
				"Flags:\n"+
				"\tvideo: Required. Path to video to decode.\n"+
				"\toutput: Optional. Path to write md5 checksum data. Defaults to stdout.\n"+
				"\tflags: Optional. Additional flags to ffmpeg. Pass space-separated\n"+
				"\t       flags individually, i.e. `--flags -hwaccel --flags vaapi`\n"+
				"\t       to pass `-hwaccel vaapi` to ffmpeg.\n")
	}

	var flags multiFlags
	var video string
	var outputPath string
	flag.Var(&flags, "flags", "additional flags to ffmpeg: for space-separated flags, pass each individually with --flags")
	flag.StringVar(&video, "video", "", "path to video to decode")
	flag.StringVar(&outputPath, "output", "", "path to md5 checksum log")
	flag.Parse()

	args := append(flags, []string{
		"-hide_banner",
		"-loglevel", "verbose",
		"-i", video,
		"-vf", "format=pix_fmts=yuv420p",
		"-autoscale", "0",
		"-f", "framemd5", "-",
	}...)
	ctx := context.Background()
	cmd := exec.CommandContext(ctx, ffmpegPath, args...)

	fmt.Printf("Running `%s %s`\n", ffmpegPath, strings.Join(args, " "))
	var outbuf, errbuf bytes.Buffer
	cmd.Stdout, cmd.Stderr = &outbuf, &errbuf
	err := cmd.Run()
	stdout := strings.TrimSpace(outbuf.String())
	stderr := strings.TrimSpace(errbuf.String())
	if err != nil {
		exitWithError(err, stdout, stderr)
	}

	var hashes []string
	if hashes, err = parseHashes(stdout); err != nil {
		exitWithError(err, stdout, "")
	}

	var f = os.Stdout
	if len(outputPath) > 0 {
		if f, err = os.Create(outputPath); err != nil {
			exitWithError(err, stdout, "")
		}
	}
	f.WriteString(strings.Join(hashes, "\n"))
	f.Close()

}
