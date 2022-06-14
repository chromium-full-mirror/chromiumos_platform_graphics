# General

The expectations in this directory are used by the deqp-runner in
host and VM by different tests. In particular graphics\_parallel\_dEQP
and borealis.DEQP use this to filter known and expected failures.

# DEQP Expectations

The expected fails list will be entries like
'dEQP-SUITE.test.name,Crash', such as you find in a failures.csv,
results.csv, or the "Some failures found:" stdout output of a previous
run.  Enter a test here when it has an expected state other than Pass or
Skip.

The skips list is a list of regexs to match test names to not run at
all. This is good for tests that are too slow or uninteresting to ever
see status for.

The flakes list is a list of regexes to match test names that may have
unreliable status.  Any unexpected result of that test will be marked
with the Flake status and not cause the test run to fail.  The runner
does automatic flake detection on its own to try to mitigate
intermittent failures, but even with that we can see too many spurious
failures in CI when run across many boards and many builds, so this lets
you run those tests while avoiding having them fail out CI runs until
the source of the flakiness can be resolved.

The primary source of board skip/flake/fails will be files in this test
directory under boards/, but we also list some common entries directly
in the code here to save repetition of the explanations.  The files may
contain empty lines or comments starting with '#'.

We could avoid adding filters for other apis than the one being tested,
but it's harmless to have unused tests in the lists and makes
copy-and-paste mistakes less likely.
