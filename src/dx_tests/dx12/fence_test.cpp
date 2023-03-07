// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <d3d12.h>
#include <d3dcommon.h>
#include <gtest/gtest.h>
#include <windows.h>
#include <atomic>
#include <chrono>
#include <thread>

#include "dx_pointer.h"

constexpr DWORD EVENT_WAIT_MS = 50;

class FenceTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    const HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0,
                                         IID_PPV_ARGS(&pDevice.get()));
    ASSERT_EQ(hr, S_OK);
  }

  void TearDown() override
  {
    if (!pDevice)
    {
      return;
    }
    //pDevice->AddRef();
    // If this does not equal 1 the test will leak the device.
    //EXPECT_EQ(pDevice->Release(), 1u);
  }

  DXPointer<ID3D12Device2> pDevice;

  HANDLE CreateTestEvent()
  {
    return CreateEventA(nullptr, false, false, nullptr);
  }

  DXPointer<ID3D12Fence> CreateTestFence()
  {
    DXPointer<ID3D12Fence> fence;
    HRESULT hr = pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                      __uuidof(ID3D12Fence),
                                      reinterpret_cast<void **>(&fence.get()));
    EXPECT_EQ(hr, S_OK);
    return fence;
  }
};

TEST_F(FenceTest, SetEventOnCompletion)
{
  HANDLE event = CreateTestEvent();

  DXPointer<ID3D12Fence> fence = CreateTestFence();
  fence->SetEventOnCompletion(2, event);
  EXPECT_EQ(WaitForSingleObject(event, EVENT_WAIT_MS),
            static_cast<DWORD>(WAIT_TIMEOUT));

  fence->Signal(1);
  EXPECT_EQ(WaitForSingleObject(event, EVENT_WAIT_MS),
            static_cast<DWORD>(WAIT_TIMEOUT));

  fence->Signal(2);
  EXPECT_EQ(WaitForSingleObject(event, EVENT_WAIT_MS), WAIT_OBJECT_0);
  // Reset by previous WaitForSingleObject, should not re-trigger.
  EXPECT_EQ(WaitForSingleObject(event, EVENT_WAIT_MS),
            static_cast<DWORD>(WAIT_TIMEOUT));

  fence->Signal(3);
  EXPECT_EQ(WaitForSingleObject(event, EVENT_WAIT_MS),
            static_cast<DWORD>(WAIT_TIMEOUT));

  CloseHandle(event);
}

TEST_F(FenceTest, MultipleEvents)
{
  HANDLE eventA = CreateTestEvent();
  HANDLE eventB = CreateTestEvent();

  DXPointer<ID3D12Fence> fence = CreateTestFence();
  fence->SetEventOnCompletion(1, eventA);
  fence->SetEventOnCompletion(2, eventB);

  fence->Signal(1);
  EXPECT_EQ(WaitForSingleObject(eventA, EVENT_WAIT_MS), WAIT_OBJECT_0);
  EXPECT_EQ(WaitForSingleObject(eventB, EVENT_WAIT_MS),
            static_cast<DWORD>(WAIT_TIMEOUT));

  fence->Signal(2);
  EXPECT_EQ(WaitForSingleObject(eventA, EVENT_WAIT_MS),
            static_cast<DWORD>(WAIT_TIMEOUT));
  EXPECT_EQ(WaitForSingleObject(eventB, EVENT_WAIT_MS), WAIT_OBJECT_0);

  fence->Signal(3);
  EXPECT_EQ(WaitForSingleObject(eventA, EVENT_WAIT_MS),
            static_cast<DWORD>(WAIT_TIMEOUT));
  EXPECT_EQ(WaitForSingleObject(eventB, EVENT_WAIT_MS),
            static_cast<DWORD>(WAIT_TIMEOUT));

  CloseHandle(eventA);
  CloseHandle(eventB);
}

TEST_F(FenceTest, MultipleEventsSameValue)
{
  HANDLE eventA = CreateTestEvent();
  HANDLE eventB = CreateTestEvent();

  DXPointer<ID3D12Fence> fence = CreateTestFence();
  fence->SetEventOnCompletion(2, eventA);
  fence->SetEventOnCompletion(2, eventB);

  fence->Signal(1);
  EXPECT_EQ(WaitForSingleObject(eventA, EVENT_WAIT_MS),
            static_cast<DWORD>(WAIT_TIMEOUT));
  EXPECT_EQ(WaitForSingleObject(eventB, EVENT_WAIT_MS),
            static_cast<DWORD>(WAIT_TIMEOUT));

  fence->Signal(2);
  EXPECT_EQ(WaitForSingleObject(eventA, EVENT_WAIT_MS), WAIT_OBJECT_0);
  EXPECT_EQ(WaitForSingleObject(eventB, EVENT_WAIT_MS), WAIT_OBJECT_0);

  fence->Signal(3);
  EXPECT_EQ(WaitForSingleObject(eventA, EVENT_WAIT_MS),
            static_cast<DWORD>(WAIT_TIMEOUT));
  EXPECT_EQ(WaitForSingleObject(eventB, EVENT_WAIT_MS),
            static_cast<DWORD>(WAIT_TIMEOUT));

  CloseHandle(eventA);
  CloseHandle(eventB);
}

TEST_F(FenceTest, SameEventMultipleValues)
{
  HANDLE event = CreateTestEvent();

  DXPointer<ID3D12Fence> fence = CreateTestFence();
  fence->SetEventOnCompletion(1, event);
  fence->SetEventOnCompletion(3, event);
  fence->SetEventOnCompletion(4, event);

  fence->Signal(1);
  EXPECT_EQ(WaitForSingleObject(event, EVENT_WAIT_MS), WAIT_OBJECT_0);

  fence->Signal(2);
  EXPECT_EQ(WaitForSingleObject(event, EVENT_WAIT_MS),
            static_cast<DWORD>(WAIT_TIMEOUT));

  fence->Signal(3);
  EXPECT_EQ(WaitForSingleObject(event, EVENT_WAIT_MS), WAIT_OBJECT_0);

  fence->Signal(4);
  EXPECT_EQ(WaitForSingleObject(event, EVENT_WAIT_MS), WAIT_OBJECT_0);

  fence->Signal(5);
  EXPECT_EQ(WaitForSingleObject(event, EVENT_WAIT_MS),
            static_cast<DWORD>(WAIT_TIMEOUT));

  CloseHandle(event);
}

TEST_F(FenceTest, CloseOriginalEventHandleBeforeFenceCompletion)
{
  HANDLE event = CreateTestEvent();
  HANDLE duplicateHandle;
  ASSERT_TRUE(DuplicateHandle(GetCurrentProcess(), event, GetCurrentProcess(),
                              &duplicateHandle, 0, false, DUPLICATE_SAME_ACCESS));

  DXPointer<ID3D12Fence> fence = CreateTestFence();
  fence->SetEventOnCompletion(1, event);

  CloseHandle(event);

  fence->Signal(1);
  EXPECT_EQ(WaitForSingleObject(duplicateHandle, EVENT_WAIT_MS), WAIT_OBJECT_0);

  CloseHandle(duplicateHandle);
}

TEST_F(FenceTest, WaitInSetEventOnCompletion)
{
  DXPointer<ID3D12Fence> fence = CreateTestFence();
  std::atomic_bool completed = false;
  std::thread t([&fence, &completed]()
                {
                  fence->SetEventOnCompletion(1, nullptr);
                  completed = true;
                });

  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  EXPECT_FALSE(completed);

  fence->Signal(1);
  t.join();
  EXPECT_TRUE(completed);
}

TEST_F(FenceTest, SetEventOnCompletionWithCurrentValue)
{
  HANDLE event = CreateTestEvent();

  DXPointer<ID3D12Fence> fence = CreateTestFence();
  fence->Signal(1);

  EXPECT_EQ(fence->SetEventOnCompletion(1, nullptr), S_OK);

  fence->SetEventOnCompletion(1, event);
  EXPECT_EQ(WaitForSingleObject(event, EVENT_WAIT_MS), WAIT_OBJECT_0);

  CloseHandle(event);
}

TEST_F(FenceTest, TriggersSkippedEvents)
{
  HANDLE eventA = CreateTestEvent();
  HANDLE eventB = CreateTestEvent();
  HANDLE eventC = CreateTestEvent();

  DXPointer<ID3D12Fence> fence = CreateTestFence();

  fence->SetEventOnCompletion(1, eventA);
  fence->SetEventOnCompletion(2, eventB);
  fence->SetEventOnCompletion(4, eventC);
  // Sleep long enough so the D3D12FenceEventManager can update it VkSemaphore
  // wait. This will make sure it wakes up even though we're not actually
  // waiting for the specific semaphore value.
  std::this_thread::sleep_for(std::chrono::milliseconds(EVENT_WAIT_MS));

  fence->Signal(3);
  EXPECT_EQ(WaitForSingleObject(eventA, EVENT_WAIT_MS), WAIT_OBJECT_0);
  EXPECT_EQ(WaitForSingleObject(eventB, EVENT_WAIT_MS), WAIT_OBJECT_0);
  EXPECT_EQ(WaitForSingleObject(eventC, EVENT_WAIT_MS),
            static_cast<DWORD>(WAIT_TIMEOUT));

  fence->Signal(5);
  EXPECT_EQ(WaitForSingleObject(eventC, EVENT_WAIT_MS), WAIT_OBJECT_0);
  // Should not re-trigger
  EXPECT_EQ(WaitForSingleObject(eventA, EVENT_WAIT_MS),
            static_cast<DWORD>(WAIT_TIMEOUT));
  EXPECT_EQ(WaitForSingleObject(eventB, EVENT_WAIT_MS),
            static_cast<DWORD>(WAIT_TIMEOUT));

  CloseHandle(eventA);
  CloseHandle(eventB);
}

TEST_F(FenceTest, DestroyFenceWithUntriggeredEvent)
{
  HANDLE event = CreateTestEvent();
  DXPointer<ID3D12Fence> fence = CreateTestFence();

  fence->SetEventOnCompletion(1, event);
  CloseHandle(event);
}
