#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Records information about the machine."""

from __future__ import print_function

import argparse
import datetime
import os

from chromiumos.config.api.test.results.v1 import machine_pb2
import results_database


def init_argparse():
    """Creates argument parser.

    Returns:
        An ArgumentParser.
    """
    parser = argparse.ArgumentParser(
        usage='%(prog)s [OPTION]...',
        description='Generate machine info protobuf',
    )
    parser.add_argument('--name', '-n',
                        help='Machine name to assign')
    parser.add_argument('--output', '-o',
                        help='File to write output to')
    parser.add_argument('--owner',
                        help='Owner to assign')
    parser.add_argument('--json',
                        action='store_true',
                        help='Force json output')
    return parser

def parse_bios_info(config, d):
    """Parse values originally from a /var/log/bios_info.txt file."""
    results_database.tryset(config, 'hwid', d, 'hwid')

def main():
    """Main function."""
    args = init_argparse().parse_args()

    config = machine_pb2.Machine()
    # pylint: disable=no-member
    config.name.value = (args.name
                         if args.name else results_database.generate_id())
    config.create_time.FromDatetime(datetime.datetime.now())
    config.owner = args.owner if args.owner else os.environ['USER']
    parse_bios_info(config, results_database.read_bios_keyval_file(
            '/var/log/bios_info.txt'))

    results_database.output_pb(config, args.output, force_json=args.json)

main()
