# Validate
Validate is a command-line tool for validating platform decoding using MD5 sums.

It can be used alone, e.g. by emerge/deploying to the DUT, but is expected to be
used in conjunction with  test runner that supplies the necessary arguments.

Example:
``` bash
validate -exec=decode_test -args="--md5 --visible" -metadata=hashes.json
```
