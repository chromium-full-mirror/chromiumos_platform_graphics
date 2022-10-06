# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""benchy create subcommand."""

# pylint: disable=import-error
from chromiumos.config.api.test.benchy.v1 import plan_pb2

import lib.common as common


def add_subparser(subparsers):
    """Add command line parsing for the subcommand."""
    subparser = subparsers.add_parser('create',
                                      help='Create a plan.')
    subparser.add_argument('--output',
                           help='output plan filename')
    subparser.add_argument('--sample', action='store_true',
                           help='create a sample plan')
    subparser.set_defaults(func=create)

def create(args):
    """Create a plan."""

    plan = plan_pb2.Plan()
    plan.name = 'Test'

    if args.sample:
        plan.devices.add(name='copano-evt-sku2-C123456',
                         host_name='192.168.1.100',
                         board='volteer')
        plan.devices.add(name='nipperkin-pvt-sku4-C345678',
                         host_name='192.168.1.101',
                         board='guybrush')
        plan.builds.add(name='R109-15185.0.0',
                        version='R109-15185.0.0')
        plan.workloads.add(name='borealis.TraceReplay.dota_2')
        plan.workloads.add(name='borealis.TraceReplay.portal_2')
        plan.workloads.add(name='borealis.TraceReplayProton.dota_2')
        plan.parameters.add(
            name='host_swappiness',
            values=['10', '15'],
            command_line='echo setting %(parameter_name)s %(parameter_value)s',
            local_execution=False)
        plan.parameters.add(
            name='abc',
            values=['a', 'b'],
            command_line='echo set abc %(parameter_name)s %(parameter_value)s',
            local_execution=False)

    common.write_plan(plan, args.output)
