# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""benchy workload subcommand."""

# pylint: disable=import-error
from lib import common


def add_subparser(subparsers):
    """Add command line parsing for the subcommand."""
    subparser = subparsers.add_parser('workload',
                                      help='Workload manipulation.')
    subparser.add_argument('--input',
                           help='input plan filename')
    subparser.add_argument('--output',
                           help='output plan filename')

    workload_subparsers = subparser.add_subparsers(dest='command',
                                                   required=True)

    add_parser = workload_subparsers.add_parser('add',
                                              help='Add a workload')
    add_parser.add_argument('name',
                            help='name of workload')
    add_parser.add_argument('--repeat',
                            type=int,
                            default=1,
                            help='repeat count for this workload')
    add_parser.add_argument('--clear', action='store_true',
                            help='clear existing before adding')
    add_parser.set_defaults(func=add_func)

    remove_parser = workload_subparsers.add_parser('remove',
                                                 help='Remove a workload')
    remove_parser.add_argument('name',
                               help='name of workload')
    remove_parser.set_defaults(func=remove_func)

    clear_parser = workload_subparsers.add_parser('clear',
                                                help='Clear all workload')
    clear_parser.set_defaults(func=clear_func)

    import_parser = workload_subparsers.add_parser(
        'import',
        help='Import workload from plan')
    import_parser.add_argument('source',
                               help='plan filename to copy from')
    import_parser.add_argument('--clear', action='store_true',
                               help='clear existing before importing')
    import_parser.set_defaults(func=import_func)

def add_func(args):
    """Add a workload to a plan."""

    plan = common.read_plan(args.input)
    if args.clear:
        del plan.workloads[:]
    plan.workloads.add(name=args.name, repeat_count=args.repeat)
    common.write_plan(plan, args.output)

def remove_func(args):
    """Remove a workloads from a plan."""

    plan = common.read_plan(args.input)
    workloads = [w for w in plan.workloads if w.name != args.name]
    del plan.workloads[:]
    plan.workloads.extend(workloads)
    common.write_plan(plan, args.output)

def clear_func(args):
    """Clear all workloads from a plan."""

    plan = common.read_plan(args.input)
    del plan.workloads[:]
    common.write_plan(plan, args.output)

def import_func(args):
    """Import workloads from plan."""

    plan = common.read_plan(args.input)

    source_plan = common.read_plan(args.source)
    if args.clear:
        del plan.workloads[:]
    plan.workloads.extend(source_plan.workloads)

    common.write_plan(plan, args.output)
