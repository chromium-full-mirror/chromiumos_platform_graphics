# Merge Tool

The Merge tool plays a very simple, yet essential role. It merges two
ApiTrace profiles, one containing only CPU timings and the other only GPU timing,
into a single file that can be consumed by tool Analyze.

You may ask, why do we need this tool? That is because when you collect
profile data with ApiTrace for VirGL, you should collect CPU and GPU profiles
separately. You need to do this because collecting GPU profile can significantly
skew the CPU timing data.

Concretely, you should run ApiTrace once to collect CPU timing data, as follows:

```bash
-> glretrace -pcpu --min-cpu-time=0 counter_strike.trace > counter_strike.cpu.prof
```
to get something like this:
``` text
-> cat counter_strike.cpu.prof
# call no gpu_start gpu_dura cpu_start cpu_dura vsize_start vsize_dura
...
call 2125 0 0 80332813 235 0 0 0 0 0 0 glBindFramebufferEXT
call 2126 0 0 80334198 295 0 0 0 0 0 0 glBindFramebufferEXT
call 2127 0 0 80336018 435 0 0 0 0 0 0 glClearColor
call 2128 0 0 80337873 4897 0 0 0 0 0 0 glClear
call 2129 0 0 80345324 1214 0 0 0 0 0 0 glBlitFramebufferEXT
...
```

And then again to collect GPU timing data, as follows:

```bash
-> glretrace -pgpu --min-cpu-time=0 counter_strike.trace > counter_strike.gpu.prof
```
``` text
-> cat counter_strike.gpu.prof
# call no gpu_start gpu_dura cpu_start cpu_dura vsize_start vsize_dura
...
call 2125 0 0 0 0 0 0 0 0 0 0 glBindFramebufferEXT
call 2126 0 0 0 0 0 0 0 0 0 0 glBindFramebufferEXT
call 2127 0 0 0 0 0 0 0 0 0 0 glClearColor
call 2128 -32052766274344 170583 0 0 0 0 0 0 0 0 glClear
call 2129 -32052766076927 128000 0 0 0 0 0 0 0 0 glBlitFramebufferEXT
...
```

Then you can merge the two profiles:

```bash
-> merge -cpu counter_strike.cpu.prof -gpu counter_strike.gpu.prof > counter_strike.prof
```

to get the final result:

``` text
-> cat counter_strike.prof
# call no gpu_start gpu_dura cpu_start cpu_dura vsize_start vsize_dura
...
call 2125 0 0 80332813 235 0 0 0 0 0 0 glBindFramebufferEXT
call 2126 0 0 80334198 295 0 0 0 0 0 0 glBindFramebufferEXT
call 2127 0 0 80336018 435 0 0 0 0 0 0 glClearColor
call 2128 -32052766274344 170583 80337873 4897 0 0 0 0 0 0 glClear
call 2129 -32052766076927 128000 80345324 1214 0 0 0 0 0 0 glBlitFramebufferEXT
...
```

Merge performs sanity checking as it processes the two input file. If it finds an error,
for example two lines with the same call # that do not invoke the same GL function,
it will spit out an error and quit. For that reason, it is important to use option
`--min-cpu-time=0` when running the ApiTrace tools to collect timing data. Without it,
different lines will be truncated because of non-matching CPU timings. Merge will fail
to process the resulting files.
