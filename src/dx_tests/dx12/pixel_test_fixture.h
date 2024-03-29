// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <gtest/gtest.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>
#include "commandline_args.h"
#include "common_test_utils.h"
#include "comparator.h"
#include "dx_pointer.h"
#include "dxgi_formats.h"
#include "fuzzy_comparator.h"
#include "neighborhood_comparator.h"

inline bool ProcessEvents()
{
  MSG msg;
  while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
  {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  if (msg.message == WM_QUIT)
  {
    return false;
  }
  return true;
}

inline LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam,
                                   LPARAM lParam)
{
  switch (message)
  {
  case WM_KEYDOWN:
  {
    if (wParam == VK_ESCAPE || wParam == VK_RETURN)
    {
      PostQuitMessage(0);
      return 0;
    }
  }
  case WM_DESTROY:
  {
    PostQuitMessage(0);
    return 0;
  }
  break;
  case WM_CLOSE:
  {
    PostQuitMessage(0);
    return 0;
  }
  break;
  }
  return DefWindowProc(hWnd, message, wParam, lParam);
}

template <DXGI_FORMAT FORMAT, size_t WIDTH, size_t HEIGHT>
using default_image_comparator = strict_image_comparator<FORMAT, WIDTH, HEIGHT>;

template <DXGI_FORMAT FORMAT, size_t WIDTH, size_t HEIGHT,
          typename comparator = default_image_comparator<FORMAT, WIDTH, HEIGHT>,
          typename PARAMETER = void>
class texel_test
    : public std::conditional_t<std::is_void<PARAMETER>::value, ::testing::Test,
                                ::testing::TestWithParam<PARAMETER>>
{
public:
  static const uint32_t width = WIDTH;
  static const uint32_t height = HEIGHT;
  static const DXGI_FORMAT format = FORMAT;

  const D3D12_BLEND_DESC defaultBlendDesc = {
      FALSE,
      FALSE,
      {{FALSE, FALSE, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        D3D12_LOGIC_OP_NOOP, D3D12_COLOR_WRITE_ENABLE_ALL}}};

  const D3D12_RASTERIZER_DESC defaultRasterDesc = {
      D3D12_FILL_MODE_SOLID,
      D3D12_CULL_MODE_BACK,
      FALSE,
      D3D12_DEFAULT_DEPTH_BIAS,
      D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
      D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS,
      TRUE,
      FALSE,
      FALSE,
      0,
      D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF};

  const D3D12_DEPTH_STENCIL_DESC defaultDepthStencil = {
      false,
      D3D12_DEPTH_WRITE_MASK_ALL,
      D3D12_COMPARISON_FUNC_LESS,
      false,
      D3D12_DEFAULT_STENCIL_READ_MASK,
      D3D12_DEFAULT_STENCIL_WRITE_MASK,
      {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP,
       D3D12_COMPARISON_FUNC_ALWAYS},
      {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP,
       D3D12_COMPARISON_FUNC_ALWAYS}};

  // At a minmum, clients must fill in pRootSignature, shaders as needed, and InputLayout
  const D3D12_GRAPHICS_PIPELINE_STATE_DESC defaultPsoDesc = {
      nullptr /* pRootSignature */,
      {},
      {},
      {},
      {},
      {} /* shaders */,
      {} /* StreamOutput */,
      defaultBlendDesc,
      UINT_MAX /* sampleMask */,
      defaultRasterDesc,
      defaultDepthStencil,
      {} /* InputLayout */,
      D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,
      D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
      1 /* NumRenderTargets */,
      {DXGI_FORMAT_R8G8B8A8_UNORM} /* RTVFormat */,
      {} /* DSVFormat */,
      {1, 0} /* SampleDesc */,
      0 /* NodeMask */,
      {} /* CachedPSO */,
      D3D12_PIPELINE_STATE_FLAG_NONE};

  const D3D12_VIEWPORT defaultViewport = {0.0f, 0.0f, static_cast<float>(width),
                                          static_cast<float>(height)};
  const D3D12_RECT defaultScissorRect = {0, 0, static_cast<LONG>(width),
                                         static_cast<LONG>(height)};

  void RunTest(
      const std::function<void(IDXGIAdapter *, ID3D12Device *, ID3D12CommandQueue *,
                               ID3D12CommandAllocator *, ID3D12Resource *)> &function)
  {
    HRESULT hr = S_OK;

    // TODO(renatopereyra): Re-enable if Proton/VKD3D-Proton ever support DX12 debug layers
    //
    // DXPointer<ID3D12Debug> debug_controller;
    // hr = D3D12GetDebugInterface(
    //     __uuidof(ID3D12Debug), reinterpret_cast<void **>(&debug_controller.get()));
    // ASSERT_EQ(S_OK, hr);
    // debug_controller->EnableDebugLayer();

    DXPointer<IDXGIFactory2> factory;
    hr = CreateDXGIFactory(__uuidof(IDXGIFactory2),
                           reinterpret_cast<void **>(&factory.get()));

    DXPointer<IDXGIAdapter> adapter;
    hr = factory->EnumAdapters(0, &adapter.get());

    DXPointer<ID3D12Device2> device;
    hr = D3D12CreateDevice(adapter.get(), D3D_FEATURE_LEVEL_12_0,
                           __uuidof(ID3D12Device2),
                           reinterpret_cast<void **>(&device.get()));
    ASSERT_EQ(S_OK, hr);

    // This block ensures that all objects created from the device
    // will be released so we can check the reference count on the
    // device at the end of the method.
    {
      DXPointer<ID3D12CommandQueue> queue;
      D3D12_COMMAND_QUEUE_DESC queue_desc{
          D3D12_COMMAND_LIST_TYPE_DIRECT,    // Type
          D3D12_COMMAND_QUEUE_PRIORITY_HIGH, // Prioirty
          D3D12_COMMAND_QUEUE_FLAG_NONE,     // Flags
          0                                  // NodeMask
      };

      hr = device->CreateCommandQueue(&queue_desc, __uuidof(ID3D12CommandQueue),
                                      reinterpret_cast<void **>(&queue.get()));
      ASSERT_EQ(S_OK, hr);

      D3D12_RESOURCE_DESC image_create_info = {
          D3D12_RESOURCE_DIMENSION_TEXTURE2D, // Dimension
          0,                                  // Alignment
          width,                              // Width
          height,                             // Height
          1,                                  // DepthOrArraySlice
          1,                                  // MipLevels
          format,                             // Format
          {
              1,                        // Count
              0,                        // Quality
          },                            // SampleDesc
          D3D12_TEXTURE_LAYOUT_UNKNOWN, // Layout
          D3D12_RESOURCE_FLAG_NONE,     // Flags
      };

      D3D12_HEAP_PROPERTIES image_heap_props{
          D3D12_HEAP_TYPE_DEFAULT,         // Type
          D3D12_CPU_PAGE_PROPERTY_UNKNOWN, // CPUPageProperty
          D3D12_MEMORY_POOL_UNKNOWN,       // MemoryPoolPreference
          0,                               // CreationNodeMask
          0,                               // VisibleNodeMask
      };

      DXPointer<ID3D12Resource> output;
      hr = device->CreateCommittedResource(
          &image_heap_props, D3D12_HEAP_FLAG_NONE, &image_create_info,
          D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource),
          reinterpret_cast<void **>(&output.get()));
      ASSERT_EQ(S_OK, hr);

      uint64_t row_pitch = sizeof(typename format_to_type<FORMAT>::type) *
                           format_to_type<FORMAT>::num_elements * width;
      row_pitch = (row_pitch + (D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)) &
                  ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);

      uint64_t required_size = row_pitch * height;

      D3D12_RESOURCE_DESC buffer_create_info = {
          D3D12_RESOURCE_DIMENSION_BUFFER, // Dimension
          0,                               // Alignment
          required_size,                   // Width
          1,                               // Height
          1,                               // DepthOrArraySlice,
          1,                               // MipLevels
          DXGI_FORMAT_UNKNOWN,
          {
              1,                          // Count
              0,                          // Quality
          },                              // SampleDesc
          D3D12_TEXTURE_LAYOUT_ROW_MAJOR, // Layout
          D3D12_RESOURCE_FLAG_NONE,       // Flags
      };

      D3D12_HEAP_PROPERTIES buffer_heap_props{
          D3D12_HEAP_TYPE_READBACK,        // Type
          D3D12_CPU_PAGE_PROPERTY_UNKNOWN, // CPUPageProperty
          D3D12_MEMORY_POOL_UNKNOWN,       // MemoryPoolPreference
          0,                               // CreationNodeMask
          0,                               // VisibleNodeMask
      };

      DXPointer<ID3D12Resource> buffer;
      hr = device->CreateCommittedResource(
          &buffer_heap_props, D3D12_HEAP_FLAG_NONE, &buffer_create_info,
          D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource),
          reinterpret_cast<void **>(&buffer.get()));
      ASSERT_EQ(S_OK, hr);

      DXPointer<ID3D12CommandAllocator> command_allocator;
      hr = device->CreateCommandAllocator(
          D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
          reinterpret_cast<void **>(&command_allocator));
      ASSERT_EQ(S_OK, hr);

      if (dx_tests::cmdline_args::display)
      {
        ASSERT_EQ(S_OK, hr);
        DXPointer<IDXGISwapChain1> swapchain;
        DXPointer<IDXGISwapChain3> swap3;
        HWND hwnd;

        WNDCLASSEXA wc;
        ZeroMemory(&wc, sizeof(WNDCLASSEXA));
        wc.cbSize = sizeof(WNDCLASSEXA);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &WindowProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)COLOR_WINDOW;
        wc.lpszClassName = "Dx12UnitTests";

        RegisterClassExA(&wc);

        hwnd = CreateWindowExA(NULL,
                               "Dx12UnitTests",       // name of the window class
                               "Dx12UnitTests",       // title of the window
                               WS_OVERLAPPEDWINDOW,   // window style
                               0,                     // x-position of the window
                               0,                     // y-position of the window
                               200,                   // width of the window
                               200,                   // height of the window
                               NULL,                  // we have no parent window, NULL
                               NULL,                  // we aren't using menus, NULL
                               GetModuleHandle(NULL), // application handle
                               NULL);                 // used with multiple windows, NULL
        if (!hwnd)
        {
          hr = GetLastError();
          ASSERT_EQ(0, hr);
        }
        ShowWindow(hwnd, TRUE);

        DXGI_SWAP_CHAIN_DESC1 swap_create_info = {
            width,  // Width
            height, // Height
            format, // Format
            FALSE,  // Stereo
            {
                1, // Count
                0, // Quality
            },     // SampleDesc
            DXGI_USAGE_BACK_BUFFER,
            2,                             // BufferCount
            DXGI_SCALING_STRETCH,          // Scaling
            DXGI_SWAP_EFFECT_FLIP_DISCARD, // Swap Effect
            DXGI_ALPHA_MODE_IGNORE,        // Alpha mode
            0                              // Flags
        };

        if (!ProcessEvents())
        {
          return;
        }
        hr = factory->CreateSwapChainForHwnd(queue.get(), hwnd, &swap_create_info,
                                             nullptr, nullptr, &swapchain.get());
        ASSERT_EQ(S_OK, hr);
        hr = swapchain->QueryInterface(__uuidof(IDXGISwapChain3),
                                       reinterpret_cast<void **>(&swap3.get()));
        ASSERT_EQ(S_OK, hr);
        while (ProcessEvents())
        {
          function(adapter.get(), device.get(), queue.get(), command_allocator.get(),
                   output.get());
          if (::testing::Test::IsSkipped())
          {
            return;
          }
          UINT bbIdx = swap3->GetCurrentBackBufferIndex();
          ID3D12Resource *res;
          hr = swapchain->GetBuffer(bbIdx, __uuidof(ID3D12Resource),
                                    reinterpret_cast<void **>(&res));
          ASSERT_EQ(S_OK, hr);

          DXPointer<ID3D12GraphicsCommandList> copy_list;
          hr = device->CreateCommandList(
              0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator.get(),
              nullptr, __uuidof(ID3D12GraphicsCommandList),
              reinterpret_cast<void **>(&copy_list.get()));

          D3D12_RESOURCE_BARRIER barriers[] = {
              {D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, // Type
               D3D12_RESOURCE_BARRIER_FLAG_NONE,       // Flags
               {D3D12_RESOURCE_TRANSITION_BARRIER{
                   res,                            // pResource
                   0,                              // subresource
                   D3D12_RESOURCE_STATE_COMMON,    //
                   D3D12_RESOURCE_STATE_COPY_DEST, //
               }}},
              {D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, // Type
               D3D12_RESOURCE_BARRIER_FLAG_NONE,       // Flags
               {D3D12_RESOURCE_TRANSITION_BARRIER{
                   output.get(),                     // pResource
                   0,                                // subresource
                   D3D12_RESOURCE_STATE_COPY_DEST,   //
                   D3D12_RESOURCE_STATE_COPY_SOURCE, //
               }}}};
          copy_list->ResourceBarrier(2, barriers);
          D3D12_TEXTURE_COPY_LOCATION dest = {
              res,
              D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, // Type
              {{0}}};

          D3D12_TEXTURE_COPY_LOCATION source = {
              output.get(),
              D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, // Type
              {{0}}};
          copy_list->CopyTextureRegion(&dest, 0, 0, 0, &source, nullptr);

          D3D12_RESOURCE_BARRIER barriers2[] = {
              {D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, // Type
               D3D12_RESOURCE_BARRIER_FLAG_NONE,       // Flags
               {D3D12_RESOURCE_TRANSITION_BARRIER{
                   res,                            // pResource
                   0,                              // subresource
                   D3D12_RESOURCE_STATE_COPY_DEST, // StateBefore
                   D3D12_RESOURCE_STATE_PRESENT,   // StateAfter
               }}},
              {D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, // Type
               D3D12_RESOURCE_BARRIER_FLAG_NONE,       // Flags
               {D3D12_RESOURCE_TRANSITION_BARRIER{
                   output.get(),                     // pResource
                   0,                                // subresource
                   D3D12_RESOURCE_STATE_COPY_SOURCE, // StateBefore
                   D3D12_RESOURCE_STATE_COPY_DEST,   // StateAfter
               }}}};
          copy_list->ResourceBarrier(2, barriers2);
          copy_list->Close();
          ID3D12CommandList *cl = copy_list.get();
          queue->ExecuteCommandLists(1, &cl);
          wait_for_completion(queue.get());
          swapchain->Present(1, 0);
        }
        return;
      }
      else
      {
        function(adapter.get(), device.get(), queue.get(), command_allocator.get(), output.get());
        if (::testing::Test::IsSkipped())
        {
          return;
        }
      }

      DXPointer<ID3D12GraphicsCommandList> list;
      hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                     command_allocator.get(), nullptr,
                                     __uuidof(ID3D12GraphicsCommandList),
                                     reinterpret_cast<void **>(&list.get()));
      ASSERT_EQ(S_OK, hr);

      D3D12_RESOURCE_BARRIER barrier{
          D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, // Type
          D3D12_RESOURCE_BARRIER_FLAG_NONE,       // Flags
          {D3D12_RESOURCE_TRANSITION_BARRIER{
              output.get(),                     // pResource
              0,                                // subresource
              D3D12_RESOURCE_STATE_COMMON,      // StateBefore
              D3D12_RESOURCE_STATE_COPY_SOURCE, // StateAfter
          }}};
      list->ResourceBarrier(1, &barrier);

      D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
      footprint.Footprint.Width = width;
      footprint.Footprint.Height = height;
      footprint.Footprint.Depth = 1;
      footprint.Footprint.Format = format;
      footprint.Footprint.RowPitch = static_cast<UINT>(row_pitch);
      D3D12_TEXTURE_COPY_LOCATION dest = {
          buffer.get(),                             // pResource
          D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, // Type
          {footprint}                               // Footprint
      };

      D3D12_TEXTURE_COPY_LOCATION source = {
          output.get(),
          D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, // Type
          {{0}}};
      list->CopyTextureRegion(&dest, 0, 0, 0, &source, nullptr);

      list->Close();

      ID3D12CommandList *out_list = list.get();
      queue->ExecuteCommandLists(1, &out_list);
      wait_for_completion(queue.get());

      void *data;
      hr = buffer->Map(0, nullptr, &data);
      ASSERT_EQ(S_OK, hr);

      if (dx_tests::cmdline_args::update_images)
      {
        auto ret = store_image<FORMAT, WIDTH, HEIGHT>(
            data, footprint.Footprint.RowPitch, "dx12");
        ASSERT_TRUE(ret);
      }
      else
      {
        std::string err;
        auto img = load_image<FORMAT, WIDTH, HEIGHT>(&err, "dx12");
        ASSERT_NE(nullptr, img.get());
        comparator c;
        EXPECT_TRUE(c.compare(data, footprint.Footprint.RowPitch, img.get()))
            << c.error_string() << std::endl;
      }
    }

    // If this fails we will leak the device!
    //device->AddRef();
    //EXPECT_EQ(device->Release(), 1u);
  }
};

template <DXGI_FORMAT FORMAT, size_t WIDTH, size_t HEIGHT,
          typename comparator = default_image_comparator<FORMAT, WIDTH, HEIGHT>,
          typename PARAMETER = void>
class render_test
    : public texel_test<FORMAT, WIDTH, HEIGHT, comparator, PARAMETER>
{
public:
  static const DXGI_FORMAT format = FORMAT;
  static const size_t width = WIDTH;
  static const size_t height = HEIGHT;

  void RunTest(const std::function<void(IDXGIAdapter *, ID3D12Device *, ID3D12CommandQueue *, ID3D12CommandAllocator *,
                                        D3D12_CPU_DESCRIPTOR_HANDLE)> &function)
  {
    texel_test<FORMAT, WIDTH, HEIGHT, comparator, PARAMETER>::RunTest(
        [&function](IDXGIAdapter *adapter, ID3D12Device *device,
                    ID3D12CommandQueue *queue,
                    ID3D12CommandAllocator *allocator, ID3D12Resource *resource)
        {
          D3D12_RESOURCE_DESC render_target_create_info = {
              D3D12_RESOURCE_DIMENSION_TEXTURE2D, // Dimension
              0,                                  // Alignment
              width,                              // Width
              height,                             // Height
              1,                                  // DepthOrArraySlice
              1,                                  // MipLevels
              format,                             // Format
              {
                  1,                                   // Count
                  0,                                   // Quality
              },                                       // SampleDesc
              D3D12_TEXTURE_LAYOUT_UNKNOWN,            // Layout
              D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, // Flags
          };

          D3D12_HEAP_PROPERTIES rt_heap_props{
              D3D12_HEAP_TYPE_DEFAULT,         // Type
              D3D12_CPU_PAGE_PROPERTY_UNKNOWN, // CPUPageProperty
              D3D12_MEMORY_POOL_UNKNOWN,       // MemoryPoolPreference
              0,                               // CreationNodeMask
              0,                               // VisibleNodeMask
          };

          D3D12_CLEAR_VALUE clear = {};
          clear.Format = format;
          DXPointer<ID3D12Resource> render_target;
          HRESULT hr = device->CreateCommittedResource(
              &rt_heap_props, D3D12_HEAP_FLAG_NONE, &render_target_create_info,
              D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, __uuidof(ID3D12Resource),
              reinterpret_cast<void **>(&render_target.get()));
          ASSERT_EQ(S_OK, hr);

          DXPointer<ID3D12DescriptorHeap> heap;
          D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
          heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
          heap_desc.NodeMask = 0;
          heap_desc.NumDescriptors = 1;
          heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

          hr = device->CreateDescriptorHeap(
              &heap_desc, __uuidof(ID3D12DescriptorHeap),
              reinterpret_cast<void **>(&heap.get()));
          ASSERT_EQ(S_OK, hr);

          auto handle = heap->GetCPUDescriptorHandleForHeapStart();

          D3D12_RENDER_TARGET_VIEW_DESC rtv_desc;
          rtv_desc.Format = format;
          rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
          rtv_desc.Texture2D = D3D12_TEX2D_RTV{0, 0};

          device->CreateRenderTargetView(render_target.get(), &rtv_desc, handle);

          function(adapter, device, queue, allocator, handle);
          if (::testing::Test::IsSkipped())
          {
            return;
          }
          DXPointer<ID3D12GraphicsCommandList> list;
          hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         allocator, nullptr,
                                         __uuidof(ID3D12GraphicsCommandList),
                                         reinterpret_cast<void **>(&list.get()));
          ASSERT_EQ(S_OK, hr);

          D3D12_RESOURCE_BARRIER barriers[]{
              {D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, // Type
               D3D12_RESOURCE_BARRIER_FLAG_NONE,       // Flags
               {D3D12_RESOURCE_TRANSITION_BARRIER{
                   render_target.get(),                // pResource
                   0,                                  // subresource
                   D3D12_RESOURCE_STATE_RENDER_TARGET, // StateBefore
                   D3D12_RESOURCE_STATE_COPY_SOURCE,   // StateAfter
               }}},
              {D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, // Type
               D3D12_RESOURCE_BARRIER_FLAG_NONE,       // Flags
               {D3D12_RESOURCE_TRANSITION_BARRIER{
                   resource,                       // pResource
                   0,                              // subresource
                   D3D12_RESOURCE_STATE_COMMON,    // StateBefore
                   D3D12_RESOURCE_STATE_COPY_DEST, // StateAfter
               }}}};
          list->ResourceBarrier(2, barriers);

          D3D12_TEXTURE_COPY_LOCATION dest = {
              resource,                                  // pResource
              D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, // Type
              {{0}}};

          D3D12_TEXTURE_COPY_LOCATION source = {
              render_target.get(),
              D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, // Type
              {{0}}};
          list->CopyTextureRegion(&dest, 0, 0, 0, &source, nullptr);
          D3D12_RESOURCE_BARRIER barrier{
              D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, // Type
              D3D12_RESOURCE_BARRIER_FLAG_NONE,       // Flags
              {D3D12_RESOURCE_TRANSITION_BARRIER{
                  resource,                       // pResource
                  0,                              // subresource
                  D3D12_RESOURCE_STATE_COPY_DEST, // StateBefore
                  D3D12_RESOURCE_STATE_COMMON,    // StateAfter
              }}};
          list->ResourceBarrier(1, &barrier);
          list->Close();

          ID3D12CommandList *out_list = list.get();
          queue->ExecuteCommandLists(1, &out_list);
          wait_for_completion(queue);
        });
  }
};
