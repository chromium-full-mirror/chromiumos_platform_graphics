# Graphics results database tools

## Google Cloud setup

Contact someone in OWNERS for access to the appropriate BigQuery database.

Authenticate against Google Cloud:
```sh
gcloud auth login
gcloud auth application-default login
```

Set project for BQ uploads:
```sh
gcloud config set project chromeos-graphics
```

## Python setup

The following installs the protobuf libraries, the results database
library and the bigquery library.

### Host

```sh
# optional: source ~/venv/bin/activate
pushd ../../../../../src/config/python >/dev/null
pip3 install .
popd /dev/null

pip3 install .
pip3 install install --upgrade google-cloud-bigquery

PATH=$PATH:$PWD/src/platform/graphics/src/results_database
```

### Chrome OS

emerge-$BOARD cros-config-api graphics-utils-python
cros deploy $dut --root /usr/local cros-config-api graphics-utils-python

### Crostini/Crouton

```sh
sudo apt install python3-pip
git clone https://chromium.googlesource.com/chromiumos/config
pip3 install config/python
git clone https://chromium.googlesource.com/chromiumos/platform/graphics
pip3 install graphics/src/results_database
PATH=$PATH:$PWD/graphics/src/results_database
```

## Common actions

### Capture machine information

The machine information is not expected to change over the lifetime of the
machine.

```sh
record_machine_info.py --owner LDAP --name UNIQUE_HOST_ID -o machine.json
```

### Capture software configuration information

The software configuration changes with new Chrome OS or Debian versions,
or when the packages installed change (via cros deply or apt).

Run in host Chrome OS context:
```sh
sw_id=$(date +%Y%m%d-%H%M%S)
record_software_config.py --id "HOSTNAME-chromeos-${sw_id}" \
  --output chromeos.json
```

Run in Termina context:
```sh
sw_id=$(date +%Y%m%d-%H%M%S)
record_software_config.py --id "HOSTNAME-termina-${sw_id}" \
  --parent chromeos.json --output termina.json
```

Run in Crostini context:
```sh
sw_id=$(date +%Y%m%d-%H%M%S)
record_software_config.py --id "HOSTNAME-crostini-${sw_id}" \
  --parent termina.json --output crostini.json
```

Alternatively, the --load argument can be used to merge software configs after
the fact.

### Capture software override

If a custom version of a package is being used that is not the system
software (eg locally built), information can be gathered from a git repo
and then attached to the result.  Detailed information can be specified
with options to the script.

```sh
record_package_override.py ~/work/apitrace -o apitrace.json
record_package_override.py ~/work/mesa -o mesa.json
```

### Run apitrace

Run apitrace, optionally using filename of log to identify trace.
```sh
trace_id=10047
log_id=$(date +%Y%m%d-%H%M%S)

local env_override="LD_LIBRARY_PATH=/tmp/mesa/usr/local/lib/x86_64-linux-gnu LIBGL_DRIVERS_PATH=/tmp/mesa/usr/local/lib/x86_64-linux-gnu/dri"

env -S "$env_override" glxinfo -B |& tee "$trace_id-$log_id.txt"
echo "CMD: " env -S "$env_override" apitrace replay -b "$trace_id"/*.trace |& tee -a "$trace_id-$log_id.txt"
env -S "$env_override" apitrace replay -b "$trace_id"/*.trace |& tee -a "$trace_id-$log_id.txt"
```

The replay command can be invoked with "timeout 1500s" to specify a 1500s
timeout.  env_override does not have to be specified but can be used to
use a different version of Mesa.

### Create results protobuf

```sh
summarize_apitrace_log.py --machine machine.json --software crostini.json \
  --package mesa.json --execution_environment crostini --output results.json \
  "$trace_id-$log_id.txt"
```

summarize_apitrace_log.txt parses the trace id and start time from the filename
or the --trace and --start_time options can be used to explicitly set them.

### Upload data to BigQuery

The --deduplicate and --dryrun options can be used if there is any concern
with adding potentially already existing data.

```sh
bq_insert_pb.py results.json
bq_insert_pb.py --message TraceList trace-info.json
bq_insert_pb.py --message Machine machine.json
bq_insert_pb.py --message SoftwareConfig software.json
```

## BigQuery initial setup

This only should need to be done once if the project changes from
chromeos-graphics.

### Dataset setup

Follow the public
[instructions](https://cloud.google.com/bigquery/docs/datasets) to create
a new 'graphics' dataset.

The options to use are:
- Multi-region
- US region
- Table expiration disabled
- Case insensitive table names disabled
- Default collation disabled

Alternatively, the following should work from command-line but has not been
recently verified:
```
bq --project_id chromeos-graphics mk graphics
```

### Table setup

The BigQuery tables need to be created with schemas based off of the
protobufs.

From a [Chrome infra checkout](https://sites.google.com/a/google.com/chrome-infrastructure/getting-started) build the [bqschemaupdater](https://chromium.googlesource.com/infra/luci/luci-go/+/main/tools/cmd/bqschemaupdater/README.md) tool.

Abbreviated version to get bqschemaupdater
```
mkdir -p ~/cr && cd ~/cr
fetch infra
cd infra
eval `go/env.py`
bqschemaupdater --help
```

Modify the protobufs to match the format expected by bqschemaupdater and
actually create the tables:
```
cd ~/chromiumos
cd infra/proto/src/test/custom_results/graphics
sed -i -e 's,import "test/custom_results/graphics/,import ",' *.proto
PROJECT=chromeos-graphics
bqschemaupdater -I . -table "${PROJECT}".graphics.results -message test.custom_results.graphics.Result
bqschemaupdater -I . -table "${PROJECT}".graphics.traces -message test.custom_results.graphics.Trace
bqschemaupdater -I . -table "${PROJECT}".graphics.machines -message test.custom_results.graphics.Machine
bqschemaupdater -I . -table "${PROJECT}".graphics.software_configs -message test.custom_results.graphics.SoftwareConfig
```

Use bq_insert_pb.py to verify that the tables can be uploaded to.

### Table migration

Alternatively instead of starting with the tables from scratch, data can be
optionally migrated over from another project or dataset.  Be careful as
these commands can wipe out the destination tables.
```
PROJECT=chromeos-graphics
bq cp 936557322845:graphics.results "${PROJECT}":graphics.results
bq cp 936557322845:graphics.traces "${PROJECT}":graphics.traces
bq cp 936557322845:graphics.machines "${PROJECT}":graphics.machines
bq cp 936557322845:graphics.software_configs "${PROJECT}":graphics.software_configs
```
