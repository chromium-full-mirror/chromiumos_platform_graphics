#!/usr/bin/env python3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""VmPerfPackager allows one to easily combine a UEFI binary and an options file in an image

   This scripts allows one to create a FAT32 image with both the UEFI binary and options file
   embedded within.

   It will perform all the necessary steps to ensure that the image can be used to run the
   appropriate tests. The resulting image can be used by any VM monitor which supports both
   UEFI BIOS boot and the raw disk image format.
"""

import argparse
import subprocess

import fs  # pylint: disable=import-error


class EfiImagePackager:
    """This class is responsible for performing both the image and options packaging"""

    def __init__(self):
        self.fatfs_filename = None
        self.fatfs_object = None
        self.options_file = None

    def create_image(self, image_name, image_size):
        """Creates an empty image file with the appropriate name and file size"""
        self.fatfs_filename = f'{image_name}.img'

        # Generate an appropriately sized file (basically creating a blank slate)
        subprocess.run(
            args=['dd', 'if=/dev/zero', f'of={self.fatfs_filename}',
            'bs=1M', f'count={image_size}'], check=True)

        # Make a FAT image out of the block file we just created
        subprocess.run(args=['mkfs.vfat', self.fatfs_filename], check=True)

        # Open the file system
        self.fatfs_object = fs.open_fs(f'fat://{self.fatfs_filename}')

    def dump_options_into_image(self, options_name, options_array):
        """Dumps the options in the array into an options file within the FAT image"""
        if len(options_array) >= 1:
            self.options_file = f'{options_name}.opt'

            # Dump all the user specified options to pass into the binary
            for options_string in options_array:
                self.fatfs_object.appendtext(self.options_file, f'{options_string}\n')

    def insert_efi_binary_into_image(self, binary_path):
        """Injects the UEFI binary into the image file"""
        with open(binary_path, mode='rb') as binary_file:
            self.fatfs_object.makedirs('EFI/BOOT')
            self.fatfs_object.upload('EFI/BOOT/BOOTX64.efi', binary_file)

def main():
    """Main entry point for the packaging script"""
    parser = argparse.ArgumentParser(description=__doc__)

    parser.add_argument('--image_name',
                        help='The filename (sans extension) of the output image',
                        default='rwdisk')
    parser.add_argument('--efi_binary', required=True,
                        help='The path to the EFI binary that is to be packaged')

    # Default to 1GiB for the image size
    parser.add_argument('--image_size_in_mbs',
                        default=1024,
                        help='The size of the resulting image in MBs')
    parser.add_argument('--option_filename',
                        help='The filename (sans extension) to put options in')
    parser.add_argument(
        '--option', action='append',
        help='Use this to add a parameter to be picked up by the binary, one per parameter')

    args = parser.parse_args()

    packager = EfiImagePackager()

    # Create the image and insert the UEFI binary into it
    packager.create_image(args.image_name, args.image_size_in_mbs)
    packager.insert_efi_binary_into_image(args.efi_binary)

    # If specified, add the options file for the UEFI binary
    if args.option_filename:
        packager.dump_options_into_image(args.option_filename, args.option)

if __name__ == '__main__':
    main()
