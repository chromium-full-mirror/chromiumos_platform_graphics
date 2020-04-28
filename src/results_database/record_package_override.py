#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Generatea a package override protobuf.

Protobuf describes a package being overridden from the standard system
software using a source repo for determination.
"""

from __future__ import print_function

import argparse
import datetime
import os
import re
import sys

from chromiumos.config.api.test.results.v1 import package_pb2
import results_database
from results_database import error

def init_argparse():
    """Creates argument parser.

    Returns:
        An ArgumentParser.
    """
    parser = argparse.ArgumentParser(
        usage='%(prog)s [OPTION] DIR...',
        description='Generate package override',
    )
    parser.add_argument('--name',
                        help='Name of package')
    parser.add_argument('--git_dir',
                        help='Path to git repo')
    parser.add_argument('--git_hash',
                        help='Git hash to assign')
    parser.add_argument('--version',
                        help='Version to assign')
    parser.add_argument('--noautoversion', action='store_false',
                        dest='autoversion',
                        help='Disable auto version detection.')
    parser.add_argument('--output', '-o',
                        help='File to write output to')
    parser.add_argument('dir', nargs='?',
                        help='Path to git work tree including repo')
    return parser

def find_mesa_version(path):
    """Find the version from a mesa repo.

    Args:
        path: The path to the repo.

    Returns:
        A string of the version, or None if it cannot be determined.
    """
    file = os.path.join(path, 'VERSION')
    if os.path.exists(file):
        with open(file) as f:
            return f.readline().rstrip()
    return None

def find_apitrace_version(path):
    """Find the version from an apitrace repo.

    Args:
        path: The path to the repo.

    Returns:
        A string of the version, or None if it cannot be determined.
    """
    # git describe --tags doesn't work if we're merging in to a local tree.
    file = os.path.join(path, 'CMakeLists.txt')
    if os.path.exists(file):
        with open(file) as f:
            d = {}
            for l in f.readlines():
                m = re.match(
                    r'^set \(CPACK_PACKAGE_VERSION_(MAJOR|MINOR) "(\d+)"\)$',
                    l)
                if m:
                    d[m.group(1)] = m.group(2)
            if 'MAJOR' in d and 'MINOR' in d:
                return '.'.join([d['MAJOR'], d['MINOR']])
    return None

def main():
    """Main function."""
    args = init_argparse().parse_args()

    package = package_pb2.Package()

    # Figure out a git dir.
    if args.git_dir:
        git_dir = args.git_dir
    elif args.dir:
        path = os.path.join(args.dir, '.git')
        if os.path.exists(path):
            git_dir = path
    else:
        git_dir = None

    # Figure out the name.
    if args.name:
        package.name = args.name
    elif args.dir:
        if not os.path.exists(args.dir):
            error('ERROR: cannot find dir %s' % args.dir)
            sys.exit(1)
        package.name = os.path.split(os.path.realpath(args.dir))[-1]
    else:
        error('ERROR: name is not specified and cannot be determined')
        sys.exit(1)

    # Get information from git if possible.
    if git_dir:
        if not os.path.exists(git_dir):
            error('ERROR: cannot find git_dir %s' % git_dir)
            sys.exit(1)
        git_cmd = ['git', '--git-dir', git_dir, '--work-tree', args.dir]

        # Current git hash.
        package.git_hash = (args.git_hash
                            if args.git_hash
                            else results_database.get_cmd_output(
                                git_cmd + ['rev-parse', 'HEAD']))

        # Current branch.
        branch = results_database.get_cmd_output(
            git_cmd + ['rev-parse', '--abbrev-ref', 'HEAD'])
        if branch != 'HEAD':
            package.branch = branch

        # Last commit date.
        raw = results_database.get_cmd_output(
            git_cmd + ['log', '-1', '--format=%ad', '--date=unix'])
        dt = datetime.datetime.utcfromtimestamp(int(raw))
        # pylint: disable=no-member
        package.commit_date.FromDatetime(dt)

        # Is the repo clean?
        package.repo_dirty = len(results_database.get_cmd_output(
            git_cmd + ['status', '--porcelain']))

    if args.version:
        package.version = args.version
    elif args.autoversion:
        # Try best to determine the version based on the contents of the repo.
        # pylint: disable=no-member
        if package.name == 'mesa':
            version = find_mesa_version(args.dir)
        elif package.name == 'apitrace':
            version = find_apitrace_version(args.dir)
        elif git_dir:
            version = results_database.get_cmd_output(
                git_cmd + ['describe', '--tags'])

        # Add the version and record if it is autogenerated.  The version should
        # roughly indicate the pedigree of the code, but label it clearly so as
        # not to confuse it with an actual labeled version.
        if version:
            package.version = version + ' (autogenerated)'

    results_database.output_pb(package, args.output)

main()
