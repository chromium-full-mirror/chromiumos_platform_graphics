# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""benchy device subcommand."""

# pylint: disable=import-error
from lib import common


def add_subparser(subparsers):
    """Add command line parsing for the subcommand."""
    subparser = subparsers.add_parser('device',
                                      help='Device manipulation.')
    subparser.add_argument('--input',
                           help='input plan filename')
    subparser.add_argument('--output',
                           help='output plan filename')

    device_subparsers = subparser.add_subparsers(dest='command', required=True)

    add_parser = device_subparsers.add_parser('add',
                                              help='Add a device')
    add_parser.add_argument('name',
                            help='name of device')
    add_parser.add_argument('--host',
                            help='hostname of device')
    add_parser.add_argument('--board',
                            help='board of device')
    add_parser.add_argument('--clear', action='store_true',
                            help='clear existing before adding')
    add_parser.set_defaults(func=add_func)

    remove_parser = device_subparsers.add_parser('remove',
                                                 help='Remove a device')
    remove_parser.add_argument('name',
                               help='name of device')
    remove_parser.set_defaults(func=remove_func)

    clear_parser = device_subparsers.add_parser('clear',
                                                help='Clear all devices')
    clear_parser.set_defaults(func=clear_func)

    import_parser = device_subparsers.add_parser('import',
                                                 help='Import devices from plan')
    import_parser.add_argument('source',
                               help='plan filename to copy from')
    import_parser.add_argument('--clear', action='store_true',
                               help='clear existing before importing')
    import_parser.set_defaults(func=import_func)

def add_func(args):
    """Add a device to a plan."""

    plan = common.read_plan(args.input)
    if args.clear:
        del plan.devices[:]
    plan.devices.add(name=args.name,
                     host_name=args.host if args.host else args.name,
                     board=args.board)
    common.write_plan(plan, args.output)

def remove_func(args):
    """Remove a device from a plan."""

    plan = common.read_plan(args.input)
    devices = [d for d in plan.devices if d.name != args.name]
    del plan.devices[:]
    plan.devices.extend(devices)
    common.write_plan(plan, args.output)

def clear_func(args):
    """Clear all devices from a plan."""

    plan = common.read_plan(args.input)
    del plan.devices[:]
    common.write_plan(plan, args.output)

def import_func(args):
    """Import devices from plan."""

    plan = common.read_plan(args.input)

    source_plan = common.read_plan(args.source)
    if args.clear:
        del plan.devices[:]
    plan.devices.extend(source_plan.devices)

    common.write_plan(plan, args.output)
