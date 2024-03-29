// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <d3d12.h>
#include <dxgi.h>
#include <gtest/gtest.h>
#include <array>
#include "common_test_utils.h"
#include "dx_pointer.h"
#include "dxgi_formats.h"
#include "fuzzy_comparator.h"

template <DXGI_FORMAT FORMAT, size_t LENGTH>
using default_buffer_comparator = strict_compute_comparator<FORMAT, LENGTH>;

template <DXGI_FORMAT FORMAT, size_t LENGTH,
          typename comparator = default_buffer_comparator<FORMAT, LENGTH>,
          typename PARAMETER = void>
class buffer_test
    : public std::conditional_t<std::is_void<PARAMETER>::value, ::testing::Test,
                                ::testing::TestWithParam<PARAMETER>> {
 public:
  static const uint32_t kLength = LENGTH;
  static const size_t kDataSize = sizeof(typename format_to_type<FORMAT>::type) *
                                  format_to_type<FORMAT>::num_elements * LENGTH;
  static const DXGI_FORMAT format = FORMAT;

 protected:
  void RunTest(
      const std::function<void(ID3D12Device*, ID3D12CommandQueue*,
                               ID3D12CommandAllocator*, ID3D12Resource*)>& function) {
    HRESULT hr = S_OK;

    // TODO(renatopereyra): Re-enable if Proton/VKD3D-Proton ever support DX12 debug layers
    //
    // DXPointer<ID3D12Debug> debug_controller;
    // hr = D3D12GetDebugInterface(
    //     __uuidof(ID3D12Debug), reinterpret_cast<void**>(&debug_controller.get()));
    // ASSERT_EQ(S_OK, hr);
    // debug_controller->EnableDebugLayer();

    DXPointer<ID3D12Device2> device;
    hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0,
                           __uuidof(ID3D12Device2),
                           reinterpret_cast<void**>(&device.get()));
    ASSERT_EQ(S_OK, hr);

    DXPointer<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC queue_desc{
        D3D12_COMMAND_LIST_TYPE_DIRECT,     // Type
        D3D12_COMMAND_QUEUE_PRIORITY_HIGH,  // Prioirty
        D3D12_COMMAND_QUEUE_FLAG_NONE,      // Flags
        0                                   // NodeMask
    };

    hr = device->CreateCommandQueue(&queue_desc, __uuidof(ID3D12CommandQueue),
                                    reinterpret_cast<void**>(&queue.get()));
    ASSERT_EQ(S_OK, hr);

    D3D12_RESOURCE_DESC buffer_create_info = {
        D3D12_RESOURCE_DIMENSION_BUFFER,  // Dimension
        0,                                // Alignment
        kDataSize,                        // Width
        1,                                // Height
        1,                                // DepthOrArraySlice,
        1,                                // MipLevels
        DXGI_FORMAT_UNKNOWN,
        {
            1,                           // Count
            0,                           // Quality
        },                               // SampleDesc
        D3D12_TEXTURE_LAYOUT_ROW_MAJOR,  // Layout
        D3D12_RESOURCE_FLAG_NONE,        // Flags
    };

    D3D12_HEAP_PROPERTIES buffer_heap_props{
        D3D12_HEAP_TYPE_READBACK,         // Type
        D3D12_CPU_PAGE_PROPERTY_UNKNOWN,  // CPUPageProperty
        D3D12_MEMORY_POOL_UNKNOWN,        // MemoryPoolPreference
        0,                                // CreationNodeMask
        0,                                // VisibleNodeMask
    };

    DXPointer<ID3D12Resource> buffer;
    hr = device->CreateCommittedResource(
        &buffer_heap_props, D3D12_HEAP_FLAG_NONE, &buffer_create_info,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource),
        reinterpret_cast<void**>(&buffer.get()));
    ASSERT_EQ(S_OK, hr);

    DXPointer<ID3D12CommandAllocator> command_allocator;
    hr = device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
        reinterpret_cast<void**>(&command_allocator));
    ASSERT_EQ(S_OK, hr);

    function(device.get(), queue.get(), command_allocator.get(), buffer.get());

    wait_for_completion(queue.get());
    void* data;
    hr = buffer->Map(0, nullptr, &data);
    ASSERT_EQ(S_OK, hr);

    if (dx_tests::cmdline_args::update_images) {
      auto stored = store_data<FORMAT, LENGTH>(data, "dx12");
      ASSERT_TRUE(stored);
    } else {
      std::string err;
      auto dat = load_data<FORMAT, LENGTH>(&err, "dx12");
      ASSERT_NE(0, dat.size());
      comparator c;
      EXPECT_TRUE(c.compare(data, dat)) << c.error_string() << std::endl;
    }
  }
};
