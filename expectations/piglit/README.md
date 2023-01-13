# Maintaining expectations for borealis.Piglit.quick\_gl and quick\_shader

There are several problems running borealis.Piglit.quick*

1) Even though the name is quick it is quite slow. We should remove slow running
tests.
2) Many tests are flaky but retrying them by the runner passes them often. This
means there is a huge reservoir of seemingly passing tests that are actually flaky
tests that sometimes don't even pass on retry. Because of that we don't mark
them as failing or even flaky but just don't run anything that is too flaky.

## Known Issues
We are at the limit of how many test cases we can list b/265467673
Adding many more causes "Compiled regex exceeds size limit of 10485760 bytes."
We need to wait for this issue to be fixed or start manually converting the
listed test cases into wildcards.

## Harvesting new expectations:

Run test then find the results.csv file. This file can be imported and column
sorted in Google sheets. Look for tests running way too long and put them into
all-chipsets-borealis-skips.txt


### From the command line:

    cp /tmp/tast/results/202.../tests/borealis.Piglit.quick_shader/piglit_results/*.csv .
    grep Flake results.csv | sed 's/\,Flake.*//' > Flake.txt
    grep Fail results.csv | sed 's/\,Fail.*//' > Fail.txt

Merge/cat/sort/uniq these files with alderlake-borealis-skips.txt and friends.
For the beginning it is ok to use the same skip files for similar Intel
generations like tigerlake and alderlake. Chances are the same tests are flaky
on both.

### Test the new expectation files

At a minimum make sure the tests start running and there are no syntax errors
or other problems like regex exploding.

    scp * root@$DUT:/usr/local/graphics/expectations/piglit/
    time tast run --buildbundle=crosint $DUT borealis.Piglit.quick_gl
    time tast run --buildbundle=crosint $DUT borealis.Piglit.quick_shader
