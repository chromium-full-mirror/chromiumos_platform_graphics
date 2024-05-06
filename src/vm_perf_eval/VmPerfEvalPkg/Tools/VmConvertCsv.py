#!/usr/bin/env python
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A script which converts ApicTimerTest CSV's into real time (us)

   The script converts the delays logged in the CSV files
   by ApicTimerTest from TSC ticks into real time.

   It takes a directory path containing the test output
   as a parameter. The converted files are placed in a
   subdirectory of the test output directory with the name of "converted".

   The TSC rate used to perform this conversion is obtained
   from the SystemInfo.csv file within the test output directory.
"""

import argparse
import csv
import os
from pathlib import Path
import numpy    # pylint: disable=import-error
import parse    # pylint: disable=import-error

# Name of columns/fields that we are to converted
# from TSC ticks to real time.
fields_to_convert = {
    'ProgramTime',
    'DesiredToReceive',
    'IsrExecTime'
}

def initialize_stats_dict():
    """Initializes an 'empty' statistics dictionary and returns it

       This function will create and return an 'empty' statistics dictionary
       that can be used later on when parsing the test output CSVs.

       The format is also useful for calculating any statistics on each
       column of data within the CSVs.
    """
    stats_dict = {}
    for feild_name in fields_to_convert:
        stats_dict[feild_name] = {'Active': {}, 'Inactive': {}}

    return stats_dict

def extract_cpu_info(test_directory):
    """Extract CPU information from the output test directory

    This function parses the SystemInfo.csv file, extracts
    both the CPU name and the TSC frequency and returns them
    as a tuple

    Args:
        test_directory: Test directory name

    Returns:
        A tuple in the form of [CPU Name, TSC Frequency (int)]
    """
    sysinfo_name = os.path.join(test_directory, 'SystemInfo.csv')
    with open(sysinfo_name, encoding='utf-8') as sysinfo_csv:
        inforeader = csv.DictReader(sysinfo_csv)
        # We only expect a single row to be present in this file
        for row in inforeader:
            tscfrequency = int(row[' TSC Frequency'])
            cpuname =  row['CPU'].strip()
    return [cpuname, tscfrequency]

def convert_header_row(header_row, ticks_multiplier):
    """Adds real time units to CSV header row

    Adds the appropriate units to each column that
    is to be converted.

    Args:
        header_row: The original header row
        ticks_multiplier: Multiplier for TSC ticks -> real time conversion

    Returns:
        A list containing the modified header row
    """
    converted_row = []
    for header_field in header_row:
        header_name = header_field
        for field in fields_to_convert:
            if header_field.strip() == field.strip():
                if ticks_multiplier == 1000:
                    header_name = f'{header_field} (ms)'
                elif ticks_multiplier == 1000000:
                    header_name = f'{header_field} (us)'
                elif ticks_multiplier == 1000000000:
                    header_name = f'{header_field} (ns)'
                else:
                    header_name = f'{header_field} - x{ticks_multiplier}'

        converted_row.append(header_name)
    return converted_row

def fixup_data_row(data_row):
    """Corrects rows which have a negative ReceiveTsc-DesiredTsc

    This would imply that the event was received before the desired time.
    If this type of event occured, the row will have its DesiredToReceive
    value forced to 0.

    Args:
        data_row: Original data row

    Returns:
        Modified row if the even occured, unmodified row is otherwise
    """
    returned_row = data_row.copy()

    if data_row['DesiredTsc'] > data_row[' ReceiveTsc']:
        returned_row[' DesiredToReceive'] = 0
        print('Issue found')
        print(data_row)
        print(returned_row)

    return returned_row

def convert_data_point(value, header_name, tsc_frequency, ticks_multiplier):
    """Converts a single point data point from TSC ticks into real time

    Args:
        value: Value to perform the conversion on
        header_name: The column name for which this data point belongs in
        tsc_frequency: The TSC tick frequency of CPU used for data collection
        ticks_multiplier: The multiplier used to real time units (1000 = ms, 1000000 = us, etc.)

    Returns:
        The converted data point in real time units
    """
    converted_value = value

    for field in fields_to_convert:
        if header_name.strip() == field.strip():
            converted_value = float(value)
            converted_value = round (((converted_value / tsc_frequency) * ticks_multiplier), 4)
            break

    return converted_value

def convert_data_row(data_row, tsc_frequency, ticks_multiplier):
    """Performs conversion on a row of data

    Converts a row of data from TSC ticks to real time

    Args:
        data_row: A list containing an element for each CSV column
        tsc_frequency: TSC frequency to be used for conversion
        ticks_multiplier: Multiplier for TSC ticks -> real time conversion

    Returns:
        A list containing the converted row
    """
    converted_row = []

    this_data_row = fixup_data_row(data_row)

    # The incoming row is expected to come from a DictReader
    for _, header_field in enumerate(this_data_row):
        value = convert_data_point(
            this_data_row[header_field], header_field, tsc_frequency, ticks_multiplier)
        converted_row.append(value)

    return converted_row

def dump_stats_to_csv(target_directory, field_name, stats_dict):
    """Dumps statistics about a particular column of data in the CSV

       This function will calculate statistics (mean, min, max, etc.) on a single
       column of data from the incoming CSV file. The output will be a
       CSV file which will show the statistics for each CPU and phase.

    Args:
        target_directory: The directory to place the resulting statistics file
        field_name: The column name to calculate the statistics on
        stats_dict: The previously generated statistics dictionary
    """
    output_file_name = Path(target_directory).joinpath(f'{field_name}_stats.csv')

    with open(output_file_name, mode='w', encoding='utf-8') as csv_output:
        writer = csv.DictWriter(csv_output,
            fieldnames=['Phase', 'CPU', 'APIC ID', '99th', '95th',
                        '90th', 'Median', 'Mean', 'Min', 'Max', 'StdDev'])

        writer.writeheader()
        # Sort the keys so that the values are listed in ascending order
        # based on the CPU number

        # The inactive phase statistics will be written, then the
        # active phase statistics will be written.
        inactive_keys = stats_dict[field_name]['Inactive'].keys()
        inactive_keys = sorted(inactive_keys, key=int)

        for cpu in inactive_keys:
            cpu_dict = stats_dict[field_name]['Inactive'][cpu]
            writer.writerow(
                {
                    'Phase': 'Inactive',
                    'CPU': cpu,
                    'APIC ID': cpu_dict['APIC'],
                    '99th' : cpu_dict['99th'],
                    '95th' : cpu_dict['95th'],
                    '90th' : cpu_dict['90th'],
                    'Median' : cpu_dict['Median'],
                    'Mean' : cpu_dict['Mean'],
                    'Min' : cpu_dict['Min'],
                    'Max' : cpu_dict['Max'],
                    'StdDev' : cpu_dict['StdDev'],
                }
            )

        active_keys = stats_dict[field_name]['Active'].keys()
        active_keys = sorted(active_keys, key=int)

        for cpu in active_keys:
            cpu_dict = stats_dict[field_name]['Active'][cpu]
            writer.writerow(
                {
                    'Phase': 'Active',
                    'CPU': cpu,
                    'APIC ID': cpu_dict['APIC'],
                    '99th' : cpu_dict['99th'],
                    '95th' : cpu_dict['95th'],
                    '90th' : cpu_dict['90th'],
                    'Median' : cpu_dict['Median'],
                    'Mean' : cpu_dict['Mean'],
                    'Min' : cpu_dict['Min'],
                    'Max' : cpu_dict['Max'],
                    'StdDev' : cpu_dict['StdDev'],
                }
            )

def calculate_stats_on_file(file, cpuinfo_tuple, ticks_multiplier):
    """Calculates some useful statistics on the incoming CSV file

    Args:
        file: The full path to the CSV file to convert
        cpuinfo_tuple: CPU information tuple.
        ticks_multiplier: Multiplier for TSC ticks -> real time conversion
    """
    value_dict = {}
    file_stats = {}

    # Populate an array for each of the fields that is to be
    # converted
    for names in fields_to_convert:
        value_dict[names] = []

    # Populate our number dict as follows:
    # Field Name is the key, the value is array containing the values
    # obtained/converted from the CSV.
    # We can then use NumPy to calculate the various statistics we need
    with open(file, encoding='utf-8') as csvfile:
        csvreader = csv.DictReader(csvfile)
        for _, input_row in enumerate(csvreader):
            this_input_row = fixup_data_row(input_row)
            for field, value in this_input_row.items():
                if field.strip() in value_dict:
                    value_dict[field.strip()].append(
                        convert_data_point(value, field, cpuinfo_tuple[1], ticks_multiplier))

    # Extract identifier information about the core from the file name
    parsed = parse.parse('ApicTimer_c{}_apic{}_{}0.csv', file.name)
    cpu_number = parsed[0]
    cpu_apic_id = parsed[1]
    phase = parsed[2]

    # For each array in the value dictionary (indexed by the column name), calculate
    # the statistics
    for names, value_array in value_dict.items():
        percentile_99 = numpy.percentile(value_array[1:], 99)
        percentile_95 = numpy.percentile(value_array[1:], 95)
        percentile_90 = numpy.percentile(value_array[1:], 90)
        min_value = numpy.min(value_array[1:])
        max_value = numpy.max(value_array[1:])
        std_dev = numpy.std(value_array[1:])
        median = numpy.median(value_array[1:])
        mean_value = numpy.mean(value_array[1:])

        # Limit these to 4 digits after the decimal point
        # to keep things clean in the statistics CSV file.
        file_stats[names] = {
            'APIC': cpu_apic_id,
            '99th' : round(percentile_99, 4),
            '95th' : round(percentile_95, 4),
            '90th' : round(percentile_90, 4),
            'Min' : round(min_value, 4),
            'Max' : round(max_value, 4),
            'StdDev' : round(std_dev, 4),
            'Median' : round(median, 4),
            'Mean' : round(mean_value, 4)
        }

    return (cpu_number, phase, file_stats)

def convert_single(file, cpuinfo_tuple, ticks_multiplier):
    """Converts a single CSV test output file in a test directory

    This function will convert the CSV from TSC ticks as units
    to real time.

    Args:
        file: The full path to the CSV file to convert
        cpuinfo_tuple: CPU information tuple.
        ticks_multiplier: Multiplier for TSC ticks -> real time conversion
    """
    # We are expecting the full path of the CSV file here
    csvPath = Path(file)
    convertedCsvPath = csvPath.parent.joinpath('converted').joinpath(csvPath.name)

    with open(file, encoding='utf-8') as csvfile:
        with open(convertedCsvPath, 'w', encoding='utf-8') as converted_csv:
            csvwriter = csv.writer(converted_csv)
            csvreader = csv.DictReader(csvfile)
            for i, input_row in enumerate(csvreader):
                if i == 0:
                    output_fields = convert_header_row(input_row, ticks_multiplier)
                    # Make any modifications in the title row
                    csvwriter.writerow(output_fields)

                data_row = convert_data_row(input_row, cpuinfo_tuple[1], ticks_multiplier)
                csvwriter.writerow(data_row)

def convert_test_directory(test_directory, ticks_multiplier):
    """Converts all the test output CSV files in a test directory

    This function converts the appropriate test CSV files into
    real time units.

    Args:
        test_directory: The test directory to convert
        ticks_multiplier: Multiplier for TSC ticks -> real time conversion
    """

    # Do an early prepopulation of our statistics dictionary
    statistics = initialize_stats_dict()

    cpuinfo_tuple = extract_cpu_info(test_directory)

    dirPath = Path(test_directory)
    corefiles = dirPath.glob('ApicTimer_*.csv')

    # Create a 'converted' directory
    dirPath = Path(os.path.join(test_directory, 'converted'))
    dirPath.mkdir(exist_ok=True)

    for core_file in corefiles:
        print(f' Converting {core_file}...')
        convert_single(core_file, cpuinfo_tuple, ticks_multiplier)

        # Do another pass, generating some statistics from this data
        stats_tuple = calculate_stats_on_file(core_file, cpuinfo_tuple, ticks_multiplier)

        # Merge the results given to us in to the statistics dictionary
        cpu_number = stats_tuple[0]
        phase = stats_tuple[1]

        # Index 2 contains a dictionary with a 'field_name' to stats dictionary
        # See the calculate_stats_on_file function
        stats_dict = stats_tuple[2]

        # Merge the results from this file into the dictionary
        for field, stats in stats_dict.items():
            statistics[field.strip()][phase][cpu_number] = stats

    # Dump the statistics
    for field_name in statistics:
        dump_stats_to_csv(dirPath, field_name, statistics)

def main():
    """Main entry point function of the script"""
    parser = argparse.ArgumentParser()

    parser.add_argument('--test_directory',
            help='Directory containing the output of the ApicTimerTest, typically a date code',
            required=True)

    # By default, the tick values will be converted to microseconds.
    parser.add_argument('--multiplier',
            help='Multiplier to be used for the ticks -> real time conversion',
            default=1000000)

    known_args = parser.parse_args()
    convert_test_directory(known_args.test_directory, int(known_args.multiplier))

if __name__ == '__main__':
    main()
