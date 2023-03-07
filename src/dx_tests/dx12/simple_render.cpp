// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <d3d12.h>
#include <dxgiformat.h>
#include <gtest/gtest.h>
#include <windows.h>
#include <functional>

#include "common_test_utils.h"
#include "dx_pointer.h"
#include "pixel_test_fixture.h"

using simplerender = render_test<DXGI_FORMAT_R8G8B8A8_UNORM, 10, 10>;

TEST_F(simplerender, basic) {
  RunTest([](IDXGIAdapter*, ID3D12Device* device, ID3D12CommandQueue* queue,
             ID3D12CommandAllocator* allocator,
             D3D12_CPU_DESCRIPTOR_HANDLE render_target_view) {
    DXPointer<ID3D12GraphicsCommandList> list;
    HRESULT hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           allocator, nullptr,
                                           __uuidof(ID3D12GraphicsCommandList),
                                           reinterpret_cast<void**>(&list.get()));
    ASSERT_EQ(S_OK, hr);
    FLOAT val[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    list->ClearRenderTargetView(render_target_view, val, 0, nullptr);
    list->Close();
    ID3D12CommandList* cl = list.get();
    queue->ExecuteCommandLists(1, &cl);
    wait_for_completion(queue);
  });
}
