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
#include "dx_pointer.h"
#include "dxgi_formats.h"
#include "fuzzy_comparator.h"
#include "neighborhood_comparator.h"

template <DXGI_FORMAT FORMAT, size_t WIDTH, size_t HEIGHT>
using default_image_comparator = strict_image_comparator<FORMAT, WIDTH, HEIGHT>;

template <DXGI_FORMAT FORMAT, size_t WIDTH, size_t HEIGHT,
          typename comparator = default_image_comparator<FORMAT, WIDTH, HEIGHT>>
class texel_test : public ::testing::Test {
 public:
  static const uint32_t width = WIDTH;
  static const uint32_t height = HEIGHT;

 protected:
  void RunTest(const std::function<void(ID3D11Device*, ID3D11DeviceContext*,
                                        ID3D11Texture2D* output)>& function) {
    DXPointer<ID3D11Device> device;
    {
      DXPointer<ID3D11DeviceContext> device_context;  // CreateD3D11DeviceContext

      D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
      HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
                                     D3D11_CREATE_DEVICE_DEBUG, &feature_level,
                                     1, D3D11_SDK_VERSION, &device.get(), NULL,
                                     &device_context.get());
      ASSERT_EQ(S_OK, hr);
      DXPointer<ID3D11Texture2D> output;  // Create ID3D11Texture2D with width,
                                          // height, size

      {
        D3D11_TEXTURE2D_DESC texture_desc;
        ZeroMemory(&texture_desc, sizeof(D3D11_TEXTURE2D_DESC));
        texture_desc.Width = WIDTH;
        texture_desc.Height = HEIGHT;
        texture_desc.MipLevels = 1;
        texture_desc.ArraySize = 1;
        texture_desc.Format = FORMAT;
        texture_desc.SampleDesc.Count = 1;
        texture_desc.SampleDesc.Quality = 0;
        texture_desc.Usage = D3D11_USAGE_STAGING;
        texture_desc.BindFlags = 0;
        texture_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        texture_desc.MiscFlags = 0;
        hr = device->CreateTexture2D(&texture_desc, NULL, &output.get());
        ASSERT_EQ(S_OK, hr);
      }

      function(device.get(), device_context.get(), output.get());

      D3D11_MAPPED_SUBRESOURCE mapped_subres;
      ZeroMemory(&mapped_subres, sizeof(D3D11_MAPPED_SUBRESOURCE));
      hr = device_context->Map(output.get(), 0, D3D11_MAP_READ, 0, &mapped_subres);
      ASSERT_EQ(S_OK, hr);

      if (dx_tests::cmdline_args::update_images) {
        auto ret = store_image<FORMAT, WIDTH, HEIGHT>(
            mapped_subres.pData, mapped_subres.RowPitch, "dx11");
        ASSERT_TRUE(ret);
      } else {
        std::string err;
        auto img = load_image<FORMAT, WIDTH, HEIGHT>(&err, "dx11");
        ASSERT_NE(nullptr, img.get());
        comparator c;
        EXPECT_TRUE(c.compare(mapped_subres.pData, mapped_subres.RowPitch, img.get()))
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

template <DXGI_FORMAT FORMAT, size_t WIDTH, size_t HEIGHT,
          typename comparator = default_image_comparator<FORMAT, WIDTH, HEIGHT>>
class render_test : public texel_test<FORMAT, WIDTH, HEIGHT, comparator> {
 protected:
  void RunTest(std::function<void(ID3D11Device*, ID3D11DeviceContext*,
                                  ID3D11RenderTargetView* view)>
                   function) {
    texel_test<FORMAT, WIDTH, HEIGHT, comparator>::RunTest(
        [&function](ID3D11Device* device, ID3D11DeviceContext* context,
                    ID3D11Texture2D* texture) {
          DXPointer<ID3D11Texture2D> temporary_texture;

          D3D11_TEXTURE2D_DESC texture_desc;
          ZeroMemory(&texture_desc, sizeof(D3D11_TEXTURE2D_DESC));
          texture_desc.Width = WIDTH;
          texture_desc.Height = HEIGHT;
          texture_desc.MipLevels = 1;
          texture_desc.ArraySize = 1;
          texture_desc.Format = FORMAT;
          texture_desc.SampleDesc.Count = 1;
          texture_desc.SampleDesc.Quality = 0;
          texture_desc.Usage = D3D11_USAGE_DEFAULT;
          texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
          texture_desc.CPUAccessFlags = 0;
          texture_desc.MiscFlags = 0;
          HRESULT hr = device->CreateTexture2D(&texture_desc, NULL,
                                               &temporary_texture.get());
          ASSERT_EQ(S_OK, hr);

          DXPointer<ID3D11RenderTargetView> rtv;
          hr = device->CreateRenderTargetView(temporary_texture.get(), NULL,
                                              &rtv.get());
          ASSERT_EQ(S_OK, hr);

          function(device, context, rtv.get());
          context->CopySubresourceRegion(texture, 0, 0, 0, 0,
                                         temporary_texture.get(), 0, nullptr);
        });
  }
};
