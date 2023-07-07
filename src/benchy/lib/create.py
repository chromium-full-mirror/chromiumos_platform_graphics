# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""benchy create subcommand."""

# pylint: disable=import-error
from chromiumos.config.api.test.benchy.v1 import plan_pb2

from lib import common


def add_subparser(subparsers):
    """Add command line parsing for the subcommand."""
    subparser = subparsers.add_parser('create',
                                      help='Create a plan.')
    subparser.add_argument('--output',
                           help='output plan filename')
    subparser.add_argument('--sample', action='store_true',
                           help='create a sample plan')
    subparser.add_argument('--tuning', action='store_true',
                           help='create a sample tuning plan')
    subparser.set_defaults(func=create)

def create(args):
    """Create a plan."""

    plan = plan_pb2.Plan()
    plan.name = 'Test'

    if args.sample or args.tuning:
        plan.devices.add(name='copano-evt-sku2-C123456',
                         host_name='192.168.1.100',
                         board='volteer')
        plan.devices.add(name='nipperkin-pvt-sku4-C345678',
                         host_name='192.168.1.101',
                         board='guybrush')
        plan.builds.add(name='R109-15185.0.0',
                        version='R109-15185.0.0')

        repeat_count = 5 if args.tuning else 1
        plan.workloads.add(name='borealis.TraceReplay.dota_2', repeat_count=repeat_count)
        plan.workloads.add(name='borealis.TraceReplay.portal_2', repeat_count=repeat_count)
        plan.workloads.add(name='borealis.TraceReplayProton.dota_2', repeat_count=repeat_count)
        plan.workloads.add(name='borealis.Benchmark.dota_2_gl', repeat_count=repeat_count)
        plan.workloads.add(name='borealis.Benchmark.dota_2_vulkan', repeat_count=repeat_count)
        plan.workloads.add(name='borealis.Benchmark.batman_knight', repeat_count=repeat_count)
        plan.workloads.add(name='borealis.Benchmark.csgo_gl', repeat_count=repeat_count)
        plan.workloads.add(name='borealis.Benchmark.csgo_vulkan', repeat_count=repeat_count)
        plan.workloads.add(name='borealis.Benchmark.civilization_vi', repeat_count=repeat_count)
        plan.workloads.add(name='borealis.Benchmark.cstrike_gl', repeat_count=repeat_count)
        plan.parameters.add(
            name='host_swappiness',
            values=['10', '15'],
            command_line='echo %(parameter_value)s > /proc/sys/vm/swappiness',
            execution_mode=plan_pb2.Parameter.ExecutionMode.EXECUTION_DUT)
        # Currently, we support a single parameter change for guest,
        # can extend it when necessary.
        plan.parameters.add(
            name='guest_swappiness',
            values=['20', '10'],
            command_line=
                'borealis.Benchmark.guestTuningCmd="sysctl vm.swappiness=%(parameter_value)s"',
            execution_mode=plan_pb2.Parameter.ExecutionMode.EXECUTION_TAST_VARIABLE)
        plan.parameters.add(
            name='abc',
            values=['a', 'b'],
            command_line='echo set abc %(parameter_name)s %(parameter_value)s',
            execution_mode=plan_pb2.Parameter.ExecutionMode.EXECUTION_LOCAL)

    common.write_plan(plan, args.output)
