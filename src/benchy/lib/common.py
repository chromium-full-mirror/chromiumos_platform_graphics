# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Common utilities for benchy."""

from contextlib import nullcontext
import fileinput
import sys

# pylint: disable=import-error
from chromiumos.config.api.test.benchy.v1 import plan_pb2
from google.protobuf import json_format


# Format strings for log handlers
LOGGING_FORMAT = '%(asctime)s %(levelname)s [%(threadName)s] %(message)s'
LOGGING_DATE_FORMAT = '%H:%M:%S'


def read_plan(file):
    """Read in a plan file."""
    lines = fileinput.input(file if file else '-')
    plan_str = ''.join(lines)

    plan = plan_pb2.Plan()
    json_format.Parse(plan_str, plan)
    return plan

def write_plan(plan, file=None):
    """Write out a plan file."""
    plan_json = json_format.MessageToJson(plan)
    with open(file, 'w', encoding='utf-8') if file else nullcontext(sys.stdout) as f:
        f.write(plan_json)
