// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <d3d12.h>
#include <d3dcommon.h>
#include <gtest/gtest.h>
#include <windows.h>

#include "dx_pointer.h"

////////////////////////////////////////////////////////////////////////////////

// Fixture
class CommandListTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    {
      const HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0,
                                           __uuidof(ID3D12Device4),
                                           reinterpret_cast<void **>(&pDevice));
      ASSERT_EQ(hr, S_OK);
    }
    {
      const HRESULT hr = CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                &pCommandAllocator.get());
      ASSERT_EQ(hr, S_OK);
    }
    {
      const HRESULT hr = CreateCommandList1(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            &pCommandList.get());
      ASSERT_EQ(hr, S_OK);
    }
  }

  HRESULT CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE type,
                                 ID3D12CommandAllocator **ppAllocator)
  {
    return pDevice->CreateCommandAllocator(type, __uuidof(ID3D12CommandAllocator),
                                           reinterpret_cast<void **>(ppAllocator));
  }

  HRESULT CreateCommandList1(D3D12_COMMAND_LIST_TYPE type,
                             ID3D12GraphicsCommandList **ppCommandList)
  {
    return pDevice->CreateCommandList1(0, type, D3D12_COMMAND_LIST_FLAG_NONE,
                                       __uuidof(ID3D12GraphicsCommandList),
                                       reinterpret_cast<void **>(ppCommandList));
  }

  DXPointer<ID3D12Device4> pDevice;
  DXPointer<ID3D12CommandAllocator> pCommandAllocator;
  DXPointer<ID3D12GraphicsCommandList> pCommandList;
};

////////////////////////////////////////////////////////////////////////////////

// An allocator cannot be reset when there is a list in the recording state
TEST_F(CommandListTest, CommandAllocatorResetWhenListRecording)
{
  HRESULT hr;

  hr = pCommandList->Reset(pCommandAllocator.get(), nullptr);
  ASSERT_EQ(hr, S_OK);

  hr = pCommandAllocator->Reset();
  ASSERT_EQ(hr, E_FAIL);

  hr = pCommandList->Close();
  ASSERT_EQ(hr, S_OK);
  hr = pCommandAllocator->Reset();
  ASSERT_EQ(hr, S_OK);
}

////////////////////////////////////////////////////////////////////////////////

// A list in the recording state cannot be reset
TEST_F(CommandListTest, CommandListResetWhenRecording)
{
  HRESULT hr;

  hr = pCommandList->Reset(pCommandAllocator.get(), nullptr);
  ASSERT_EQ(hr, S_OK);

  hr = pCommandList->Reset(pCommandAllocator.get(), nullptr);
  ASSERT_EQ(hr, E_FAIL);

  hr = pCommandList->Close();
  ASSERT_EQ(hr, S_OK);
}

////////////////////////////////////////////////////////////////////////////////

// An allocator can only have a single list recording at the same time
TEST_F(CommandListTest, CommandAllocatorOnlySingleRecordingList)
{
  HRESULT hr;

  DXPointer<ID3D12GraphicsCommandList> pSecondCommandList;
  hr = CreateCommandList1(D3D12_COMMAND_LIST_TYPE_DIRECT,
                          &pSecondCommandList.get());
  ASSERT_EQ(hr, S_OK);

  hr = pCommandList->Reset(pCommandAllocator.get(), nullptr);
  ASSERT_EQ(hr, S_OK);

  hr = pSecondCommandList->Reset(pCommandAllocator.get(), nullptr);
  ASSERT_EQ(hr, E_INVALIDARG);

  hr = pCommandList->Close();
  ASSERT_EQ(hr, S_OK);
  hr = pSecondCommandList->Reset(pCommandAllocator.get(), nullptr);
  ASSERT_EQ(hr, S_OK);
  hr = pSecondCommandList->Close();
  ASSERT_EQ(hr, S_OK);
}

////////////////////////////////////////////////////////////////////////////////

// A list cannot be closed if it is not recording
TEST_F(CommandListTest, CommandListCloseNotRecordingList)
{
  HRESULT hr;

  hr = pCommandList->Close();
  ASSERT_EQ(hr, E_FAIL);

  hr = pCommandList->Reset(pCommandAllocator.get(), nullptr);
  ASSERT_EQ(hr, S_OK);
  hr = pCommandList->Close();
  ASSERT_EQ(hr, S_OK);

  hr = pCommandList->Close();
  ASSERT_EQ(hr, E_FAIL);
}

////////////////////////////////////////////////////////////////////////////////

// A list can be reassigned to another allocator
TEST_F(CommandListTest, CommandListSwitchAllocator)
{
  HRESULT hr;

  DXPointer<ID3D12CommandAllocator> pSecondCommandAllocator;
  hr = CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                              &(pSecondCommandAllocator.get()));
  ASSERT_EQ(hr, S_OK);

  hr = pCommandList->Reset(pCommandAllocator.get(), nullptr);
  ASSERT_EQ(hr, S_OK);
  hr = pCommandList->Close();
  ASSERT_EQ(hr, S_OK);

  hr = pCommandList->Reset(pSecondCommandAllocator.get(), nullptr);
  ASSERT_EQ(hr, S_OK);
  hr = pCommandList->Close();
  ASSERT_EQ(hr, S_OK);
}
