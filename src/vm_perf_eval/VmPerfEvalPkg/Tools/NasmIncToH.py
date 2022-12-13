#!/usr/bin/env python3
# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file.

"""NasmIncToH automates conversion of defines in an INC file to an header file

   The script is mainly intended to be used with the CPU bootstrapper in
   this package. This is an effort to prevent failures/bugs from mismatched
   definitions.

   It will convert all defines and structures in the INC file into a
   C header file appropriate for its use in the bootstrapper.

   The output file will be <prefix>.h from a provided input file of
   <prefix>.inc as input to the script.
"""

import argparse
from pathlib import Path


chromeos_copyright = '''
/* Copyright 2022 The ChromiumOS Authors
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/
'''

def conv_file(file):
    """The core file conversion logic"""

    with open(file) as inc_file_input:
        output_path = Path(file)

        output_name = f'{output_path.stem}.h'

        with open(output_name, 'w', newline='\n') as output:
            input_lines = inc_file_input.readlines()

            struct_name = None

            output.writelines([chromeos_copyright])
            output.writelines(['\n'])
            output.writelines(['/* This is an auto generated file, DO NOT EDIT! */\n'])
            output.writelines(['#include <Uefi.h>\n'])

            output.writelines(['#pragma pack( push )\n'])
            output.writelines(['#pragma pack( 1 )\n'])

            for currentline in input_lines:
                stripped_line = currentline.strip()
                if stripped_line.startswith('%define'):
                    define_split = stripped_line.split()
                    c_define = f'#define {define_split[1]} {define_split[2]}\n'
                    output.writelines([c_define])
                elif stripped_line.startswith('struc'):
                    struc_split = stripped_line.split()
                    struct_name = struc_split[1]
                    c_struct = 'typedef struct {\n'
                    output.writelines([c_struct])
                elif stripped_line.startswith('endstruc'):
                    c_struct = f'}} {struct_name};\n'
                    output.writelines([c_struct])
                    struct_name = None
                else:
                    if not stripped_line.startswith(';'):
                        # Ignore comments
                        linesplit = stripped_line.split()

                        if len(linesplit) > 3:
                            arrcount = linesplit[2]
                            name = linesplit[0]

                            name = name.replace(':', '')
                            if linesplit[1] == 'resb':
                                data_type = 'UINT8'
                            elif linesplit[1] == 'resw':
                                data_type = 'UINT16'
                            elif linesplit[1] == 'resd':
                                data_type = 'UINT32'
                            elif linesplit[1] == 'resq':
                                data_type = 'UINT64'

                            if arrcount == '1':
                                c_line = f'{data_type} {name};\n'
                            else:
                                c_line = f'{data_type} {name}[{arrcount}];\n'

                            output.writelines([c_line])

            output.writelines(['#pragma pack( pop )\n'])

def main():
    """Main entry point for the script"""
    parser = argparse.ArgumentParser(description=__doc__)

    parser.add_argument('--incfile',
                        help='The path of the inc file to perform conversion for',
                        required=True)

    args = parser.parse_args()
    conv_file(args.incfile)

if __name__ == '__main__':
    main()
