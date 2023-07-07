# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""benchy aggregate subcommand."""

import collections
import csv
import fileinput
import json
import logging
import os


def add_subparser(subparsers):
    """Add command line parsing for the subcommand."""
    subparser = subparsers.add_parser('aggregate', help='aggregate results')
    subparser.add_argument('result_dir',
                           help='the folder the results are located')
    subparser.add_argument('--input',
                           help="""
                           The input should be resultlist*.json, when specified,
                           it will generate a report based on the input json;
                           when not specified, it will automatically find all
                           resultlist*.json files located under the result_dir
                           and generate a report based on all the resultslist*.json
                           that can be found.
                           """)
    subparser.add_argument('--output',
                           help="""
                           Specify the name of the output csv file.
                           when not specified, it will generate a summary
                           with name performance_summary.csv located in result_dir.
                           """)
    subparser.set_defaults(func=aggregate)


def parse_labels(labels):
    """Parse device, build, workload, parameter info from labels"""
    parameters = []
    for item in labels:
        if item['grouping'] == 'parameter':
            parameters.append(f"{item['name']}-{item['value']}")
        elif item['name'] == 'device':
            device = item['value']
        elif item['name'] == 'build':
            build = item['value']
        elif item['name'] == 'workload':
            workload = item['value']
    return device, build, workload, '_'.join(parameters)


def load_resultlist(file):
    """Load file into json"""
    lines = fileinput.input(file if file else '-')
    result_str = ''.join(lines)
    result_list = json.loads(result_str)
    return result_list


def aggregate(args):
    """Generate a csv report given the resultlis*.json."""

    output_file = args.output if args.output else os.path.join(
        args.result_dir, 'performance_summary.csv')

    # perfdict is used to aggregate results per device-build-workload-parameter.
    # It is of format:
    # (device, build, workload, parameters, metric_1): []
    # (device, build, workload, parameters, metric_2): []
    perfdict = collections.defaultdict(list)

    # Keep record of results from multiple devices.
    # when input is specified, only read the input json file;
    # when input is not specified, read all resultlist*.json file
    # in the result folder.
    result_lists = []
    perf_filenames = []
    if args.input:
        perf_filenames.append(args.input)
        result_lists.append(load_resultlist(args.input))
    else:
        for file in os.listdir(args.result_dir):
            if file.startswith('resultlist') and file.endswith('.json'):
                perf_filenames.append(os.path.join(args.result_dir, file))
                result_lists.append(
                    load_resultlist(os.path.join(args.result_dir, file)))

    for i, result_list in enumerate(result_lists):
        if not result_list:
            logging.warning("%s is empty", perf_filenames[i])
            continue
        for perf_entry in result_list['value']:
            device, build, workload, parameter = parse_labels(
                perf_entry['labels'])
            for metric in perf_entry['metrics']:
                perfdict[(device, build, workload, parameter,
                          metric['name'])].append(float(metric['value']))

    already_has_header = False
    with open(output_file, 'w', encoding='utf-8') as file:
        writer = csv.writer(file, delimiter=',')
        for k, v in perfdict.items():
            if not already_has_header:
                writer.writerow(
                    ['Device', 'Build', 'Workload', 'Parameter', 'Metric'] + [
                        'RepeatCount', 'Average_except_1st', 'Average', 'Min',
                        'Max', 'Sum'
                    ] + [f'Repeat{i}_val' for i in range(len(v))])
                already_has_header = True
            # Add metric average except the 1st run as the 1st run is usually
            # a warm-up of the machine, which can affect the measure of performance.
            average_except_1st = 'NaN' if len(v) < 2 else (sum(v) -
                                                           v[0]) / (len(v) - 1)
            average = 'NaN' if len(v) < 1 else sum(v) / len(v)
            writer.writerow(
                list(k) +
                [len(v), average_except_1st, average,
                 min(v),
                 max(v),
                 sum(v)] + v)

    logging.info('Report generated based on %s is %s', perf_filenames,
                 output_file)
