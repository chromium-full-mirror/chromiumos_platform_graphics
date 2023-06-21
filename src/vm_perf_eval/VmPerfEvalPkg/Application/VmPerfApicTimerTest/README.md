APIC Timer Test Application
===========================

The APIC timer test is an application which can get a decent gauge of the
wake up latency. This is the time between an interrupt being signalled and
when the interrupt actually starts being serviced by a CPU/vCPU. Idle CPU
and active CPU cases are both tested in a "PWM" like fashion.

This test application makes use of the the TSC deadline mode present
(currently) on Intel CPU's. This mode allows software to specify an absolute
time in which an interrupt must fire rather than a relative time. This enables
us to do a direct comparison between the desired interrupt time stamp and the
one sampled very close to the beginning of the interrupt service routine.

This test cannot run on a CPU lacking TSC deadline support. This means, at least
for bare metal, this test cannot run on AMD CPU's. However, this program will
work just fine under Linux KVM virtualization on AMD CPU's.


Basic Theory
============
The test defines a test item and a record. A test item defines parameters for
a particular section of a test run. A test item contains the following
information:
- Frequency: The rate at which we perform a full CPU active/idle cycle
- Duty Cycle: The ratio of the CPU active time and the period defined by the frequency
- Sample Count: The number of measurements to take (At a rate of 2x frequency)
- Flags which control how the test for this item is actually performed

A single test run can be made of up to 64 individual test items, provided there
is enough space in the record storage buffer for all particular test items.
The sample count number also, indirectly, controls the duration of the test.

The test record holds the results from the active/inactive portions. It records
the following information:
- TSC at interrupt start (stub)
- TSC at C interrupt start
- Time it took to program the APIC deadline timer
- Flags (one bit indicating active/inactive portion)

With these parameters, the test will calculate the period, active time and inactive time
in TSC ticks. With the active time being the amount of ticks that the CPU
should be actively doing some work, and the inactive time being the amount of ticks the CPU
spends in the halted state. The APIC deadline timer interrupt is used to switch
the state between active and idle.

When the interrupt arrives, the TSC is sampled very early within the interrupt
handler stub.  This information along with other time measurements will be saved
in the record buffer. Checks are performed to see if we have collected a sufficient
amount of records based on either the current test item sample count or the size of the record
buffer attached to the running CPU. The test moves onto the next item if the requested
sample count has been hit. The test stops running if the CPU record buffer
has been filled or if there are no more test items remaining.

Additional data will be collected related to the overall length of time the core spends in
the test. The information collected here will be put in a per core summary file.

The following data is collected:
- TSC at the start and the end of the test
- Idealized TSC at test end (explained below)

From the data above, we can calculate the test duration in TSC ticks.

"Idealized TSC"
===============

The concept of idealized TSC is introduced here to represent a machine which requires no time
for processing and has 0 delay in response to the timer interrupt firing.

With these constraints, we can calculate the expected value of the TSC at any point during
the test using the TSC sampled at the beginning of the test and the calculated ticks per phase
at each switching point. Delays in the interrupt firing will shift the real time further
away from the ideal point. This can be used as a single value to represent the overall delay
experienced by the test. These values can be compared with each other, once they have been
converted from ticks to actual time.


Test Output and Results
=======================

After the test has completed on all specified cores, the main supervisor core will produce
a number of files in the resulting test directory:
- `SystemInfo`: This is a CSV that contains both the CPU name and the estimated TSC frequency
- `Items`: This is also a CSV file that contains a list of the test items used for this run
- `ApicTimer_<...>`: This is a set of CSV file that has the active and inactive records of each core
- `PerCoreSummary`: This contains test duration information (for each core)

These files can be used to get an idea of how the system performed latency wise.

Bare metal tests can be used to establish a baseline that represents the absolute
best that the system can achieve latency wise when responding to an interrupt,
unencumbered by any virtualization overhead. Under virtualization, the vCPU
threads are subject to a number of external factors that could impact its ability to respond
to external events in a timely manner. It stands to reason that any problems (or non-issues)
we see here can affect other interrupts, mainly from virtio devices.

How to run
===========
Follow the instructions within the base package README on how to either package the binary
for use on a virtual machine or to place it on a USB key for use on a bare metal machine.

The binary will be named "VmPerfApicTimerTest.efi".

This test program requires a bit more memory that the previous test applications.
It is recommended to use at least 4GiB of memory for the resulting VM.
For crosvm, add "--mem <MemoryInMiB>" to the command line to modify the amount of
memory given to the VM.

The option file must be named "ApicTimerTest.opt", or use "ApicTimerTest" as a parameter
when making use of the packager script.

Options
=======
These section defines the options that can be used to modify the behavior of the test.

### ItemN=<CommaSeparatedList>
This defines an individual test item. Where N ranges from 0 to 63.

The line supplied with this option is expected to be formatted as follows (in this exact order):

`<Freq>,<DutyCycle>,<SampleCount>,<CStateIndex>,<OneOrMoreCommaSeparatedFlags>`

- `Freq`: Frequency in Hz, 2 interrupts for each cycle so ~1000 interrupts/sec = 500Hz
- `DutyCycle`: Duty cycle in percentage (* 256, ie 8.8 fixed point): 50% = 12800
- `SampleCount`: Number of samples to collect at this state
- `CStateIndex`: Index into the C-state array for the idle side. 255 = regular HLT
- `OneOrMoreCommaSeparatedFlags`: A comma separated list comprised of any number of the following flags:
### Flags
- `HP_ON`: Explicitly turn on halt polling when running under KVM, does nothing otherwise.
- `HP_OFF`: Explicitly turn off halt polling when running under KVM, does nothing otherwise.


### Cores=N
Use up to the actual number of cores specified in this file. By default, the test will
use all cores that have been found (up to a maximum of 64 remote cores).

Currently, this will treat all cores as the same, which generally works on CPUs without
a hybrid architecture or ones with no hyperthreading. These will impact the results on baremetal
if the CPU contains any of these technologies.

### PerCoreRecordBufferSize=N
This is the amount of memory allocated for each core to hold test records. This value sets
the number of records that can be held in each per core buffer.
It defaults to 256K records.

This also can be used to limit the duration of a test. A core will stop the test if its
record buffer has been filled.

### PollLoopTimeout=N
Specifies an upper poll timeout in minutes. This is the maximum amount of time that will
be waited for when waiting for the remote processors to complete their data collection.
It defaults to 45 minutes.

You MUST ensure that the poll timeout is sufficient for the test items
specified. If the poll timeout is hit before data collection completes, no results
will be saved.

### ResetFlag=N
Indicates whether the test should reset or shutdown the system at the end of the test.
Set to 1 to instruct the test to reset the system. Set to 0 to shutdown after
test completion. Reset (that is 1), is the default state. Select what works
best for your setup. For the Crosvm port of OVMF, either will terminate crosvm
at the end of the test.

### CoreTriggerMode=N
Allows the selection of the trigger mode when starting up the remote cores.
By default, when this setting is 0, the cores are started with no trigger.
This means the remote core will start the test as soon as it is kicked off.
When set to 1, the cores will spin and wait for a trigger value write.
This is intended to align all the remotes cores as close to each other in time
as possible.
