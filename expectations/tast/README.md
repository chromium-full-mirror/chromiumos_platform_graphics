# Overview

The expectations file in this directory are used by tast tests which
have integrated the tast [expectations package]. These are installed
in the test image as part of the [graphics-expectations] package.

The expectations files are used to tell tast that, for particular
devices, a test case is expected to fail due to an already-triaged
reason.

# Contents and organization

The contents of an expectations file is documented in the
[expectations package] README.md. To summarize, expectations files are
YAML and contain a map from a test case name to an
`expectations.Expectation` structure for that test case. For
parameterized tests, multiple test case expectations can be specified in
a single file for a particular device.

The files are stored in a directory that is determined by the test
package and function. For example, an expectations file for
`tast.video.PlatformDecoding.vp8` will be placed in
tast/video/PlatformDecoding.

The expectations file name is used to match particular devices using
one of the following identifiers (in order): model, build board (e.g.
trogdor-kernelnext), board (e.g. trogdor), GPU chipset, or all. Only the
first matching file will be used.

## File names

The name of a test expectation is documented in the [expectations package],
but it is worth noting that it will be up to a test writer to determine
the most appropriate expectation file type (model, build board, board, GPU
chipset, or all). In general, select the most generic file type you can.
I.e. types later in the list.

If you can use the GPU chipset to cover the same expectations for multiple
boards, then do so. It is perfectly acceptable to use the model file type
if there really is model specific behavior. One example would be if a
particular model has lower memory than other models and the failure arises
from an "out of memory" situation.

For device specific failures, the GPU chipset file type is likely a good
type to start with.

# Authoring expectations

## From stainless results

The script [create\_expectations\_from\_stainless.go](create_expectations_from_stainless.go)
can be used to update and author new expectations files using PASS/FAIL
results from stainless. This is designed to run from the ChromiumOS SDK chroot.

### Prerequisites

1. dev-go/gcp-bigquery package is present. Run `sudo emerge dev-go/gcp-bigquery`
   your ChromiumOS SDK chroot to stage the dev-go/gcp-bigquery package.
2. Gcloud application default credentials. On your gLinux host, run the following
   command and follow the instructions: `gcloud auth application-default login`.
   Copy `~/.config/gcloud/application_default_credentials.json` into the chroot.

### Usage examples

A test `tast.video.PlatformDecoding.vaapi_av2_conformance` has been failing
devices using the board `notepad`. The issue is being tracked and is awaiting
a fix from a vendor. In the meantime, it would be helpful to silence the
failures to remove them from the video guard's triage queue. No expectation
file exists for board notepad.

#### Creating a new expectations file

Run:

```
~/trunk/src/platform/tast/tools/go.sh run create_expectations_from_stainless.go\
 --board=notepad\
 --test="tast.video.PlatformDecoding.vaapi_av2_conformance"\
 --ticket=b/1234\
 --comments="Notepad decoder has an issue with intra secondary transform application. The vendor is working on a fix"\
 --output video/PlatformDecoding/board-notepad.yml
```

This will look up the tast results for `tast.video.PlatformDecoding.vaapi_av2_conformance`
for devices using the board `notepad` over the last 3 days. If the test results
in Stainless are all "FAIL", then it creates an expectation file at the path
`video/PlatformDecoding/board-notepad.yml` with entries for this failure.

I.e.

```
video.PlatformDecoding.vaapi_av2_conformance:
  expectation: FAIL
  tickets:
  - b/1234
  comments: Notepad decoder has an issue with intra secondary transform
    application. The vendor is working on a fix
  since_build: R108-15175.0.0
```

#### Adding test expectations to an existing file

It is also noted that `notepad` devices are failing the tests `tast.video.PlatformDecoding.vaapi_vvc_h264_base`
and `tast.video.PlatformDecoding.vaapi_vvc_h265_base`. This issue is also being
tracked and is awaiting resolution from the vendor. We can update the board
expectations for `notepad` by using the following command:

```
~/trunk/src/platform/tast/tools/go.sh run create_expectations_from_stainless.go\
 --input video/PlatformDecoding/board-notepad.yml\
 --update_input\
 --board=notepad\
 --test_regex="^tast\.video\.PlatformDecoding\.vaapi_vvc_h26[45]_base$"\
 --ticket=b/1235\
 --comments="Notepad decoder VVC is broken due to a recent regression. The vendor is working on a fix"
```

Note the use of a regular expression for the test case matching. The
`--update_input` flag tells the program to write updates to the input file,
`video/PlatformDecoding/board-notepad.yml`.

The script won't modify any existing expectation, so the workflow can be applied
before or after landing the original expectations file. With the expectations
file checked in, the next build should have passing results in Stainless.

#### Removing tests from an expectations file

After some time, a fix is checked in for `tast.video.PlatformDecoding.vaapi_vvc_h264_base`.
The test begins to fail with an error message: "Test passed! Consider removing
FAIL expectation due to b/1235". To remove the expectation, there are two
options: using results in stainless or doing offline editing.

##### Using stainless

To use stainless results to decide what tests to remove from the expectations
file, calling

```
~/trunk/src/platform/tast/tools/go.sh run create_expectations_from_stainless.go\
 --input video/PlatformDecoding/board-notepad.yml\
 --update_input \
 --board=notepad\
 --test_regex="^tast\.video\.PlatformDecoding\.vaapi_vvc_h26[45]_base$"
```

remove the `video.PlatformDecoding.vaapi_vvc_h264_base` test case due to its
failure reason. If there is not a fix for the other matching test case,
`video.PlatformDecoding.vaapi_vvc_h265_base`, will not be modified. It is fine
to make the test regular expression match many parameterized test cases.

##### Offline editing

If the fix has not landed yet, use the offline editing functionality to delete
the test case by calling:

```
~/trunk/src/platform/tast/tools/go.sh run create_expectations_from_stainless.go\
 --input video/PlatformDecoding/board-notepad.yml\
 --update_input
 --test="tast.video.PlatformDecoding.vaapi_vvc_h264_base"\
 --delete_tests
```

Note: this uses the `--test` option to list a single test, but the
`--test_regex` option could be used similarly.

After modifying the file, it must be reviewed and committed before it is used
for future testing.
[expectations package]: https://chromium.googlesource.com/chromiumos/platform/tast-tests/+/HEAD/src/go.chromium.org/tast-tests/cros/local/graphics/expectations/
[graphics-expectations]: https://chromium.googlesource.com/chromiumos/platform/graphics/+/HEAD/expectations/
