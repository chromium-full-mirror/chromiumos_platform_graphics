# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import PIL
from PIL import Image
import argparse
import numpy as np
import matplotlib.pyplot as plt
import time
import pathlib

format_data = {
    1: ('DXGI_FORMAT_R32G32B32A32_TYPELESS', 4, 'uint32', False),
    2: ('DXGI_FORMAT_R32G32B32A32_FLOAT', 4, 'single', False),
    3: ('DXGI_FORMAT_R32G32B32A32_UINT', 4, 'uint32', False),
    4: ('DXGI_FORMAT_R32G32B32A32_SINT', 4, 'int32', False),
    5: ('DXGI_FORMAT_R32G32B32_TYPELESS', 3, 'uint32', False),
    6: ('DXGI_FORMAT_R32G32B32_FLOAT', 3, 'single', False),
    7: ('DXGI_FORMAT_R32G32B32_UINT', 3, 'uint32', False),
    8: ('DXGI_FORMAT_R32G32B32_SINT', 3, 'int32', False),
    9: ('DXGI_FORMAT_R16G16B16A16_TYPELESS', 4, 'uint16', False),
    10: ('DXGI_FORMAT_R16G16B16A16_FLOAT', 4, 'uint16', False),
    11: ('DXGI_FORMAT_R16G16B16A16_UNORM', 4, 'uint16', False),
    12: ('DXGI_FORMAT_R16G16B16A16_UINT', 4, 'uint16', False),
    13: ('DXGI_FORMAT_R16G16B16A16_SNORM', 4, 'int16', False),
    14: ('DXGI_FORMAT_R16G16B16A16_SINT', 4, 'int16', False),
    15: ('DXGI_FORMAT_R32G32_TYPELESS', 2, 'uint8', False),
    16: ('DXGI_FORMAT_R32G32_FLOAT', 2, 'single', False),
    17: ('DXGI_FORMAT_R32G32_UINT', 2, 'uint8', False),
    18: ('DXGI_FORMAT_R32G32_SINT', 2, 'int8', False),
    27: ('DXGI_FORMAT_R8G8B8A8_TYPELESS', 4, 'uint8', False),
    28: ('DXGI_FORMAT_R8G8B8A8_UNORM', 4, 'uint8', False),
    29: ('DXGI_FORMAT_R8G8B8A8_UNORM_SRGB', 4, 'uint8', False),
    30: ('DXGI_FORMAT_R8G8B8A8_UINT', 4, 'uint8', False),
    31: ('DXGI_FORMAT_R8G8B8A8_SNORM', 4, 'int8', False),
    32: ('DXGI_FORMAT_R8G8B8A8_SINT', 4, 'int8', False),
    41: ('DXGI_FORMAT_R32_FLOAT', 1, 'single', False),
    (0x80000000 | 20) : ('D3DFMT_R8G8B8', 3, 'uint8'),
    (0x80000000 | 21) : ('D3DFMT_A8R8G8B8', 4, 'uint8', True),
    (0x80000000 | 22) : ('D3DFMT_X8R8G8B8', 4, 'uint8', True),
    (0x80000000 | 32) : ('D3DFMT_A8B8G8R8', 4, 'uint8', True),
    (0x80000000 | 33) : ('D3DFMT_X8B8G8R8', 4, 'uint8', True),

}

def show_image(args):
    bytes_read = None
    with open(args.image, "rb") as f:
        bytes_read = f.read()
    if not bytes_read:
        print(f"Could not open image {args.image}")

    if len(bytes_read) < 12:
        print("Error invalid image, no header")
        return None
    format = int.from_bytes(bytes_read[0:4], 'little')
    width = int.from_bytes(bytes_read[4:8], 'little')
    height = int.from_bytes(bytes_read[8:12], 'little')
    fd = format_data[format]
    image = np.array(np.frombuffer(bytes_read[12:], fd[2]))
    if fd[3]: # We have to shuffe this, because DX9 outputs in odd byte ordering
        for i in range(0, int(len(image) / 4)):
            tmp = image[4*i + 0]
            image[4*i+0] = image[4*i+2]
            image[4*i+2] = tmp
    image = np.reshape(image, (width, height, fd[1]))
    return plt.imshow(image)

def main(args):
    fname = pathlib.Path(args.image)
    if not fname.exists():
        print(f'Could not fine image {args.image}')
        return False
    t = fname.stat().st_mtime
    img = show_image(args)
    if not img:
        return False
    plt.show(block=False)

    while True:
        plt.pause(1)
        # If our image is closed, then
        # return
        if not plt.get_fignums():
            return
        new_fname = pathlib.Path(args.image)
        if not new_fname.exists():
            continue
        nt = new_fname.stat().st_mtime
        if nt != t:
            show_image(args)
        t = nt

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="Shows golden images")
    parser.add_argument('image', type=str, help='the image to open')
    args = parser.parse_args()
    main(args)
