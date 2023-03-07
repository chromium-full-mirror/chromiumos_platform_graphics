// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <d3d11.h>
#include <d3dcommon.h>
#include <dxgiformat.h>
#include <gtest/gtest.h>
#include <windows.h>
#include <functional>
#include <istream>
#include <string>

#include "dx_pointer.h"
#include "pixel_test_fixture.h"
#include "test_utils.h"

using geometryshader = render_test<DXGI_FORMAT_R8G8B8A8_UNORM, 50, 50>;

const std::string g_vsCode =
    R"(
struct GS_IN {
  float4 pos : SV_POSITION;
};
GS_IN main(uint id: SV_VertexID): SV_POSITION {
  GS_IN output;
  if (id == 0) {
    output.pos = float4(0.0, 0.5, 0.5, 1.0);
  }
  if (id == 1) {
    output.pos = float4(0.5, -0.5, 0.5, 1.0);
  }
  if (id == 2) {
    output.pos = float4(-0.5, -0.5, 0.5, 1.0);
  }
  return output;
}
)";

const std::string g_gsCode =
    R"(
struct GS_IN {
  float4 pos : SV_POSITION;
};
struct PS_IN {
  float4 pos : SV_POSITION;
};
[maxvertexcount(4)]
void main( point GS_IN center[1], inout TriangleStream<PS_IN> triStream) {
  PS_IN v;
  float delta = 0.1;
  v.pos = center[0].pos;
  v.pos.x -= delta;
  triStream.Append(v);
  v.pos = center[0].pos;
  v.pos.y += delta;
  triStream.Append(v);
  v.pos = center[0].pos;
  v.pos.y -= delta;
  triStream.Append(v);
  v.pos = center[0].pos;
  v.pos.x += delta;
  triStream.Append(v);
  triStream.RestartStrip();
}
)";

const std::string g_psCode =
    R"(
struct PS_IN {
  float4 pos : SV_POSITION;
};
float4 main(PS_IN ps_in) : SV_TARGET {
  return float4(1.0f, 1.0f, 0.0f, 1.0f);
}
)";

TEST_F(geometryshader, basicshader) {
  RunTest([](ID3D11Device* device, ID3D11DeviceContext* context,
             ID3D11RenderTargetView* view) {
    // Render stuff using device, and context to get it into texture2d.
    const float clear_color[4] = {0.15f, 0.15f, 0.15f, 1.0f};
    context->ClearRenderTargetView(view, clear_color);

    ShaderOutput<ID3D11VertexShader> vs;
    ShaderOutput<ID3D11GeometryShader> gs;
    ShaderOutput<ID3D11PixelShader> ps;

    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Vertex Shader", "vs_5_0",
                                         g_vsCode.c_str(), &vs));
    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Geometry Shader", "gs_5_0",
                                         g_gsCode.c_str(), &gs));
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

    context->OMSetRenderTargets(1, &view, nullptr);

    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);

    context->VSSetShader(vs.shader.get(), nullptr, 0);
    context->GSSetShader(gs.shader.get(), nullptr, 0);
    context->PSSetShader(ps.shader.get(), nullptr, 0);

    context->Draw(3, 0);
    ID3D11RenderTargetView* null_view = nullptr;
    context->OMSetRenderTargets(1, &null_view, nullptr);
  });
}
