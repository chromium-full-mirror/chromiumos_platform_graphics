#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Summarize the results of an apitrace run from its output.

log_name="XXXXX-$(date +%Y%m%d-%H%M%S).txt"
glxinfo -B > "${log_name}"
apitrace replay XXXXX.trace >> "${log_name}"
summarize_apitraace_log.py "${log_name}"
"""

from __future__ import print_function

import argparse
import datetime
import os
import re

from chromiumos.config.api.test.results.v1 import machine_pb2
from chromiumos.config.api.test.results.v1 import result_pb2
from chromiumos.config.api.test.results.v1 import software_config_pb2
import results_database

EXECUTION_ENVS = frozenset([
    'unknown',
    'host',
    'termina',
    'crostini',
    'steam',
    'arc',
    'arcvm',
    'crouton',
    'crosvm',
])

GLXINFO_LABELS = frozenset([
    'vendor',
    'renderer',
    'core profile version',
    'core profile shading language version',
    'version',
    'shading language version',
    'ES profile version',
    'ES profile shading language version',
])

BENCHMARK_APITRACE = 'apitrace'

def init_argparse():
    """Creates argument parser.

    Returns:
        An ArgumentParser.
    """
    parser = argparse.ArgumentParser(
        usage='%(prog)s [OPTION] [FILE]...',
        description='Summarize apitrace results',
    )
    parser.add_argument('--execution_environment', '-e',
                        choices=EXECUTION_ENVS,
                        default='unknown',
                        help='environment of the run')
    parser.add_argument('--invocation_source',
                        help='Source of the test invocation')
    parser.add_argument('--machine', '-m',
                        help='Machine protobuf or ID')
    parser.add_argument('--output', '-o',
                        help='File to write output to')
    parser.add_argument('--package', '-p',
                        action='append',
                        help='Package override protobuf')
    parser.add_argument('--software', '-s',
                        help='SoftwareConfig protobuf or ID')
    parser.add_argument('--start_time',
                        help='Start time for the trace run')
    parser.add_argument('--trace',
                        help='Trace ID that was run')
    parser.add_argument('--username', '-u',
                        default=os.environ['USER'],
                        help='username to attribute tests to')
    parser.add_argument('files', nargs='*',
                        help='apitrace replay results')
    return parser

def interpret_replay_log_filename(result, file):
    """Parse a replay log filename with embedded date.

    Args:
        result: Result protobuf to store into.
        file: Filename of the form XXXXX-YYYYMMDD-HHMMSS where XXXXX is a
              LunarG trace name.
    """
    m = re.match(r'(?P<trace_name>.*)-(?P<start_time>2\d{7}-[0-2]\d{5}).txt$',
            file)
    if m:
        d = m.groupdict()
        if not result.HasField('trace'):
            result.trace.value = d['trace_name']
        if not result.HasField('start_time'):
            result.start_time.FromDatetime(
                datetime.datetime.strptime(d['start_time'], '%Y%m%d-%H%M%S'))

def expand_metrics(metrics):
    """Expand a dictionary of metrics into an array of dictionaries.

    Args:
        metrics: A dictionary with key/value pairs.

    Returns:
        An array of dictionaries of the form:
        { 'metric': KEY, 'value': VALUE }
    """
    return [{'metric': k, 'value': v} for k, v in metrics.items()]

def add_frame_rate(metric, frame_rate):
    """Add a metric for frame rate."

    Args:
        metric: Metric protobuf to modify.
        frame_rate: Frame rate in fps.
    """
    metric.name = 'frame_rate'
    metric.value = float(frame_rate)
    metric.units = 'fps'
    metric.larger_is_better = True

def add_frame_count(metric, frame_count):
    """Add a metric for frame count."

    Args:
        metric: Metric protobuf to modify.
        frame_count: Number of frames.
    """
    metric.name = 'frame_count'
    metric.value = int(frame_count)
    metric.units = 'frames'

def add_duration(metric, duration):
    """Add a metric for duration."

    Args:
        metric: Metric protobuf to modify.
        duration: Duration in seconds.
    """
    metric.name = 'duration'
    metric.value = float(duration)
    metric.units = 'seconds'

def add_label(labels, name, value, grouping=None):
    """Add a label to a Result.

    Args:
        labels: List of Label protobufs to append to.
        name: Name of label.
        value: Value of label.
        grouping: Optional grouping for the label.
    """
    label = result_pb2.Result.Label()
    label.name = name
    label.value = value
    if grouping:
        label.grouping = grouping
    labels.append(label)

def find_label(labels, name, grouping=None):
    """Search for a label with a given name and grouping.

    Args:
        labels: List of Label protobufs.
        name: Name of label fo find.
        grouping: Optional grouping for the label.

    Returns:
        label if found, None otherwise.
    """
    for l in labels:
        if (grouping is None or l.grouping == grouping) and l.name == name:
            return l.value
    return None

def process_replay_log(results, template, file):
    """Process the log of an apitrace replay and emit a list of a results.

    Args:
        results: List of Result protobufs to append to.
        template: Result protobuf template to use as a basis for every result.
        file: The file to read.
    """
    GLXINFO_RE = (r'OpenGL (?P<name>%s) string: (?P<value>.*)' %
                  '|'.join(GLXINFO_LABELS))

    with open(file) as f:
        labels = []
        for line in f.read().splitlines():
            m = re.match(r'(?P<name>CMD): +(?P<value>.*)', line)
            if m:
                add_label(labels, grouping=BENCHMARK_APITRACE, **m.groupdict())

            m = re.match(GLXINFO_RE, line)
            if m:
                add_label(labels, grouping='glxinfo', **m.groupdict())

            m = re.search((r'Rendered (?P<frames>\d+) frames in '
                           '(?P<seconds>[0-9.]+) secs, '
                           'average of (?P<fps>[0-9.]+) fps'), line)
            if m:
                d = m.groupdict()
                result = results.value.add()
                result.CopyFrom(template)
                # TODO(davidriley): Or use a uuid?
                result.id.value = '%s-%s' % (
                        result.invocation_source,
                        re.sub(r'\..*', r'', os.path.basename(file)))
                cmd = find_label(labels, 'CMD', grouping=BENCHMARK_APITRACE)
                if cmd:
                    result.command_line = cmd
                interpret_replay_log_filename(result, file)
                result.benchmark = BENCHMARK_APITRACE
                add_frame_rate(result.metrics.add(), d['fps'])
                add_frame_count(result.metrics.add(), d['frames'])
                add_duration(result.metrics.add(), d['seconds'])
                result.primary_metric_name = 'frame_rate'
                result.labels.extend(labels)
                if result.start_time:
                    result.end_time.FromDatetime(
                        result.start_time.ToDatetime() +
                        datetime.timedelta(seconds=float(d['seconds'])))
            # TODO(davidriley): Handle errors.

def main():
    """Main function."""
    args = init_argparse().parse_args()

    template = result_pb2.Result()

    # Trace ID.
    # pylint: disable=no-member
    if args.trace:
        template.id.value = args.trace

    # Start time.
    if args.start_time:
        dt = results_database.parse_date(args.start_time)
        if dt:
            template.start_time.FromDatetime(dt)

    # Machine protobuf or ID.
    if args.machine:
        if os.path.exists(args.machine):
            machine = machine_pb2.Machine()
            results_database.read_pb(machine, args.machine)
            template.machine.value = machine.name.value
        else:
            template.machine.value = args.machine

    # SoftwareConfig protobuf or ID.
    if args.software:
        if os.path.exists(args.software):
            config = software_config_pb2.SoftwareConfig()
            results_database.read_pb(config, args.software)
            template.software_config.value = config.id.value
        else:
            template.software_config.value = args.software

    # List of Package software override protobufs.
    if args.package:
        for p in args.package:
            package = template.overrides.packages.add()
            results_database.read_pb(package, p)

    # Autodetect source if not specified.
    if args.invocation_source:
        template.invocation_source = args.invocation_source
    else:
        if args.username:
            username = args.username
        else:
            username = os.environ['USER']
        template.invocation_source = 'user/' + username

    if args.execution_environment:
        template.execution_environment = (
            result_pb2.Result.ExecutionEnvironment.Value(
                args.execution_environment.upper()))

    # Process all the results.
    results = result_pb2.ResultList()
    for file in args.files:
        process_replay_log(results, template, file)

    results_database.output_pb(results, args.output)
    print('summarized %d traces' % len(results.value))

main()
