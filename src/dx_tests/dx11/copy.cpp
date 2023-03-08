// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <d3d11.h>
#include <d3dcommon.h>
#include <dxgicommon.h>
#include <dxgiformat.h>
#include <gtest/gtest.h>
#include <stddef.h>
#include <windows.h>
#include <functional>
#include <istream>
#include <string>

#include "dx_pointer.h"
#include "pixel_test_fixture.h"
#include "test_utils.h"

template <DXGI_FORMAT FORMAT, size_t WIDTH, size_t HEIGHT,
          size_t max_num_fuzzy_texels, size_t max_per_channel_texel_difference_percent>
struct fuzzy_image_comparator;

using copy = texel_test<DXGI_FORMAT_R32_FLOAT, 30, 30,
                        fuzzy_image_comparator<DXGI_FORMAT_R32_FLOAT, 30, 30, 105, 1>>;

const std::string g_vsCode =
    R"(
float4 main(uint id: SV_VertexID): SV_POSITION {
  if (id == 0)
    return float4(0.0, 0.5, 0.25, 1);
  if (id == 1)
    return float4(0.5, -0.5, 0.5, 1);
  if (id == 2)
    return float4(-0.5, -0.5, 1.0, 1);
  return float4(0, 0, 0, 0);
})";

TEST_F(copy, betweenaspects) {
  RunTest([](ID3D11Device* device, ID3D11DeviceContext* context,
             ID3D11Texture2D* texture) {
    DXPointer<ID3D11Texture2D> depth_texture;
    D3D11_TEXTURE2D_DESC desc;

    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32_TYPELESS;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;

    ASSERT_EQ(S_OK, device->CreateTexture2D(&desc, 0, &depth_texture.get()));

    DXPointer<ID3D11DepthStencilView> dsv;

    D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
    dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
    dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    dsv_desc.Texture2D.MipSlice = 0;

    ASSERT_EQ(S_OK, device->CreateDepthStencilView(depth_texture.get(),
                                                   &dsv_desc, &dsv.get()));

    ShaderOutput<ID3D11VertexShader> vs;

    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Vertex Shader", "vs_5_0",
                                         g_vsCode.c_str(), &vs));

    D3D11_DEPTH_STENCIL_DESC dsDesc;

    // Depth test parameters
    dsDesc.DepthEnable = true;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
    dsDesc.StencilEnable = false;
    dsDesc.StencilReadMask = 0xFF;
    dsDesc.StencilWriteMask = 0xFF;
    dsDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    dsDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
    dsDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    dsDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    dsDesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    dsDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR;
    dsDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    dsDesc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

    DXPointer<ID3D11DepthStencilState> dss;
    device->CreateDepthStencilState(&dsDesc, &dss.get());
    context->OMSetDepthStencilState(dss.get(), 1);

    D3D11_VIEWPORT omViewport;
    omViewport.TopLeftX = 0.0f;
    omViewport.TopLeftY = 0.0f;
    omViewport.Width = width;
    omViewport.Height = height;
    omViewport.MinDepth = 0.0f;
    omViewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &omViewport);

    context->ClearDepthStencilView(dsv.get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    context->OMSetRenderTargets(0, nullptr, dsv.get());
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vs.shader.get(), nullptr, 0);

    context->Draw(3, 0);

    context->OMSetRenderTargets(0, nullptr, nullptr);
    context->CopySubresourceRegion(texture, 0, 0, 0, 0, depth_texture.get(), 0,
                                   nullptr);
  });
}
