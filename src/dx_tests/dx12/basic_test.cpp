// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <d3d12.h>
#include <d3dcommon.h>
#include <dxgi1_6.h>
#include <gtest/gtest.h>
#include <windows.h>

#include "dx_pointer.h"

TEST(basicDx12, device) {
  DXPointer<IDXGIFactory2> pFactory;
  HRESULT hr = CreateDXGIFactory(__uuidof(IDXGIFactory2),
                                 reinterpret_cast<void**>(&pFactory.get()));
  ASSERT_EQ(hr, S_OK);

  DXPointer<IDXGIAdapter1> pAdapter;
  hr = pFactory->EnumAdapters1(0, &pAdapter.get());
  ASSERT_EQ(hr, S_OK);

  DXGI_ADAPTER_DESC1 desc = {};
  hr = pAdapter->GetDesc1(&desc);
  ASSERT_EQ(hr, S_OK);

  DXPointer<ID3D12Device2> pDevice;
  hr = D3D12CreateDevice(pAdapter.get(), D3D_FEATURE_LEVEL_12_0,
                         __uuidof(ID3D12Device2),
                         reinterpret_cast<void**>(&pDevice.get()));
  ASSERT_EQ(hr, S_OK);
  // Make sure devices can be destroyed.
  //EXPECT_EQ(pDevice->AddRef(), 2u);
  //EXPECT_EQ(pDevice->Release(), 1u);
}

TEST(basicDx12, deviceNoFactory) {
  DXPointer<ID3D12Device2> pDevice;
  HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0,
                                 __uuidof(ID3D12Device2),
                                 reinterpret_cast<void**>(&pDevice.get()));
  ASSERT_EQ(hr, S_OK);
  // Make sure devices can be destroyed.
  //EXPECT_EQ(pDevice->AddRef(), 2u);
  //EXPECT_EQ(pDevice->Release(), 1u);
}
