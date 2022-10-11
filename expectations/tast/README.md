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

[expectations package]: https://chromium.googlesource.com/chromiumos/platform/tast-tests/+/HEAD/src/chromiumos/tast/local/graphics/expectations/
[graphics-expectations]: https://chromium.googlesource.com/chromiumos/platform/graphics/+/HEAD/expectations/
