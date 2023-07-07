# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""benchy build subcommand."""

# pylint: disable=import-error
from lib import common


def add_subparser(subparsers):
    """Add command line parsing for the subcommand."""
    subparser = subparsers.add_parser('build',
                                      help='Build manipulation.')
    subparser.add_argument('--input',
                           help='input plan filename')
    subparser.add_argument('--output',
                           help='output plan filename')

    build_subparsers = subparser.add_subparsers(dest='command', required=True)

    add_parser = build_subparsers.add_parser('add',
                                              help='Add a build')
    add_parser.add_argument('name',
                            help='name of build')
    add_parser.add_argument('--version',
                            help='version of build')
    add_parser.add_argument('--clear', action='store_true',
                            help='clear existing before adding')
    add_parser.set_defaults(func=add_func)

    remove_parser = build_subparsers.add_parser('remove',
                                                 help='Remove a build')
    remove_parser.add_argument('name',
                               help='name of build')
    remove_parser.set_defaults(func=remove_func)

    clear_parser = build_subparsers.add_parser('clear',
                                                help='Clear all build')
    clear_parser.set_defaults(func=clear_func)

    import_parser = build_subparsers.add_parser('import',
                                                 help='Import build from plan')
    import_parser.add_argument('source',
                               help='plan filename to copy from')
    import_parser.add_argument('--clear', action='store_true',
                               help='clear existing before importing')
    import_parser.set_defaults(func=import_func)

def add_func(args):
    """Add a build to a plan."""

    plan = common.read_plan(args.input)
    if args.clear:
        del plan.builds[:]
    plan.builds.add(name=args.name,
                    version=args.version if args.version else args.name)
    common.write_plan(plan, args.output)

def remove_func(args):
    """Remove a build from a plan."""

    plan = common.read_plan(args.input)
    builds = [b for b in plan.builds if b.name != args.name]
    del plan.builds[:]
    plan.builds.extend(builds)
    common.write_plan(plan, args.output)

def clear_func(args):
    """Clear all builds from a plan."""

    plan = common.read_plan(args.input)
    del plan.builds[:]
    common.write_plan(plan, args.output)

def import_func(args):
    """Import builds from plan."""

    plan = common.read_plan(args.input)

    source_plan = common.read_plan(args.source)
    if args.clear:
        del plan.builds[:]
    plan.builds.extend(source_plan.builds)

    common.write_plan(plan, args.output)
