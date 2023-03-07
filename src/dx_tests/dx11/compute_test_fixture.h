// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <dxgi.h>
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include "comparator.h"
#include "compute_data.h"
#include "dx_pointer.h"
#include "dxgi_formats.h"

template <DXGI_FORMAT FORMAT, size_t LENGTH>
using default_compute_comparator = strict_compute_comparator<FORMAT, LENGTH>;

template <DXGI_FORMAT FORMAT, size_t LENGTH,
          typename comparator = default_compute_comparator<FORMAT, LENGTH>>
class compute_test : public ::testing::Test {
 public:
  static const uint32_t kLength = LENGTH;
  static const size_t kDataSize = sizeof(typename format_to_type<FORMAT>::type) *
                                  format_to_type<FORMAT>::num_elements * LENGTH;

  static const DXGI_FORMAT kFormat = FORMAT;

 protected:
  void RunTest(const std::function<void(ID3D11Device*, ID3D11DeviceContext*,
                                        ID3D11Buffer* output)>& function) {
    DXPointer<ID3D11Device> device;
    {
      DXPointer<ID3D11DeviceContext> device_context;  // CreateD3D11DeviceContext

      D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
      HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
                                     D3D11_CREATE_DEVICE_DEBUG, &feature_level,
                                     1, D3D11_SDK_VERSION, &device.get(), NULL,
                                     &device_context.get());
      ASSERT_EQ(S_OK, hr);
      DXPointer<ID3D11Buffer> output;  // Create ID3D11Buffer with the specific size

      {
        D3D11_BUFFER_DESC bufferDesc = {kDataSize,
                                        D3D11_USAGE_DEFAULT,
                                        D3D11_BIND_UNORDERED_ACCESS,
                                        D3D11_CPU_ACCESS_READ,
                                        0,
                                        0};
        ASSERT_EQ(S_OK, device->CreateBuffer(&bufferDesc, NULL, &output.get()));
      }

      function(device.get(), device_context.get(), output.get());

      D3D11_MAPPED_SUBRESOURCE mapped_subres;
      ZeroMemory(&mapped_subres, sizeof(D3D11_MAPPED_SUBRESOURCE));
      hr = device_context->Map(output.get(), 0, D3D11_MAP_READ, 0, &mapped_subres);
      ASSERT_EQ(S_OK, hr);

      if (dx_tests::cmdline_args::update_images) {
        auto stored = store_data<FORMAT, LENGTH>(mapped_subres.pData, "dx11");
        ASSERT_TRUE(stored);
      } else {
        std::string err;
        auto dat = load_data<FORMAT, LENGTH>(&err, "dx11");
        ASSERT_NE(0, dat.size());
        comparator c;
        EXPECT_TRUE(c.compare(mapped_subres.pData, dat))
            << c.error_string() << std::endl;
      }
      device_context->Flush();
      device_context->ClearState();
    }
    // We add the ref here so we can release it on the next line and get the count
    device->AddRef();
    ASSERT_EQ(1u, device->Release()) << "If you are failing here, it is likely "
                                        "that you are leaking DX11 objects";
  }
};
