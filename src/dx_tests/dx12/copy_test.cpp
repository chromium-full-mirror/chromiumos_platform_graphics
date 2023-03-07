// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <d3d12.h>
#include <dxgiformat.h>
#include <gtest/gtest.h>
#include <stddef.h>
#include <windows.h>
#include <cstdint>
#include <functional>
#include <utility>

#include "buffer_test_fixture.h"
#include "common_test_utils.h"
#include "dx_pointer.h"
#include "dxgi_formats.h"
#include "pixel_test_fixture.h"

struct copy_location {
  uint32_t footprint_offset;
  uint32_t copy_x;
  uint32_t copy_y;
  uint32_t width;
  uint32_t height;
};

using comparator = default_image_comparator<DXGI_FORMAT_R8G8B8A8_UNORM, 10, 10>;
using copyTest =
    texel_test<DXGI_FORMAT_R8G8B8A8_UNORM, 10, 10, comparator, copy_location>;

TEST_P(copyTest, bufferToImage) {
  this->RunTest([](IDXGIAdapter*, ID3D12Device* device, ID3D12CommandQueue* queue,
                   ID3D12CommandAllocator* allocator, ID3D12Resource* texture) {
    const copy_location& loc = GetParam();
    DXPointer<ID3D12GraphicsCommandList> list = GetCommandList(device, allocator);
    DXPointer<ID3D12Resource> buffer = CreateAndFillUploadBuffer<uint8_t, 256 * 6>(
        device, [](size_t i) {
          if (i % 4 == 3) {  // All alphas will be 0
            return static_cast<uint8_t>(255);
          }
          // Stripes R, G, B, Black
          if (i % 16 == 0) {
            return static_cast<uint8_t>(255);
          }
          if (i % 16 == 5) {
            return static_cast<uint8_t>(255);
          }
          if (i % 16 == 10) {
            return static_cast<uint8_t>(255);
          }
          return static_cast<uint8_t>(0);
        });

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    footprint.Footprint.Width = loc.width;
    footprint.Footprint.Height = loc.height;
    footprint.Footprint.Format = format;
    footprint.Footprint.RowPitch = 256;
    footprint.Footprint.Depth = 1;
    footprint.Offset = loc.footprint_offset;

    D3D12_TEXTURE_COPY_LOCATION src = {
        buffer.get(),                              // pResource
        D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,  // Type
        {footprint}                                // Footprint
    };

    D3D12_TEXTURE_COPY_LOCATION dest = {
        texture,
        D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,  // Type
        {{0}}};

    list->CopyTextureRegion(&dest, loc.copy_x, loc.copy_y, 0, &src, nullptr);
    HRESULT hr = list->Close();
    ASSERT_EQ(S_OK, hr);

    ID3D12CommandList* cl = list.get();
    queue->ExecuteCommandLists(1, &cl);
    wait_for_completion(queue);
  });
}

TEST_P(copyTest, imageToImage) {
  this->RunTest([](IDXGIAdapter*, ID3D12Device* device, ID3D12CommandQueue* queue,
                   ID3D12CommandAllocator* allocator, ID3D12Resource* texture) {
    const copy_location& loc = GetParam();
    DXPointer<ID3D12GraphicsCommandList> list = GetCommandList(device, allocator);
    // Create an image that contains our content.
    auto img = CreateAndFillImage<format, 6, 6>(
        device, list.get(), [](size_t x, size_t, size_t c) {
          if (c == 3) {
            return static_cast<uint8_t>(255);
          }
          if ((x & 3) == 0 && c == 0) {
            return static_cast<uint8_t>(255);
          }
          if ((x & 3) == 1 && c == 1) {
            return static_cast<uint8_t>(255);
          }
          if ((x & 3) == 2 && c == 2) {
            return static_cast<uint8_t>(255);
          }
          return static_cast<uint8_t>(0);
        });

    D3D12_RESOURCE_BARRIER barrier{
        D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,  // Type
        D3D12_RESOURCE_BARRIER_FLAG_NONE,        // Flags
        {D3D12_RESOURCE_TRANSITION_BARRIER{
            img.first.get(),                   // pResource
            0,                                 // subresource
            D3D12_RESOURCE_STATE_COPY_DEST,    // StateBefore
            D3D12_RESOURCE_STATE_COPY_SOURCE,  // StateAfter
        }}};
    list->ResourceBarrier(1, &barrier);

    D3D12_TEXTURE_COPY_LOCATION src = {
        img.first.get(),                            // pResource
        D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,  // Type
    };

    D3D12_TEXTURE_COPY_LOCATION dest = {
        texture,
        D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,  // Type
        {{0}}};

    D3D12_BOX box;
    box.left = loc.copy_x;
    box.top = loc.copy_y;
    box.right = loc.copy_x + loc.width;
    box.bottom = loc.copy_y + loc.height;
    box.front = 0;
    box.back = 1;

    list->CopyTextureRegion(&dest, loc.copy_x, loc.copy_y, 0, &src, &box);
    HRESULT hr = list->Close();
    ASSERT_EQ(S_OK, hr);

    ID3D12CommandList* cl = list.get();
    queue->ExecuteCommandLists(1, &cl);
    wait_for_completion(queue);
  });
}

INSTANTIATE_TEST_SUITE_P(CopyTests, copyTest,
                         ::testing::Values(copy_location{0, 0, 0, 6, 6},
                                           copy_location{0, 1, 1, 5, 5},
                                           copy_location{512, 0, 0, 1, 1}));

struct copy_buffer_location {
  uint32_t dst_offset;
  uint32_t srcx;
  uint32_t srcy;
  uint32_t srcwidth;
  uint32_t srcheight;
};

// This buffer is large enough to hold a 10x10 texture.
using buffer_comparator = default_buffer_comparator<DXGI_FORMAT_R8G8B8A8_UNORM, 640>;
using copyToBufferTest =
    buffer_test<DXGI_FORMAT_R8G8B8A8_UNORM, 640, buffer_comparator, copy_buffer_location>;
TEST_P(copyToBufferTest, imageToBuffer) {
  this->RunTest([](ID3D12Device* device, ID3D12CommandQueue* queue,
                   ID3D12CommandAllocator* allocator, ID3D12Resource* buffer) {
    const copy_buffer_location& loc = GetParam();
    DXPointer<ID3D12GraphicsCommandList> list = GetCommandList(device, allocator);
    // Create an image that contains our content.
    auto img = CreateAndFillImage<format, 6, 6>(
        device, list.get(), [](size_t x, size_t, size_t c) {
          if (c == 3) {
            return static_cast<uint8_t>(255);
          }
          if ((x & 3) == 0 && c == 0) {
            return static_cast<uint8_t>(255);
          }
          if ((x & 3) == 1 && c == 1) {
            return static_cast<uint8_t>(255);
          }
          if ((x & 3) == 2 && c == 2) {
            return static_cast<uint8_t>(255);
          }
          return static_cast<uint8_t>(0);
        });

    D3D12_RESOURCE_BARRIER barrier{
        D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,  // Type
        D3D12_RESOURCE_BARRIER_FLAG_NONE,        // Flags
        {D3D12_RESOURCE_TRANSITION_BARRIER{
            img.first.get(),                   // pResource
            0,                                 // subresource
            D3D12_RESOURCE_STATE_COPY_DEST,    // StateBefore
            D3D12_RESOURCE_STATE_COPY_SOURCE,  // StateAfter
        }}};
    list->ResourceBarrier(1, &barrier);

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    footprint.Footprint.Width = 6;
    footprint.Footprint.Height = 6;
    footprint.Footprint.Depth = 1;
    footprint.Footprint.Format = format;
    footprint.Footprint.RowPitch = 256;
    footprint.Offset = loc.dst_offset;

    D3D12_TEXTURE_COPY_LOCATION dest = {
        buffer,                                    // pResource
        D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,  // Type
        {footprint}                                // Footprint
    };

    D3D12_TEXTURE_COPY_LOCATION src = {img.first.get(),
                                       D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,  // Type
                                       {{0}}};

    D3D12_BOX box;
    box.left = loc.srcx;
    box.top = loc.srcy;
    box.right = loc.srcx + loc.srcwidth;
    box.bottom = loc.srcy + loc.srcheight;
    box.front = 0;
    box.back = 1;
    list->CopyTextureRegion(&dest, 0, 0, 0, &src, &box);

    list->Close();

    ID3D12CommandList* cl = list.get();
    queue->ExecuteCommandLists(1, &cl);
    wait_for_completion(queue);
  });
}

INSTANTIATE_TEST_SUITE_P(CopyBufferTests, copyToBufferTest,
                         ::testing::Values(copy_buffer_location{0, 0, 0, 6, 6},
                                           copy_buffer_location{0, 1, 1, 5, 5},
                                           copy_buffer_location{0, 0, 0, 1, 1}));

// TODO(awoloszyn): Add tests for 3d texture copies
// TODO(awoloszyn): Add tests for copying between subresource indices.
