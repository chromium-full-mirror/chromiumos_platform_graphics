#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Module for Chrome OS Graphics results database."""

from __future__ import print_function

import datetime
import os
import re
import subprocess
import sys
import uuid

from google.protobuf import json_format


def error(*args, **kwargs):
    """Print an error message."""
    print(*args, file=sys.stderr, **kwargs)

def generate_id():
    """Generates a random id.

    Returns:
        A string.
    """
    return str(uuid.uuid4())

def get_cmd_output(cmd):
    """Read output from a command.

    Args:
        cmd: Command to run.

    Returns:
        A string with stdout.
    """
    return subprocess.check_output(cmd).rstrip().decode('utf-8')

def is_json(filename):
    """Determines if a filename is a JSON file.

    Args:
        filename: The filnemae to consider.

    Returns:
        A boolean that is True if so.
    """
    return re.search(r'\.json$', filename)

def output_pb(pb, filename, force_json=False):
    """Output protobuf as json, protobuf or to stdout.

    Args:
        pb: Protobuf to output>
        filename: File to write to in either json or binary format.
            If not specified, print to stdout.
        force_json: Always generate json.
    """
    if force_json or filename:
        if force_json or is_json(filename):
            j = json_format.MessageToJson(pb) + '\n'
            if filename:
                with open(filename, 'w') as f:
                    f.write(j)
            else:
                print(j)
        else:
            with open(filename, 'wb') as f:
                f.write(pb.SerializeToString(deterministic=True))
    else:
        print(pb)

def parse_date(s):
    """Parse s as a date.

    Args:
        s: A string in one of the following formats:
           now (current time)
           STRING=FORMAT (strptime format)
           YYYYMMDD-HHMMSS
           UTCTIMESTAMP
           YYYY-MM-DD[*HH[:MM[:SS[.fff[fff]]]][+HH:MM[:SS[.ffffff]]]] (iso)
           Python locale appropriate format (%c)

    Returns:
        A datetime or None if nothing can be parsed.
    """
    # now
    if s.lower() == 'now':
        return datetime.datetime.now()

    # STRING=FORMAT
    m = re.match(r'(?P<date_string>.*)=(?P<format>.*)$', s)
    if m:
        return datetime.datetime.strptime(m.group(1), m.group(2))

    # YYYYMMDD-HHMMSS
    m = re.match(r'\d{8}-[0-2]\d{5}$', s)
    if m:
        return datetime.datetime.strptime(s, '%Y%m%d-%H%M%S')

    # UTC timestamp: 1588891489
    m = re.match(r'\d{1,0}$', s)
    if m:
        return datetime.datetime.utcfromtimestamp(int(s))

    # isoformat
    try:
        return datetime.datetime.fromisoformat(s)
    except ValueError:
        pass

    # Locale appropriate.
    try:
        return datetime.datetime.strptime(s, '%c')
    except ValueError:
        pass

def read_bios_keyval_file(file):
    """Read a bios log keyval file into a dictionary.

    Args:
        file: Filename to read.

    Returns:
        A dictionary.
    """
    info = {}
    if os.path.exists(file):
        with open(file) as f:
            for line in f.readlines():
                line = line.rstrip()
                m = re.search(r'^(\S+)\s+(?:[|=])\s+([^#]+)', line)
                if m:
                    info[m.group(1)] = m.group(2).rstrip()
    return info


def read_pb(pb, filename):
    """Reads a protobuf in either JSON or binary format.

    Args:
        pb: A protobuf Message to read into.
        filename: The filename of the protobuf.
    """
    if is_json(filename):
        with open(filename) as f:
            json_format.Parse(f.read(), pb)
    else:
        with open(filename, 'rb') as f:
            pb.ParseFromString(f.read())

def strip_quotes(value):
    """Strip matched quotes or double quotes from a string.

    Args:
        value: The string to strip quotes from.

    Returns:
        A string.
    """
    value = re.sub(r'^"(.*)"$', r'\1', value)
    value = re.sub(r"^'(.*)'$", r'\1', value)
    return value

def tryset(p, field, d, dict_key, conv=str):
    """Try and set a protobuf value from a dictionary.

    Try and set value d[dict_key] into protobuf p's field if it exists,
    converting the value with conv.

    Args:
        p: Protobuf to assign into.
        field: Field of protobuf to assign.
        d: Dictionary to read from.
        dict_key: Key within dictionary.
        conv: conversion function.
    """
    if d.get(dict_key):
        p.__setattr__(field, conv(d[dict_key]))
