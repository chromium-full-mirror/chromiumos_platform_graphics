# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""benchy run subcommand."""

import concurrent.futures
import copy
import datetime
import itertools
import json
import logging
import os
import re
import subprocess
import sys

# pylint: disable=import-error
from chromiumos.config.api.test.results.v1 import machine_pb2
from chromiumos.config.api.test.results.v1 import result_pb2
from chromiumos.config.api.test.results.v1 import software_config_pb2
from google.protobuf import json_format

import lib.common as common


# This subcommand handles fully executing a plan of:
# devices
#  builds
#   parameter1 values
#    parameter2 values
#     ...
#      parameterN values
#       workloads

# Results are accumulated in a directory (managed by JobOutput), and with
# each stage of the descent, the JobInfo tracks the state and the ability
# to store tagged files.  Results are cached to allow runs to be resumed
# if aborted for some reason.

# The end result of the plan is the generation of results database json
# protobufs that are ready to be uploded to BigQuery.

# The list of artifacts that can be stored in a job.
EXECUTED_PLAN_TAG = ('executedplan', '.json')
MACHINE_INFO_TAG = ('machineinfo', '.json')
SOFTWARE_CONFIG_HOST_TAG = ('softwareconfighost', '.json')
SOFTWARE_CONFIG_GUEST_TAG = ('softwareconfigguest', '.json')
TAST_LOG_TAG = ('tastlog', '.txt')
TAST_RESULTS_TAG = ('tastresults', '.json')
RESULTS_DB_RESULT_TAG = ('result', '.json')
RESULT_LIST_TAG = ('resultlist', '.json')

# The tast test used to gather software config information.
COLLECT_INFO_TEST = 'borealis.CollectInfo'

# The list of artifacts that are recovered from tast tests.
RESULTS_CHART_JSON = 'results-chart.json'
HOST_SOFTWARE_CONFIG_JSON = 'host_software_config.json'
GUEST_SOFTWARE_CONFIG_JSON = 'guest_software_config.json'

def add_subparser(subparsers):
    """Add command line parsing for the subcommand."""
    subparser = subparsers.add_parser('run',
                                      help='Execute a plan.')
    subparser.add_argument('output',
                           help='directory to store job results in')
    subparser.add_argument('--input',
                           help='input plan filename')
    subparser.add_argument('--noparallel', dest='parallel', action='store_false',
                           help='do not test in parallel')
    subparser.set_defaults(func=run)

def perform_call(local, command_line, cmd_args, stdout=False):
    """Execute a local or remote shell command."""

    cmd = command_line % cmd_args
    if cmd:
        if local:
            local_call(cmd, stdout=stdout)
        else:
            remote_call(cmd_args['device_hostname'], cmd, stdout=stdout)

def local_call(cmd, stdout=False):
    """Execute a local shell command."""
    if stdout:
        return subprocess.check_output(cmd,
                                       shell=True).decode(sys.stdout.encoding)
    subprocess.check_call(cmd, shell=True)
    return None

def remote_call(dut, cmd, stdout=False):
    """Execute a remote shell command."""
    ssh_cmd = ['ssh', '-q', dut, cmd]
    if stdout:
        return subprocess.check_output(ssh_cmd).decode(sys.stdout.encoding)
    subprocess.check_call(ssh_cmd)
    return None

def get_device_machine_info(cmd_args):
    """Retrieves the machine info json from the device."""
    cmd = ('record_machine_info.py '
            '--name %(device_name)s '
            '--owner %(device_owner)s --json') % cmd_args
    machine_info = remote_call(cmd_args['device_hostname'], cmd, stdout=True)
    return machine_info

def get_device_software_configs(job, job_info):
    """Gets the host and guest software config jsons from the device."""

    # Software config collection is handled via a tast test.
    subjob_info = job_info.copy()
    subjob_info.add_name(JobInfo.EXECUTION, COLLECT_INFO_TEST)
    cmd_args = job_info.make_args()
    runner = TastRunner(job, subjob_info, cmd_args['device_hostname'],
                        COLLECT_INFO_TEST)
    tast_result = runner.run()

    # Results need to be merged such that the guest points to the host
    # as it's parent.
    software_config_host = tast_result.get_file(HOST_SOFTWARE_CONFIG_JSON)
    software_config_guest = tast_result.get_file(GUEST_SOFTWARE_CONFIG_JSON)
    if software_config_host and software_config_guest:
        return (software_config_host,
                merge_software_config(software_config_host,
                                        software_config_guest))
    return None

def merge_software_config(host_json, guest_json):
    """Merges the host software config into the guest software config."""

    # Parse the protobufs.
    host = software_config_pb2.SoftwareConfig()
    json_format.Parse(host_json, host)
    guest = software_config_pb2.SoftwareConfig()
    json_format.Parse(guest_json, guest)

    # Insert the host id as the guest's parent.
    guest.parent.value = host.id.value
    return json_format.MessageToJson(guest)

def get_device_version(cmd_args):
    """Gets the current version from a device."""
    cmd = 'cat /etc/lsb-release'
    lsb_release = remote_call(cmd_args['device_hostname'], cmd, stdout=True)
    version = ''
    for line in lsb_release.splitlines():
        if line.startswith('CHROMEOS_RELEASE_BUILDER_PATH'):
            version = line.rstrip().split('/', 2)[1]
    return version

def get_owner():
    """Get the owner for results database entries."""
    return os.environ['USER']

def make_run_label(dirname=None, start_time=None):
    """Get a unique label for the run."""
    if not start_time:
        start_time = datetime.datetime.now()
    start = start_time.strftime('%Y%M%d-%H%M%S')
    return f'{get_owner()}-{dirname}-{start}'

def find_tast_results_dir(tast_log):
    """Parse tast output to find the results dir."""
    for line in tast_log.splitlines():
        # 2022-10-06T15:47:27.372627Z Results saved to /tmp/tast/results/20221006-084414
        m = re.search(r'Results saved to (.*)', line)
        if m:
            return m.group(1)
    return None

class Field:
    """Parameter field for a particular job."""
    def __init__(self, value, label, arg_name, path_component):
        self.value = value
        self.label = label
        self.arg_name = arg_name
        self.path_component = path_component

    def __repr__(self):
        return str(self.__class__) + ": " + str(self.__dict__)

class JobInfo:
    """Tracks all the information identifying the job.

    JobInfos are constantly mutated as the plan is being executed and tracks
    the different configuration.  Each further level of traversal should be
    copy()'d instead of just mutated to allow the JobInfo to track the correct
    state when the lower level returns.

    Families represent the high level items that are iterated through
    (eg build, device, workloads).

    Names are used to uniquely identify the value for a given family.
    They are identified explicitly as part of the filenames in the cached
    output.

    Tags are used to record additional metadata (eg hostname, version) for
    a given family, and can be used when executing commands.

    Parameters are like their own individual family.
    """
    # Top level families.
    RUN = 'run'
    DEVICE = 'device'
    BUILD = 'build'
    WORKLOAD = 'workload'
    PARAMETER = 'parameter'
    EXECUTION = 'execution'

    def __init__(self, job):
        self.fields = []
        self.job = job

    def __repr__(self):
        return str(self.__class__) + ": " + str(self.__dict__)

    def copy(self):
        """Copy to allow further descent into the job."""
        return copy.deepcopy(self)

    def add_field(self, field):
        """Internal method to add a pre-constructed Field object."""
        self.fields.append(field)

    def add_name(self, family, value, path_component=True):
        """Add a top-level keyed value.

        Args:
            family: Family being entered (eg build, device)
            value: Name of entity being entered
            path_component: Should the value be included in any filenames.
        """
        # All the top-level values will be added as labels under 'benchy.'
        field = Field(value, ('benchy', family), f'{family}_name',
                      path_component)
        self.add_field(field)

    def add(self, family, tag, value):
        """Add a tag under a given family."""
        field = Field(value, None, f'{family}_{tag}', False)
        self.add_field(field)

    def set_parameter(self, name, value):
        """Set a parameter to a given value."""
        field = Field(name, None, 'parameter_name', True)
        self.add_field(field)
        # All the parameters will be added as labels under 'parameter.'
        field = Field(value, ('parameter', name), 'parameter_value', True)
        self.add_field(field)

    def make_result_labels(self):
        """Generate labels for the results database."""
        return {l.label: l.value for l in self.fields if l.label}

    def make_args(self):
        """Generate arguments for command line substitutions."""
        return {l.arg_name: l.value for l in self.fields if l.arg_name}

    def make_file_parts(self):
        """Generate the filename components for job artifacts."""
        return [l.value for l in self.fields if l.path_component]

    def make_id(self):
        """Generate an id for logging progress."""
        return ' '.join([l.value for l in self.fields if l.path_component])

class JobOutput:
    """Manage the intermediate and output data from a job."""
    def __init__(self, output_dir):
        self.output_dir = output_dir

    def __repr__(self):
        return str(self.__class__) + ": " + str(self.__dict__)

    def start(self):
        """Creates a new handle into the JobOutput for tracking iteration."""
        return JobInfo(self)

    def get_filename(self, job_info, tag):
        """Determines filename for tag given job_info."""
        identifier = '-'.join(job_info.make_file_parts())
        if identifier:
            file = f'{tag[0]}-{identifier}{tag[1]}'
        else:
            file = f'{tag[0]}{tag[1]}'
        return os.path.join(self.output_dir, file)

    def exists(self, job_info, tag):
        """Checks if the specified file exists."""
        return os.path.exists(self.get_filename(job_info, tag))

    def read_file(self, job_info, tag):
        """Reads the file specified by job_info and tag."""
        filename = self.get_filename(job_info, tag)
        with open(filename) as f:
            logging.info('reading %s%s from %s', tag[0], tag[1], filename)
            return f.read()

    def write_file(self, job_info, tag, data):
        """Writes the file specified by job_info and tag."""
        filename = self.get_filename(job_info, tag)
        with open(filename, 'w') as f:
            logging.info('writing %s%s to %s', tag[0], tag[1], filename)
            f.write(data)

    def get_machine_info(self, job_info):
        """Gets the machine info json, using a cached value if possible."""
        machine = None
        if self.exists(job_info, MACHINE_INFO_TAG):
            machine = self.read_file(job_info, MACHINE_INFO_TAG)
        if not machine:
            machine = get_device_machine_info(job_info.make_args())
            self.write_file(job_info, MACHINE_INFO_TAG, machine)
        return machine

    def get_software_config(self, job_info):
        """Gets the software config json, using a cached value if possible."""
        if (self.exists(job_info, SOFTWARE_CONFIG_HOST_TAG) and
            self.exists(job_info, SOFTWARE_CONFIG_GUEST_TAG)):
            sw_host = self.read_file(job_info, SOFTWARE_CONFIG_HOST_TAG)
            sw_guest = self.read_file(job_info, SOFTWARE_CONFIG_GUEST_TAG)
        else:
            sw_host, sw_guest = get_device_software_configs(self, job_info)
            self.write_file(job_info, SOFTWARE_CONFIG_HOST_TAG, sw_host)
            self.write_file(job_info, SOFTWARE_CONFIG_GUEST_TAG, sw_guest)
        return sw_guest

class TastRunner:
    """Helper to run a tast test."""
    def __init__(self, job, job_info, hostname, test):
        self.job = job
        self.job_info = job_info
        self.hostname = hostname
        self.test = test

    def run(self, force=False, dry_run=False):
        """Runs the tast test."""
        tast_dir = None
        if not force and self.job.exists(self.job_info, TAST_LOG_TAG):
            tast_log = self.job.read_file(self.job_info, TAST_LOG_TAG)
            tast_dir = find_tast_results_dir(tast_log)

        if not tast_dir:
            cmd = ('cros_sdk tast run -buildbundle=crosint '
                   f'{self.hostname} {self.test}')
            logging.info('running test: %s', cmd)
            if dry_run:
                tast_log = ''
            else:
                tast_log = local_call(cmd, stdout=True)
            self.job.write_file(self.job_info, TAST_LOG_TAG, tast_log)
            tast_dir = find_tast_results_dir(tast_log)

        return TastResult(self.test, tast_log, tast_dir)

class TastResult:
    """Manage the results from a tast run."""
    def __init__(self, test, tast_log, tast_dir):
        self.test = test
        self.tast_log = tast_log
        self.tast_dir = tast_dir

    def get_file(self, file):
        """Get a file from the results."""
        if not self.tast_dir:
            return None

        # TODO(davidriley): Support temporary resultsdir to avoid
        # creating a lot of old tast directories.
        filename = os.path.join(self.tast_dir, 'tests', self.test, file)
        try:
            output = local_call(f'cros_sdk cat {filename}', stdout=True)
        except subprocess.CalledProcessError:
            # TODO(davidriley): Don't ignore error?
            logging.info('Unable to read %s, ignoring', filename)
            output = None
        return output

class Device:
    """Manages configuring a device."""
    def __init__(self, pb, job_info):
        self.pb = pb
        job_info.add_name(JobInfo.DEVICE, self.pb.name)
        job_info.add(JobInfo.DEVICE, 'hostname', self.pb.host_name)
        job_info.add(JobInfo.DEVICE, 'board', self.pb.board)
        job_info.add(JobInfo.DEVICE, 'owner', get_owner())

class Build:
    """Manages configuring a build."""
    def __init__(self, pb, job_info):
        self.pb = pb
        job_info.add_name(JobInfo.BUILD, self.pb.name)
        job_info.add(JobInfo.BUILD, 'version', self.pb.version)

    def flash(self, cmd_args):
        """Flashes a new build to a device."""
        image = 'xbuddy://remote/%(device_board)s/%(build_version)s/test' % (
                cmd_args)
        cmd_args['image'] = image

        # TODO(davidriley): Check version fully before flashing, in particular
        # this likely doesn't handle dev builds well.
        version = get_device_version(cmd_args)
        logging.info('existing version %s', version)
        if version == self.pb.version:
            logging.info('correct version already present, skipping flashing')
            return
        cmd = 'cros flash %(device_hostname)s %(image)s' % cmd_args
        logging.info(cmd)
        local_call(cmd)

class Parameter:
    """Manages configuring a parameter."""
    def __init__(self, pb):
        self.pb = pb

    def set_parameter(self, value, job_info):
        """Set a parameter on a device."""
        job_info.set_parameter(self.pb.name, value)
        perform_call(self.pb.local_execution,
                     self.pb.command_line, job_info.make_args())

def make_result_db_id(invocation_source, full_name, start_time):
    """Create a results database id for a run."""
    start = start_time.strftime('%Y%M%d-%H%M%S')
    return f'{invocation_source}-{full_name}-{start}'

class Workload:
    """Manages executing a workload."""
    def __init__(self, pb, job_info):
        self.pb = pb
        job_info.add_name(JobInfo.WORKLOAD, self.pb.name)

    def run(self, job, job_info):
        """Run a workload."""
        cmd_args = job_info.make_args()
        runner = TastRunner(job, job_info, cmd_args['device_hostname'],
                            self.pb.name)
        return runner.run(dry_run=False)

    def generate_result(self,
                        r,
                        start_time,
                        end_time,
                        machine_json,
                        software_config_json,
                        results_chart_json,
                        labels):
        """Generate the results database results for a run."""
        # Add all the information about the run.
        full_name = f'tast.{self.pb.name}'
        invocation_source = f'user/{get_owner()}'
        r.id.value = make_result_db_id(invocation_source, full_name, start_time)
        r.start_time.FromDatetime(start_time)
        r.end_time.FromDatetime(end_time)
        machine = machine_pb2.Machine()
        json_format.Parse(machine_json, machine)
        r.machine.value = machine.name.value
        software_config = software_config_pb2.SoftwareConfig()
        json_format.Parse(software_config_json, software_config)
        r.software_config.value = software_config.id.value
        # TODO(davidriley): This shouldn't be hardcoded.
        r.execution_environment = result_pb2.Result.ExecutionEnvironment.STEAM
        r.invocation_source = invocation_source
        r.test_name = full_name
        r.benchmark = 'apitrace' if 'TraceReplay' in full_name else 'tast'
        r.trace.value = full_name

        # Add the metrics.
        results_chart = json.loads(results_chart_json)
        for trace, metrics in results_chart.items():
            # TODO(davidriley): What happens if there are multiple traces?
            r.trace.value = trace
            for metric, values in metrics.items():
                larger_is_better = values['improvement_direction'] == 'up'
                r.metrics.add(name=metric,
                              index=0,
                              value=values['value'],
                              units=values['units'],
                              larger_is_better=larger_is_better,
                              externally_gathered=False)
        r.primary_metric_name = 'fps'

        # Add the labels.
        for (grouping, name), value in labels.items():
            r.labels.add(name=name, value=value, grouping=grouping)
        return json_format.MessageToJson(r)

def run(args):
    """Run a plan."""
    plan = common.read_plan(args.input)
    logging.info('read plan')
    logging.info(plan)

    execute(plan, args.output, args.parallel)

def execute(plan, output_dir, parallel=False):
    """Execute a plan."""
    logging.info('executing plan, writing job results to %s', output_dir)

    # Calculate all permutations of parameters in advance.
    # This essentially is flattening:
    # for a in parameter1_values:
    #  for b in parameter2_values:
    #   ...
    #    for v in paremeterN_values
    param_list = []
    for parameter in plan.parameters:
        param_list.append(list(zip(itertools.repeat(parameter),
                                   parameter.values)))
    param_combinations = list(itertools.product(*param_list))

    # TODO(davidriley): Handle staging payloads.

    # Make the output directory if necessary.
    if not os.path.exists(output_dir):
        logging.info('making job directory %s', output_dir)
        os.makedirs(output_dir)
    job = JobOutput(output_dir)
    job_info = job.start()

    # Create a unique identifier for the run.
    run_label = make_run_label(os.path.basename(os.path.abspath(output_dir)))
    job_info.add_name(JobInfo.RUN, run_label, path_component=False)
    plan_json = json_format.MessageToJson(plan)
    job.write_file(job_info, EXECUTED_PLAN_TAG, plan_json)

    # TODO(davidriley): This (and the other execute methods) only handles
    # non-empty configurations, but if a configuration isn't present should
    # traverse through to later parameters.  ie, if builds or parameters
    # is empty it should still process workloads.

    with concurrent.futures.ThreadPoolExecutor() as executor:
        if parallel:
            mapfunc = executor.map
        else:
            mapfunc = map
        results = mapfunc(lambda x: execute_device(plan, param_combinations,
                                                   x, job, job_info.copy()),
                          plan.devices)

    # Flatten results from list of lists into a  flat list.
    results = list(itertools.chain(*results))

    logging.info('%d total runs -> run label: %s', len(results), run_label)

def execute_device(plan, param_combinations, device_pb, job, job_info):
    """Execute the plan for a given device."""
    _ = Device(device_pb, job_info)
    logging.debug('> device %s', job_info.make_id())

    machine = job.get_machine_info(job_info)

    results = []
    for build_pb in plan.builds:
        results.extend(execute_build(plan, param_combinations,
                                     build_pb, job, job_info.copy(), machine))

    # Gather all the results in a format suitable for uploading.
    result_list = result_pb2.ResultList()
    result_list.value.extend(results)
    result_list_json = json_format.MessageToJson(result_list)
    job.write_file(job_info, RESULT_LIST_TAG, result_list_json)

    logging.debug('< device %s', job_info.make_id())
    return results

def execute_build(plan, param_combinations, build_pb, job, job_info, machine):
    """Execute the plan for a given build."""
    build = Build(build_pb, job_info)
    logging.debug('>> build %s', job_info.make_id())

    build.flash(job_info.make_args())
    software_config = job.get_software_config(job_info)

    # There is a bit of inversion with the parameter order and workloads.
    # In the end, all the parameters are set each time in case any workload
    # disrupts any of them.
    results = []
    for parameter_set in param_combinations:
        for workload_pb in plan.workloads:
            result = execute_workload_with_params(workload_pb, parameter_set,
                                                  job, job_info.copy(),
                                                  machine, software_config)
            if result:
                results.append(result)

    logging.debug('<< build %s', job_info.make_id())
    return results

def execute_workload_with_params(workload_pb, parameter_set,
                                 job, job_info, machine, software_config):
    """Execute the plan for a given workload and set of parameters."""
    workload = Workload(workload_pb, job_info)
    # Set each parameter on the device (and fill in the job_info).
    for p, v in parameter_set:
        param = Parameter(p)
        param.set_parameter(v, job_info)
    logging.debug('>>> workload %s', job_info.make_id())

    # TODO(davidriley): This should handle when old results are used
    # (and end_time).
    start_time = datetime.datetime.now()

    # Run the test (or used the cached results).
    # TODO(davidriley): This should support retrying of failed tests.
    tast_result = workload.run(job, job_info)

    # TODO(davidriley): The results chart shouldn't need to be gathered
    # and saved each time.  In particular if the tast results directory
    # disappears (eg when moving to temporary results dir), then this will
    # start failing.
    results_chart = tast_result.get_file(RESULTS_CHART_JSON)
    if results_chart:
        job.write_file(job_info, TAST_RESULTS_TAG, results_chart)
    else:
        return None

    end_time = datetime.datetime.now()

    # Generate an entry for the results database.
    result = result_pb2.Result()
    result_json = workload.generate_result(result,
                                           start_time,
                                           end_time,
                                           machine,
                                           software_config,
                                           results_chart,
                                           job_info.make_result_labels())
    job.write_file(job_info, RESULTS_DB_RESULT_TAG, result_json)

    logging.debug('<<< workload %s', job_info.make_id())
    return result
