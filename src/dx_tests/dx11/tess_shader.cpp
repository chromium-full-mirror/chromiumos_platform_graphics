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

using tessellationshader =
    texel_test<DXGI_FORMAT_R8G8B8A8_UNORM, 50, 50,
               fuzzy_image_comparator<DXGI_FORMAT_R8G8B8A8_UNORM, 50, 50, 529, 34>>;

const std::string g_vsCode =
    R"(
struct HS_IN {
  float3 pos : POSITION;
};
HS_IN main(uint id: SV_VertexID) {
  HS_IN output;
  if (id == 0) {
    output.pos = float3(0.0, 0.5, 0.5);
  }
  if (id == 1) {
    output.pos = float3(0.5, -0.5, 0.5);
  }
  if (id == 2) {
    output.pos = float3(-0.5, -0.5, 0.5);
  }
  return output;
}
)";

const std::string g_hsCode =
    R"(
#define MAX_POINTS 3
struct HS_IN {
  float3 pos : POSITION;
};
struct HS_CONSTANT_DATA {
    float Edges[3] : SV_TessFactor;
    float Inside : SV_InsideTessFactor;
};
HS_CONSTANT_DATA ConstantHS(InputPatch<HS_IN, MAX_POINTS> ip,
           uint PatchID : SV_PrimitiveID) {
  HS_CONSTANT_DATA output;
  output.Edges[0] = 3;
  output.Edges[1] = 3;
  output.Edges[2] = 3;
  output.Inside = 3;
  return output;
}

struct HS_OUT{
    float3 pos : POSITION;
};
[domain("tri")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("ConstantHS")]
[maxtessfactor(64.0)]
HS_OUT main( InputPatch<HS_IN, MAX_POINTS> ip,
                            uint i : SV_OutputControlPointID,
                            uint PatchID : SV_PrimitiveID)
{
    HS_OUT output;
    output.pos = ip[i].pos;
    return output;
}
)";

const std::string g_dsCode =
    R"(
struct HS_CONSTANT_DATA {
    float Edges[3] : SV_TessFactor;
    float Inside : SV_InsideTessFactor;
};
struct HS_OUT{
    float3 pos : POSITION;
};
struct DS_OUT{
    float4 vPosition : SV_POSITION;
    nointerpolation float3 color : COLOR;
};

[domain("tri")]
DS_OUT main(HS_CONSTANT_DATA input,
            float3 UVW : SV_DomainLocation,
            const OutputPatch<HS_OUT, 3> patch)
{
    DS_OUT output;
    float3 pointPos = UVW.x * patch[0].pos + UVW.y * patch[1].pos + UVW.z * patch[2].pos;
    output.vPosition = float4(pointPos, 1.0f);
    output.color = UVW;
    return output;
}
)";

const std::string g_psCode =
    R"(
struct DS_OUT{
    float4 vPosition : SV_POSITION;
    nointerpolation float3 color : COLOR;
};
float4 main(DS_OUT ps_in) : SV_TARGET {
  return float4(ps_in.color, 1.0f);
}
)";

// TODO(b/272548788): Lower the error tolerance of the tesselationshader fixture
TEST_F(tessellationshader, basictessshader) {
  RunTest([](ID3D11Device* device, ID3D11DeviceContext* context,
             ID3D11Texture2D* texture) {
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

    DXPointer<ID3D11Texture2D> temporary_texture;
    ASSERT_EQ(S_OK, device->CreateTexture2D(&texture_desc, NULL,
                                            &temporary_texture.get()));
    DXPointer<ID3D11RenderTargetView> rtv;
    ASSERT_EQ(S_OK, device->CreateRenderTargetView(temporary_texture.get(),
                                                   NULL, &rtv.get()));
    // Render stuff using device, and context to get it into texture2d.
    const float clear_color[4] = {0.15f, 0.15f, 0.15f, 1.0f};
    context->ClearRenderTargetView(rtv.get(), clear_color);

    ShaderOutput<ID3D11VertexShader> vs;
    ShaderOutput<ID3D11HullShader> hs;
    ShaderOutput<ID3D11DomainShader> ds;
    ShaderOutput<ID3D11PixelShader> ps;

    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Vertex Shader", "vs_5_0",
                                         g_vsCode.c_str(), &vs));
    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Hull Shader", "hs_5_0",
                                         g_hsCode.c_str(), &hs));
    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Domain Shader", "ds_5_0",
                                         g_dsCode.c_str(), &ds));
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
    context->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);

    context->VSSetShader(vs.shader.get(), nullptr, 0);
    context->HSSetShader(hs.shader.get(), nullptr, 0);
    context->DSSetShader(ds.shader.get(), nullptr, 0);
    context->PSSetShader(ps.shader.get(), nullptr, 0);

    context->Draw(3, 0);
    ID3D11RenderTargetView* null_view = nullptr;
    context->OMSetRenderTargets(1, &null_view, nullptr);

    context->CopyResource(texture, temporary_texture.get());
  });
}
