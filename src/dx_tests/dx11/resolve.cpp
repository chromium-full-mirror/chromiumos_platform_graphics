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

using resolve = texel_test<DXGI_FORMAT_R8G8B8A8_UNORM, 30, 30,
                           fuzzy_image_comparator<DXGI_FORMAT_R8G8B8A8_UNORM, 30, 30,
                                                  static_cast<size_t>(~0), 10>>;

const std::string g_vsCode =
    R"(
float4 main(uint id: SV_VertexID): SV_POSITION {
  if (id == 0)
    return float4(-0.5, -0.5, 0.5, 1.0);
  if (id == 1)
    return float4(-0.5, 0.5, 0.5, 1.0);
  if (id == 2)
    return float4(0.5, -0.5, 0.5, 1.0);
  if (id == 3)
    return float4(0.5, 0.5, 0.5, 1.0);
  return float4(0, 0, 0, 0);
 })";

const std::string g_psCode =
    "struct PS_IN {\n"
    "  float4 pos : SV_POSITION;\n"
    "};\n"
    "uint4 main(PS_IN ps_in) : SV_TARGET {\n"
    "   return uint4(0, 255, 0, 255);"
    "}\n";

TEST_F(resolve, simpleRect) {
  RunTest([](ID3D11Device* device, ID3D11DeviceContext* context,
             ID3D11Texture2D* texture) {
    DXPointer<ID3D11Texture2D> resolve_texture;
    DXPointer<ID3D11Texture2D> temporary_texture;

    D3D11_TEXTURE2D_DESC texture_desc;
    ZeroMemory(&texture_desc, sizeof(D3D11_TEXTURE2D_DESC));
    texture_desc.Width = width;
    texture_desc.Height = height;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.SampleDesc.Quality = 0;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    texture_desc.CPUAccessFlags = 0;
    texture_desc.MiscFlags = 0;
    HRESULT hr = device->CreateTexture2D(&texture_desc, NULL,
                                         &resolve_texture.get());
    ASSERT_EQ(S_OK, hr);

    texture_desc.SampleDesc.Count = 4;
    hr = device->CreateTexture2D(&texture_desc, NULL, &temporary_texture.get());
    ASSERT_EQ(S_OK, hr);

    DXPointer<ID3D11RenderTargetView> rtv;
    hr = device->CreateRenderTargetView(temporary_texture.get(), NULL, &rtv.get());
    ASSERT_EQ(S_OK, hr);

    // Render stuff using device, and context to get it into texture2d.
    const float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    context->ClearRenderTargetView(rtv.get(), clear_color);

    ShaderOutput<ID3D11VertexShader> vs;
    ShaderOutput<ID3D11PixelShader> ps;

    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Vertex Shader", "vs_5_0",
                                         g_vsCode.c_str(), &vs));
    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Pixel Shader", "ps_5_0",
                                         g_psCode.c_str(), &ps));

    D3D11_VIEWPORT omViewport;
    omViewport.TopLeftX = 0.0f;
    omViewport.TopLeftY = 0.0f;
    omViewport.Width = width;
    omViewport.Height = height;
    omViewport.MinDepth = 0.0f;
    omViewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &omViewport);

    context->OMSetRenderTargets(1, &rtv.get(), nullptr);

    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    context->VSSetShader(vs.shader.get(), nullptr, 0);
    context->PSSetShader(ps.shader.get(), nullptr, 0);

    context->Draw(4, 0);
    ID3D11RenderTargetView* null_view = nullptr;
    context->OMSetRenderTargets(1, &null_view, nullptr);

    context->ResolveSubresource(resolve_texture.get(), 0, temporary_texture.get(),
                                0, DXGI_FORMAT_R8G8B8A8_UNORM);
    context->CopyResource(texture, resolve_texture.get());
  });
}

TEST_F(resolve, differentFormat) {
  RunTest([](ID3D11Device* device, ID3D11DeviceContext* context,
             ID3D11Texture2D* texture) {
    DXPointer<ID3D11Texture2D> resolve_texture;
    DXPointer<ID3D11Texture2D> temporary_texture;

    D3D11_TEXTURE2D_DESC texture_desc;
    ZeroMemory(&texture_desc, sizeof(D3D11_TEXTURE2D_DESC));
    texture_desc.Width = width;
    texture_desc.Height = height;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.SampleDesc.Quality = 0;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    texture_desc.CPUAccessFlags = 0;
    texture_desc.MiscFlags = 0;
    HRESULT hr = device->CreateTexture2D(&texture_desc, NULL,
                                         &resolve_texture.get());
    ASSERT_EQ(S_OK, hr);

    ZeroMemory(&texture_desc, sizeof(D3D11_TEXTURE2D_DESC));
    texture_desc.Width = width;
    texture_desc.Height = height;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UINT;
    texture_desc.SampleDesc.Count = 4;
    texture_desc.SampleDesc.Quality = 0;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    texture_desc.CPUAccessFlags = 0;
    texture_desc.MiscFlags = 0;
    hr = device->CreateTexture2D(&texture_desc, NULL, &temporary_texture.get());
    ASSERT_EQ(S_OK, hr);

    DXPointer<ID3D11RenderTargetView> rtv;
    hr = device->CreateRenderTargetView(temporary_texture.get(), NULL, &rtv.get());
    ASSERT_EQ(S_OK, hr);

    // Render stuff using device, and context to get it into texture2d.
    const float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    context->ClearRenderTargetView(rtv.get(), clear_color);

    ShaderOutput<ID3D11VertexShader> vs;
    ShaderOutput<ID3D11PixelShader> ps;

    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Vertex Shader", "vs_5_0",
                                         g_vsCode.c_str(), &vs));
    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Pixel Shader", "ps_5_0",
                                         g_psCode.c_str(), &ps));

    D3D11_VIEWPORT omViewport;
    omViewport.TopLeftX = 0.0f;
    omViewport.TopLeftY = 0.0f;
    omViewport.Width = width;
    omViewport.Height = height;
    omViewport.MinDepth = 0.0f;
    omViewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &omViewport);

    context->OMSetRenderTargets(1, &rtv.get(), nullptr);

    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    context->VSSetShader(vs.shader.get(), nullptr, 0);
    context->PSSetShader(ps.shader.get(), nullptr, 0);

    context->Draw(4, 0);
    ID3D11RenderTargetView* null_view = nullptr;
    context->OMSetRenderTargets(1, &null_view, nullptr);

    context->ResolveSubresource(resolve_texture.get(), 0, temporary_texture.get(),
                                0, DXGI_FORMAT_R8G8B8A8_UNORM);
    context->CopyResource(texture, resolve_texture.get());
  });
}
