// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <d3d11.h>
#include <dxgicommon.h>
#include <dxgiformat.h>
#include <gtest/gtest.h>
#include <stddef.h>
#include <windows.h>
#include <functional>

#include "dx_pointer.h"
#include "pixel_test_fixture.h"

template <DXGI_FORMAT FORMAT, size_t WIDTH, size_t HEIGHT,
          size_t max_num_fuzzy_texels, size_t max_per_channel_texel_difference_percent>
struct fuzzy_image_comparator;

using mipmapgen =
    texel_test<DXGI_FORMAT_R8G8B8A8_UNORM, 16, 16,
               fuzzy_image_comparator<DXGI_FORMAT_R8G8B8A8_UNORM, 16, 16, 16, 2>>;

TEST_F(mipmapgen, basictexture) {
  RunTest([](ID3D11Device* device, ID3D11DeviceContext* context,
             ID3D11Texture2D* texture) {
    DXPointer<ID3D11Texture2D> temporary_texture;
    D3D11_TEXTURE2D_DESC texture_desc;
    ZeroMemory(&texture_desc, sizeof(D3D11_TEXTURE2D_DESC));
    texture_desc.Width = 64;
    texture_desc.Height = 64;
    texture_desc.MipLevels = 7;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.SampleDesc.Quality = 0;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    texture_desc.CPUAccessFlags = 0;
    texture_desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    HRESULT hr = device->CreateTexture2D(&texture_desc, NULL,
                                         &temporary_texture.get());
    ASSERT_EQ(S_OK, hr);

    unsigned char upload_data[64 * 64 * 4] = {};

    for (size_t i = 0; i < 64; ++i) {
      for (size_t j = 0; j < 64; ++j) {
        unsigned char* texel = upload_data + i * (64 * 4) + j * 4;
        if (i >= j) {
          texel[0] = 0xFF;
        } else {
          texel[1] = 0xFF;
        }
        texel[3] = 0xFF;
      }
    }

    context->UpdateSubresource(temporary_texture.get(), 0, nullptr, upload_data,
                               64 * 4, 0);

    DXPointer<ID3D11ShaderResourceView> srv;
    hr = device->CreateShaderResourceView(temporary_texture.get(), NULL,
                                          &srv.get());
    ASSERT_EQ(S_OK, hr);
    context->GenerateMips(srv.get());

    context->CopySubresourceRegion(texture, 0, 0, 0, 0, temporary_texture.get(),
                                   2, nullptr);
  });
}
