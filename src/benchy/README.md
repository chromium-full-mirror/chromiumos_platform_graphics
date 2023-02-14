# Benchy borealis benchmarking framework

benchy supports creation and execution of a plan consisting of:
- devices
- builds
- parameters
- workloads

All the different permutations are tested, and results are generated
in json protobuf format for uploading to the results database for further
analysis by plx.

The plan is specified according via the benchy plan protos, and the tool
allows formulation of a plan through command-line paramters to add and
import desired entities.

The formulated plan is executed, in parallel if possible, using a results
directory to store both intermediate workload results and the final
results database json files.  The results database bq_insert_pb.py script
is used to upload results.

## Google Cloud setup

Contact someone in OWNERS for access to the appropriate BigQuery database.
Request the following permissions from google.com:stainless-dev:
- BigQuery Data Editor
- BigQuery Data Viewer
- BigQuery Job User
- BigQuery User
- Service Usage Consumer

Authenticate against Google Cloud:
```sh
gcloud auth login
gcloud auth application-default login
```

Set project for BQ uploads:
```sh
gcloud config set project google.com:stainless-dev
```

The following might be necessary:
```
export GOOGLE_APPLICATION_CREDENTIALS=$HOME/.config/gcloud/application_default_credentials.json
gcloud auth application-default set-quota-project google.com:stainless-dev
```

## Python setup

The following installs the necessary python libraries.

### Host

Optionally run these from a Python virtual enviornment.  The source
will need to be done each time.
```sh
python3 -m venv ~/venv
source ~/venv/bin/activate
```

Install the Python libraries and add to path to find the tools.
```sh
pip3 install --upgrade google-cloud-bigquery
pip3 install --upgrade protobuf

pip3 install ../../../../../src/config/python
pip3 install ../results_database

PATH=$PATH:$PWD/src/platform/graphics/src/results_database:$PWD/src/platform/graphics/src/benchy
```

## Usage

By default benchy reads and writes plans from stdin and stdout allow plans
to be formulated through piping together benchy executions.

Example invocation of executing a sample workload to a specified DUT:
```sh
./benchy create --sample |
./benchy device add --clear copano-pvt-sku1-C123456 --host DUT --board volteer |
./benchy run results

cd results
bq_insert_pb.py --deduplicate --message Machine machineinfo*json
bq_insert_pb.py --deduplicate --message SoftwareConfig softwareconfig*json
bq_insert_pb.py resultlist*json
```

### Plan creation

The "create" subcommand is used to create a plan.

```sh
./benchy create
./benchy create --sample
./benchy create > plan.json
```

The "device", "build", and "workload" subcommands are used to
modify a plan to add, remove, clear and import elements.

```sh
./benchy create | \
./benchy device add copano-evt-sku1-C123456 --host 192.168.1.2 --board volteer | \
./benchy device add anahera-evt-sku1-C234567 --host 192.168.1.3 --board brya \
> devices.json

./benchy create | \
./benchy device import --clear devices.json | \
./benchy build add R108-15183.8.0 --version R108-15183.8.0 | \
./benchy workload add borealis.TraceReplay.glxgears > basic.json
```

Parameters represent tunable parameters of interest.  Each parameter has
each potential value tested against all workloads.  If multiple parameters
are present, then all combinations of parameters are tested.  Parameters
must currently be specified by editing the plan json directly or by writing
python to generate the plan.  Parameters are set on device by specifying
a command line that is substituted using named % substitutions.

For example:
- 'sudo sysctl vm.swappiness=%(parameter_value)s'

In general, substitutions can be done for each plan element (device, build,
workload, parameter) and all of the named fields.

### Plan execution

The "run" subcommand is used to execute a plan.  It requires a directory
to stage results and the job output.

```sh
./benchy run results < plan.json
```

### Upload data to BigQuery

The results database script bq_insert_pb.py should be used to upload the
results to bigquery.  The --deduplicate option should be used to avoid adding
potentially already existing data.

```sh
cd results
bq_insert_pb.py --deduplicate --message Machine machineinfo*json
bq_insert_pb.py --deduplicate --message SoftwareConfig softwareconfig.json
bq_insert_pb.py --deduplicate resultlist*json
```

### Anaylze results via plx

Once uploaded the results are regularly imported into plx tables:
- chromeos_gfx_resource.results
- chromeos_gfx_resource.machines
- chromeos_gfx_resource.software_configs

Furthermore, two views faciliate viewing results:
- chromeos_gfx_resource.results_benchy
- chromeos_gfx_resource.results_flattened_benchy

Finally, two dashboards exist:
- http://go/benchy-summary
- http://go/benchy-summary-parameter

### Generate a local report
```sh
./benchy aggregate results
```
will generate a summary of the performance `performance_summary` inside the directory results.

For more setup details, refer to
```
go/borealis-benchy
```
