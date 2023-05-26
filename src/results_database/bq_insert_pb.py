#!/usr/bin/env python3
# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Inserts json or binary protobufs into a BigQuery table."""

from __future__ import print_function

import argparse
import importlib

# pylint: disable=import-error
from google.cloud import bigquery
from google.protobuf import json_format

import results_database

# The following messages are treated as lists, otherwise protobufs are
# treated as singletons.
LIST_MESSAGES = frozenset(['ResultList', 'TraceList'])

# The messages that are handled by this script, with a tuple of:
# - Full path to python object to instantiate
# - BigQuery table
RESULTS_PATH = 'chromiumos.config.api.test.results.v1'
GRAPHICS_PATH = 'chromiumos.config.api.test.results.graphics.v1'
MESSAGES = {
    'Machine': (RESULTS_PATH + '.machine_pb2.Machine', 'machines'),
    'ResultList': (RESULTS_PATH + '.result_pb2.ResultList', 'results'),
    'SoftwareConfig': (RESULTS_PATH + '.software_config_pb2.SoftwareConfig',
                       'software_configs'),
    'TraceList': (GRAPHICS_PATH + '.trace_pb2.TraceList', 'traces'),
}

def init_argparse():
    """Creates argument parser.

    Returns:
        An ArgumentParser.
    """
    parser = argparse.ArgumentParser(
        usage='%(prog)s [OPTION] [FILE]...',
        description='Post protobufs to BigQuery database',
    )
    parser.add_argument('--class_path',
                        help='Override full python class to instantiate')
    parser.add_argument('--dataset',
                        default='graphics',
                        help='Project dataset')
    parser.add_argument('--deduplicate', action='store_true',
                        help='Do not upload any existing protobufs')
    parser.add_argument('--dryrun', action='store_true',
                        help='Do a dry run without actually uploading results')
    parser.add_argument('--message',
                        default='ResultList',
                        choices=[*MESSAGES],
                        help='Protobuf message to instantiate')
    parser.add_argument('--project',
                        default='chromeos-graphics',
                        help='Google Cloud project')
    parser.add_argument('--table',
                        help='Override table to insert results into')
    parser.add_argument('--verbose', action='store_true',
                        help='Explain what is being done')
    parser.add_argument('files', nargs='*',
                        help='results protobufs')
    return parser

def main():
    """Main function."""
    args = init_argparse().parse_args()

    # Determine class path if not fully specified.
    class_path = (args.class_path if args.class_path
                                    else MESSAGES[args.message][0])
    (module_path, class_name) = class_path.rsplit('.', 1)
    if args.verbose:
        print(f'module: {module_path}')
        print(f'class: {class_name}')

    # Load the appropriate python protobuf module based on the specified
    # class path.
    module = importlib.import_module(module_path)
    class_type = getattr(module, class_name)

    # Read in all the protobufs.
    rows = []
    row_ids = []
    id_key = None
    for file in args.files:
        pb = class_type()
        results_database.read_pb(pb, file)

        pb_list = pb.value if class_name in LIST_MESSAGES else [pb]
        for p in pb_list:
            rows.append(json_format.MessageToDict(
                p,
                including_default_value_fields=True,
                preserving_proto_field_name=True))
            if hasattr(p, 'id'):
                row_ids.append(p.id.value)
                id_key = 'id.value'
            elif hasattr(p, 'name'):
                row_ids.append(p.name.value)
                id_key = 'name.value'
            else:
                row_ids.append(None)
                print(f'WARNING: message missing id/name: {p}')

    # BigQuery connection to the specified table.
    client = bigquery.Client(args.project)
    table_name = '.'.join([args.project, args.dataset,
                           args.table if args.table
                                      else MESSAGES[args.message][1]])
    if args.verbose:
        print(f'table: {table_name}')
    client.get_table(table_name)

    # If de-duplication is enabled, query for all ids and only insert new ones.
    if id_key and args.deduplicate:
        # pylint: disable=import-error
        ids = ','.join([f"'{id}'" for id in row_ids])
        query = (f'SELECT {id_key} FROM `{table_name}` WHERE {id_key} IN ({ids})')
        if args.verbose:
            print(f'dedup query: {query}')
        query_job = client.query(query)  # API request
        results = query_job.result()  # Waits for query to finish
        duplicates = []
        if results:
            duplicates = {row.value for row in results}
            print(f'skipping {len(duplicates)} duplicates:')
            for d in duplicates:
                print(f' {d}')

        # Filter duplicates if any where found.
        if duplicates:
            dedup_rows = []
            dedup_ids = []
            for row, row_id in zip(rows, row_ids):
                if id not in duplicates:
                    dedup_rows.append(row)
                    dedup_ids.append(row_id)
            rows = dedup_rows
            row_ids = dedup_ids

    # Actually insert the results.
    if rows:
        # TODO(davidriley): Split this into chunks if necessary.
        print(f'inserting {len(rows)} rows to {table_name}')
        if not args.dryrun:
            insert = client.insert_rows_json(table_name, rows)
            if insert:
                print(f'ERROR: could not insert rows: {insert}')
        else:
            print('dryrun: skipping inserts')
    else:
        print('no data to insert')

main()
