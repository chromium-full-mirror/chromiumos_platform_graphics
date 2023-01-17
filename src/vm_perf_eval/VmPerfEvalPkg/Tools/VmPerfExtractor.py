#!/usr/bin/env python3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""VmPerfExtractor allows one to easily extract test artifacts out of a FAT32 image

   Once a test has been run within a VM the test artifacts will not be directly accessible.
   These artifacts will be present within the image file that was used to house the EFI
   binary. This script will extract these artifacts so they can be
   directly accessed by any tools that need access to them.
"""

import argparse
import os

import fs  # pylint: disable=import-error


def extract_artifacts(image_name, output_directory):
    """Extracts test artifacts from an image file"""
    fatfs_name = f'{image_name}.img'
    os.makedirs(output_directory, exist_ok=True)
    with fs.open_fs(f'fat://{fatfs_name}') as fatfs:
        # Use the walk method here to find ALL files in the image
        # that are not *.opt or under the EFI directory.
        #
        # The hierarchy within the FAT image should be preserved
        # in the output directory
        for entry in fatfs.walk.files(exclude=['*.opt'], exclude_dirs=['EFI']):
            file_dir = os.path.dirname(entry)

            if file_dir.startswith('/'):
                file_dir = file_dir.replace('/', '', 1)
            file_name = os.path.basename(entry)
            file_output_dir = os.path.join(output_directory, file_dir)

            # Initiate the file copy and output directory creation
            os.makedirs(file_output_dir, exist_ok=True)
            with open(os.path.join(file_output_dir, file_name), "wb") as output_fd:
                fatfs.download(path=entry, file=output_fd)

def main():
    """Main entry point for the artifact extractor script"""
    parser = argparse.ArgumentParser(description=__doc__)

    parser.add_argument('--image_name', default='rwdisk',
                        help='The file name (sans extension) of the input FAT image file')

    parser.add_argument('--output_directory', required=True,
                        help='The directory name of where all extracted artifacts will be placed')

    args = parser.parse_args()
    extract_artifacts(args.image_name, args.output_directory)

if __name__ == '__main__':
    main()
