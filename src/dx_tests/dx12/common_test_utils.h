// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <d3d12.h>
#include <dxgi.h>
#include <gtest/gtest.h>
#include <math.h>
#include <windows.h>
#include <array>
#include "dx_pointer.h"
#include "dxgi_formats.h"
#include "root_signature_builder.h"

// Creates a buffer with the given data. Fills the buffer with the
// requested data. Leaves the buffer in the D3D12_RESOURCE_STATE_COPY_SOURCE
// state.
template <typename T, size_t N>
DXPointer<ID3D12Resource> CreateAndFillUploadBuffer(
    ID3D12Device* device, const std::function<T(size_t)>& fill) {
  D3D12_RESOURCE_DESC buffer_create_info = {
      D3D12_RESOURCE_DIMENSION_BUFFER,  // Dimension
      0,                                // Alignment
      N * sizeof(T),                    // Width
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
      D3D12_HEAP_TYPE_UPLOAD,           // Type
      D3D12_CPU_PAGE_PROPERTY_UNKNOWN,  // CPUPageProperty
      D3D12_MEMORY_POOL_UNKNOWN,        // MemoryPoolPreference
      0,                                // CreationNodeMask
      0,                                // VisibleNodeMask
  };

  DXPointer<ID3D12Resource> buffer;
  HRESULT hr = device->CreateCommittedResource(
      &buffer_heap_props, D3D12_HEAP_FLAG_NONE, &buffer_create_info,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, __uuidof(ID3D12Resource),
      reinterpret_cast<void**>(&buffer.get()));
  EXPECT_EQ(S_OK, hr);
  void* data;
  hr = buffer->Map(0, nullptr, &data);
  EXPECT_EQ(S_OK, hr);
  T* t = reinterpret_cast<T*>(data);
  for (size_t i = 0; i < N; ++i) {
    *(t++) = fill(i);
  }

  buffer->Unmap(0, nullptr);
  return buffer;
}

// Creates an on-gpu texture with the given data. Adds the required
// commands to the command list to fill the texture with the given data.
// Leaves the texture in the D3D12_RESOURCE_STATE_COPY_DEST
// state. This returns both the texture, as well as the scratch buffer
// that was used to upload data.
template <DXGI_FORMAT TEX_FORMAT, size_t tex_width, size_t tex_height>
std::pair<DXPointer<ID3D12Resource>, DXPointer<ID3D12Resource>> CreateAndFillImage(
    ID3D12Device* device, ID3D12GraphicsCommandList* list,
    std::function<typename format_to_type<TEX_FORMAT>::type(size_t x, size_t y, size_t c)>
        fill) {
  using format_type = typename format_to_type<TEX_FORMAT>::type;
  const size_t format_count = format_to_type<TEX_FORMAT>::num_elements;

  const uint64_t row_pitch = ((sizeof(format_type) * format_count * tex_width) +
                              (D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)) &
                             ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
  static_assert(row_pitch % sizeof(format_type) * format_count == 0,
                "Expected the format to be a divisor of 256, this code has to "
                "be updated to handle whatever you are doing");

  const uint64_t row_element_count = row_pitch /
                                     (format_count * sizeof(format_type));

  size_t x = 0;
  size_t y = 0;
  size_t c = 0;
  DXPointer<ID3D12Resource> upload_buffer =
      CreateAndFillUploadBuffer<format_type, format_count * row_element_count * tex_height>(
          device, [=, &x, &y, &c, &fill](size_t) {
            const size_t format_count = format_to_type<TEX_FORMAT>::num_elements;
            if (c == format_count) {
              c = 0;
              ++x;
            }
            if (x == row_element_count) {
              x = 0;
              ++y;
            }
            if (x > tex_width) {
              c++;
              return static_cast<format_type>(0);
            }
            return fill(x, y, c++);
          });

  D3D12_RESOURCE_DESC image_create_info = {
      D3D12_RESOURCE_DIMENSION_TEXTURE2D,  // Dimension
      0,                                   // Alignment
      tex_width,                           // Width
      tex_height,                          // Height
      1,                                   // DepthOrArraySlice
      1,                                   // MipLevels
      TEX_FORMAT,                          // Format
      {
          1,                         // Count
          0,                         // Quality
      },                             // SampleDesc
      D3D12_TEXTURE_LAYOUT_UNKNOWN,  // Layout
      D3D12_RESOURCE_FLAG_NONE,      // Flags
  };
  D3D12_HEAP_PROPERTIES image_heap_props{
      D3D12_HEAP_TYPE_DEFAULT,          // Type
      D3D12_CPU_PAGE_PROPERTY_UNKNOWN,  // CPUPageProperty
      D3D12_MEMORY_POOL_UNKNOWN,        // MemoryPoolPreference
      0,                                // CreationNodeMask
      0,                                // VisibleNodeMask
  };
  DXPointer<ID3D12Resource> texture;
  HRESULT hr = device->CreateCommittedResource(
      &image_heap_props, D3D12_HEAP_FLAG_NONE, &image_create_info,
      D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource),
      reinterpret_cast<void**>(&texture.get()));
  EXPECT_EQ(S_OK, hr);

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  footprint.Footprint.Width = tex_width;
  footprint.Footprint.Height = tex_height;
  footprint.Footprint.Format = TEX_FORMAT;
  footprint.Footprint.RowPitch = row_pitch;
  footprint.Footprint.Depth = 1;
  footprint.Offset = 0;

  D3D12_TEXTURE_COPY_LOCATION src = {
      upload_buffer.get(),                       // pResource
      D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,  // Type
      {footprint}                                // Footprint
  };

  D3D12_TEXTURE_COPY_LOCATION dest = {texture.get(),
                                      D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,  // Type
                                      {{0}}};

  list->CopyTextureRegion(&dest, 0, 0, 0, &src, nullptr);
  return std::make_pair(std::move(texture), std::move(upload_buffer));
}

// Create trivial image with full mips.
template <DXGI_FORMAT TEX_FORMAT, size_t tex_width, size_t tex_height>
DXPointer<ID3D12Resource> CreateTrivialImageWithFullMips(ID3D12Device* device) {
  using format_type = typename format_to_type<TEX_FORMAT>::type;
  const size_t format_count = format_to_type<TEX_FORMAT>::num_elements;

  const uint64_t row_pitch = ((sizeof(format_type) * format_count * tex_width) +
                              (D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)) &
                             ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
  static_assert(row_pitch % sizeof(format_type) * format_count == 0,
                "Expected the format to be a divisor of 256, this code has to "
                "be updated to handle whatever you are doing");

  const UINT16 mipLevels = static_cast<UINT16>(
                               floor(log2(std::max(tex_width, tex_height)))) +
                           1;

  D3D12_RESOURCE_DESC image_create_info = {
      D3D12_RESOURCE_DIMENSION_TEXTURE2D,  // Dimension
      0,                                   // Alignment
      tex_width,                           // Width
      tex_height,                          // Height
      1,                                   // DepthOrArraySlice
      mipLevels,                           // MipLevels
      TEX_FORMAT,                          // Format
      {
          1,                         // Count
          0,                         // Quality
      },                             // SampleDesc
      D3D12_TEXTURE_LAYOUT_UNKNOWN,  // Layout
      D3D12_RESOURCE_FLAG_NONE,      // Flags
  };
  D3D12_HEAP_PROPERTIES image_heap_props{
      D3D12_HEAP_TYPE_DEFAULT,          // Type
      D3D12_CPU_PAGE_PROPERTY_UNKNOWN,  // CPUPageProperty
      D3D12_MEMORY_POOL_UNKNOWN,        // MemoryPoolPreference
      0,                                // CreationNodeMask
      0,                                // VisibleNodeMask
  };
  DXPointer<ID3D12Resource> texture;
  HRESULT hr = device->CreateCommittedResource(
      &image_heap_props, D3D12_HEAP_FLAG_NONE, &image_create_info,
      D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource),
      reinterpret_cast<void**>(&texture.get()));
  EXPECT_EQ(S_OK, hr);

  return texture;
}

inline void wait_for_completion(ID3D12CommandQueue* queue) {
  DXPointer<ID3D12Fence> fence;
  DXPointer<ID3D12Device> device;
  queue->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&device.get()));
  device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                      reinterpret_cast<void**>(&fence.get()));
  queue->Signal(fence.get(), 1);
  fence->SetEventOnCompletion(1, nullptr);
}

inline DXPointer<ID3D12GraphicsCommandList> GetCommandList(
    ID3D12Device* device, ID3D12CommandAllocator* allocator) {
  DXPointer<ID3D12GraphicsCommandList> list;
  HRESULT hr = device->CreateCommandList(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr,
      __uuidof(ID3D12GraphicsCommandList), reinterpret_cast<void**>(&list.get()));
  EXPECT_EQ(S_OK, hr);
  return list;
}

inline DXPointer<ID3D12RootSignature> CreateRootSignature(ID3D12Device* device) {
  RootSignatureBuilder builder;
  return builder.commit(device);
}

inline DXPointer<ID3D12Resource> CreateUploadBuffer(ID3D12Device* device,
                                                    UINT bufferSize) {
  DXPointer<ID3D12Resource> buffer;
  D3D12_HEAP_PROPERTIES heapProps = {D3D12_HEAP_TYPE_UPLOAD,
                                     D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                                     D3D12_MEMORY_POOL_UNKNOWN, 1, 1};
  D3D12_RESOURCE_DESC desc = {D3D12_RESOURCE_DIMENSION_BUFFER,
                              0,
                              bufferSize,
                              1,
                              1,
                              1,
                              DXGI_FORMAT_UNKNOWN,
                              {1, 0},
                              D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                              D3D12_RESOURCE_FLAG_NONE};
  if (S_OK !=
      device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                                      D3D12_RESOURCE_STATE_GENERIC_READ,
                                      nullptr, IID_PPV_ARGS(&buffer.get()))) {
    return DXPointer<ID3D12Resource>();
  }
  return buffer;
}

struct triangle_data {
  DXPointer<ID3D12Resource> vertexBuffer;
  D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
  D3D12_INPUT_ELEMENT_DESC inputElementDesc;
};

struct Vertex {
  float pos[3];
};

inline triangle_data CreateTriangleVB(ID3D12Device* device) {
  const Vertex triangleVertices[] = {
      {0.0f, 0.5f, 0.0f}, {0.5f, -0.5f, 0.0f}, {-0.5f, -0.5f, 0.0f}};
  const UINT vertexBufferSize = sizeof(triangleVertices);

  triangle_data t;
  t.vertexBuffer = CreateUploadBuffer(device, vertexBufferSize);
  EXPECT_FALSE(!t.vertexBuffer);

  D3D12_RANGE readRange = {0, 0};  // We do not intend to read from this
                                   // resource on the CPU.
  UINT8* pVertexDataBegin;
  t.vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin));

  memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
  t.vertexBuffer->Unmap(0, nullptr);

  t.vertexBufferView = {};
  t.vertexBufferView.BufferLocation = t.vertexBuffer->GetGPUVirtualAddress();
  t.vertexBufferView.StrideInBytes = sizeof(Vertex);
  t.vertexBufferView.SizeInBytes = vertexBufferSize;

  t.inputElementDesc = {
      "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,
      0,          0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
      0};

  return t;
}
