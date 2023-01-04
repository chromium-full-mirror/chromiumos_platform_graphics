# -*- coding: utf-8 -*-

# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Hook to stop people from running `git cl`."""

import sys


USE_PYTHON3 = True

def CheckChangeOnUpload(_input_api, _output_api):
    """Ensure 'repo upload' is used in lieu of 'git cl upload'"""
    print('ERROR: CrOS repos use `repo upload`, not `git cl upload`.',
          file=sys.stderr)
    sys.exit(1)
