# Graphics results database tools

## Google Cloud setup

Contact someone in OWNERS for access to the appropriate BigQuery database.

Authenticate against Google Cloud:
```sh
gcloud auth login
```

Set project for BQ uploads:
```sh
gcloud config set project google.com:stainless-dev
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
