# General

The expectations in this directory are used by graphics and video
tests, including [DEQP](deqp), [piglit](piglit), and [tast](tast).

They get installed by chromeos-base/graphics-expectations.ebuild to
/usr/local/graphics/expectations/

# Deploying expectations

## Using rsync

To test local changes it is fastest to use `rsync` to copy them to the
device under test (DUT). Run

```
rsync -ra ~/chromiumos/src/platform/graphics/expectations root@<DUT>:/usr/local/graphics/
```

where `<DUT>` is the DUT's SSH alias, hostname, or IP address.

## Using cros deploy

1. `cros workon start graphics-expectations -b <target>` to enable
building the graphics-expectations package locally,
2. `emerge-<target> graphics-expectations` to build the package with
local changes, and
3. `cros deploy --root=/usr/local <device> graphics-expectations` to
deploy the changes to a device.
