#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Generate a protobuf with information about a list of traces."""

# TODO(davidriley): This script needs to be adapted to handle TVC traces
# using the game report.

from __future__ import print_function

import argparse
import os
import re

from chromiumos.config.api.test.results.graphics.v1 import trace_pb2

import results_database

SOURCES = ['lunarg', 'tvcs', 'user']

def init_argparse():
    """Creates argument parser.

    Returns:
        An ArgumentParser.
    """
    parser = argparse.ArgumentParser(
        usage='%(prog)s [OPTION] [TRACE]...',
        description='Generate trace info',
    )
    parser.add_argument('--output', '-o',
                        help='File to write output to')
    parser.add_argument('--source',
                        default=SOURCES[0],
                        choices=SOURCES,
                        help='Source of traces (%s)' % ', '.join(SOURCES))
    parser.add_argument('traces', nargs='*',
                        help='Directories of traces')
    return parser

def find_traces(dirname):
    """Find all traces in a directory.

    Args:
        dirname: Path to directory

    Returns:
        List of all files named *.trace.
    """
    return [f for f in os.listdir(dirname) if re.match(r'.*\.trace', f)]

def parse_frame_range(field, file):
    """Parse a frame range file."""
    if os.path.exists(file):
        with open(file) as f:
            (field.start, field.end) = tuple(map(int,
                                                 f.read().rstrip().split('-')))

def parse_frame_list(field, file):
    """Parse a frame list file."""
    if os.path.exists(file):
        with open(file) as f:
            data = f.read().rstrip()
            if data:
                field.extend([int(f) for f in data.split(',')])

def main():
    """Main function."""
    args = init_argparse().parse_args()

    results = trace_pb2.TraceList()
    for trace_dir in args.traces:
        trace_id = os.path.basename(trace_dir)

        # Search the directory for .trace files and ensure there is only one.
        traces = find_traces(trace_dir)
        assert len(traces) == 1
        filename = traces[0]

        # Basic info about the trace.
        # pylint: disable=no-member
        trace = results.value.add()
        trace.id.value = trace_id
        trace.filename = filename
        trace.size = os.stat(os.path.join(trace_dir, filename)).st_size

        if args.source:
            trace.source = args.source

        # TODO(davdriley): Add application_id, frame_count.

        # The following files are all optional.
        # framerange identifies the frames after the game began to interact.
        parse_frame_range(trace.frame_range,
                          os.path.join(trace_dir, 'framerange'))
        # keyframes are the ones worth checking for pixel correctness.
        parse_frame_list(trace.key_frames, os.path.join(trace_dir, 'keyframes'))
        # loopframes are the ones worth looping on to gauge performance.
        parse_frame_list(trace.loop_frames,
                         os.path.join(trace_dir, 'loopframes'))

    results_database.output_pb(results, args.output)

main()
