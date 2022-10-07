#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Records the software configuration on a device."""

from __future__ import print_function

import argparse
import datetime
import os
import re
import subprocess

from chromiumos.config.api.test.results.v1 import software_config_pb2
import results_database


def init_argparse():
    """Creates argument parser.

    Returns:
        An ArgumentParser.
    """
    parser = argparse.ArgumentParser(
        usage='%(prog)s [OPTION] [TRACE]...',
        description='Generate software configuration protobuf',
    )
    parser.add_argument('--gentoo_pkg_db', default='/var/db/pkg',
                        help='Gentoo package database')
    parser.add_argument('--id', '-i',
                        help='SoftwareConfigId to assign')
    parser.add_argument('--load', '-l',
                        help='SoftwareConfig protobuf to load to merge')
    parser.add_argument('--output', '-o',
                        help='File to write output to')
    parser.add_argument('--parent', '-p',
                        help='SoftwareConfig protobuf or ID of parent')
    parser.add_argument('--skip_packages', action='store_false',
                        dest='packages',
                        help='skip recording of packages')
    parser.add_argument('--json',
                        action='store_true',
                        help='Force json output')
    return parser

def read_pkg_yaml(file):
    """Partially read a gentoo package license.yaml into a dictionary.

    Args:
        file: The filename of the file to read.

    Returns:
        A dictionary.
    """
    info = {}
    with open(file) as f:
        for line in f.readlines():
            line = line.rstrip()
            line = re.sub(r'!!python/unicode ', r'', line)
            m = re.search(r'^- !!python/tuple \[([a-z]+), (.*)\]$', line)
            if m:
                value = results_database.strip_quotes(m.group(2))
                if value != 'null':
                    info[m.group(1)] = value
    return info

def read_release_keyval_file(file):
    """Read a release keyval file into a dictionary.

    Args:
        file: The filename of the file to read.

    Returns:
        A dictionary.
    """
    info = {}

    try:
        with open(file) as f:
            for line in f.readlines():
                (key, value) = line.rstrip().split('=')
               # Strip double quoted values.
                value = results_database.strip_quotes(value)
                info[key] = value
    except (FileNotFoundError, ValueError):
        pass

    return info

def parse_pkg_yaml(package, d):
    """Parse package info originally from a license.yaml file.

    Args:
        package: Package protobuf to parse results into.
        d: Dictionary of key/values to parse.
    """
    package.name = '%s/%s' % (d['category'], d['name'])
    if 'revision' in d:
        package.version = '%s-r%s' % (d['version'], d['revision'])
    else:
        package.version = d['version']

def parse_lsb_release(config, d):
    """Parse values originally from an /etc/lsb-release file.

    Args:
        config: SoftwareConfig protobuf to store results in.
        d: Dictionary of key/values to parse.
    """
    m = config.chromeos
    results_database.tryset(m, 'board', d, 'CHROMEOS_RELEASE_BOARD')
    results_database.tryset(m, 'branch_number', d,
                            'CHROMEOS_RELEASE_BRANCH_NUMBER', int)
    results_database.tryset(m, 'builder_path', d,
                            'CHROMEOS_RELEASE_BUILDER_PATH')
    results_database.tryset(m, 'build_number', d,
                            'CHROMEOS_RELEASE_BUILD_NUMBER', int)
    results_database.tryset(m, 'build_type', d, 'CHROMEOS_RELEASE_BUILD_TYPE')
    results_database.tryset(m, 'chrome_milestone', d,
                            'CHROMEOS_RELEASE_CHROME_MILESTONE', int)
    results_database.tryset(m, 'description', d, 'CHROMEOS_RELEASE_DESCRIPTION')
    results_database.tryset(m, 'keyset', d, 'CHROMEOS_RELEASE_KEYSET')
    results_database.tryset(m, 'name', d, 'CHROMEOS_RELEASE_NAME')
    results_database.tryset(m, 'patch_number', d,
                            'CHROMEOS_RELEASE_PATCH_NUMBER')
    results_database.tryset(m, 'track', d, 'CHROMEOS_RELEASE_TRACK')
    results_database.tryset(m, 'version', d, 'CHROMEOS_RELEASE_VERSION')

def parse_os_release(config, d):
    """Parse values originally from an /etc/os-release file.

    Args:
        config: SoftwareConfig protobuf to store results in.
        d: Dictionary of key/values to parse.
    """
    m = config.os
    results_database.tryset(m, 'build_id', d, 'BUILD_ID')
    results_database.tryset(m, 'codename', d, 'VERSION_CODENAME')
    results_database.tryset(m, 'id', d, 'ID')
    results_database.tryset(m, 'name', d, 'NAME')
    results_database.tryset(m, 'pretty_name', d, 'PRETTY_NAME')
    results_database.tryset(m, 'version_id', d, 'VERSION_ID')
    results_database.tryset(m, 'version', d, 'VERSION')

def parse_bios_info(config, d):
    """Parse values originally from a /var/log/bios_info.txt file

    Args:
        config: SoftwareConfig protobuf to store results in.
        d: Dictionary of key/values to parse.
    """
    results_database.tryset(config, 'bios_version', d, 'fwid')

def parse_ec_info(config, d):
    """Parse values originally from a /var/log/ec_info.txt file

    Args:
        config: SoftwareConfig protobuf to store results in.
        d: Dictionary of key/values to parse.
    """
    results_database.tryset(config, 'ec_version', d, 'fw_version')

def find_arch_packages(config):
    """Determine list of Arch system packages.

    Args:
        config: SoftwareConfig protobuf to store results in.
    """
    try:
        output = subprocess.check_output([
            'pacman',
            '-Q',
        ])
        for p in output.splitlines():
            package = config.packages.add()
            (package.name, package.version) = p.rstrip().split()
    except FileNotFoundError:
        pass

def find_debian_packages(config):
    """Determine list of Debian system packages.

    Args:
        config: SoftwareConfig protobuf to store results in.
    """
    try:
        output = subprocess.check_output([
            'dpkg-query',
            '--show',
            '--showformat=${binary:Package} ${Version}\n'
        ])
        for p in output.splitlines():
            package = config.packages.add()
            (package.name, package.version) = p.rstrip().split()
    except FileNotFoundError:
        pass

def find_gentoo_packages(config, base):
    """Determine list of Gentoo system packages.

    Args:
        config: SoftwareConfig protobuf to store results in.
        base: Path to gentoo package databse.
    """
    if os.path.exists(base):
        for c in os.listdir(base):
            for p in os.listdir(os.path.join(base, c)):
                file = os.path.join(base, c, p, 'license.yaml')
                if os.path.exists(file):
                    parse_pkg_yaml(config.packages.add(), read_pkg_yaml(file))

def main():
    """Main function."""
    args = init_argparse().parse_args()

    config = software_config_pb2.SoftwareConfig()
    # pylint: disable=no-member
    if args.load:
        results_database.read_pb(config, args.load)
        # Allow id to be override if provided.
        if args.id:
            # pylint: disable=no-member
            config.id.value = args.id
    else:
        config.id.value = args.id if args.id else results_database.generate_id()
    config.create_time.FromDatetime(datetime.datetime.now())

    # Read a parent protobuf or just treat it as an id.
    if args.parent:
        if (os.path.exists(args.parent) and
            re.search(r'\.(json|pb)$', args.parent)):
            parent = software_config_pb2.SoftwareConfig()
            results_database.read_pb(parent, args.parent)
            config.parent.value = parent.id.value
        else:
            config.parent.value = args.parent

    # Only fill in information if an existing protobuf wasn't loaded.
    if not args.load:
        if args.packages:
            find_arch_packages(config)
            find_debian_packages(config)
            find_gentoo_packages(config, args.gentoo_pkg_db)
        config.kernel_release = results_database.get_cmd_output(['uname', '-r'])
        config.kernel_version = results_database.get_cmd_output(['uname', '-v'])
        parse_lsb_release(config, read_release_keyval_file('/etc/lsb-release'))
        parse_os_release(config, read_release_keyval_file('/etc/os-release'))
        parse_bios_info(config, results_database.read_bios_keyval_file(
            '/var/log/bios_info.txt'))
        parse_ec_info(config, results_database.read_bios_keyval_file(
            '/var/log/ec_info.txt'))

    results_database.output_pb(config, args.output, force_json=args.json)

main()
