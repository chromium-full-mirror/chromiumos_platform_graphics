// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <d3d11.h>
#include <d3dcommon.h>
#include <dxgiformat.h>
#include <gtest/gtest.h>
#include <stddef.h>
#include <stdint.h>
#include <windows.h>
#include <functional>
#include <istream>
#include <string>
#include <vector>

#include "dx_pointer.h"
#include "pixel_test_fixture.h"
#include "test_utils.h"

using streamoutput = render_test<DXGI_FORMAT_R8G8B8A8_UNORM, 30, 30>;

const std::string g_vsCode =
    R"(
float4 main(uint id: SV_VertexID): SV_POSITION {
  if (id == 0)
    return float4(0.0, 0.5, 0.5, 1.0);
  if (id == 1)
    return float4(0.5, -0.5, 0.5, 1.0);
  if (id == 2)
    return float4(-0.5, -0.5, 0.5, 1.0);
  return float4(0, 0, 0, 0);
 })";

const std::string g_vs2Code =
    R"(
struct VS_IN {
  float4 in_pos: VS_POSITION;
};

float4 main(VS_IN vsi): SV_POSITION {
  return float4(-vsi.in_pos.xy, 0.5f, 1.0f);
 })";

const std::string g_psCode =
    "struct PS_IN {\n"
    "  float4 pos : SV_POSITION;\n"
    "};\n"
    "float4 main(PS_IN ps_in) : SV_TARGET {\n"
    "   return float4(0.0f, 1.0f, 0.0f, 1.0f);"
    "}\n";

TEST_F(streamoutput, streamoutLineList) {
  RunTest([](ID3D11Device* device, ID3D11DeviceContext* context,
             ID3D11RenderTargetView* view) {
    // Render stuff using device, and context to get it into texture2d.
    const float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    context->ClearRenderTargetView(view, clear_color);

    ShaderOutput<ID3D11VertexShader> vs;
    ShaderOutput<ID3D11VertexShader> vs2;
    ShaderOutput<ID3D11PixelShader> ps;
    DXPointer<ID3D11GeometryShader> gs;
    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Vertex Shader", "vs_5_0",
                                         g_vsCode.c_str(), &vs));
    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Pixel Shader", "ps_5_0",
                                         g_psCode.c_str(), &ps));
    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Vertex Shader2", "vs_5_0",
                                         g_vs2Code.c_str(), &vs2));

    D3D11_SO_DECLARATION_ENTRY pDecl[] = {
        // semantic name, semantic index, start component, component count, output slot
        {0, "SV_POSITION", 0, 0, 4, 0},  // output all components of position
    };
    device->CreateGeometryShaderWithStreamOutput(
        vs.blob_output.data(), vs.blob_output.size(), pDecl, 1, NULL, 0,
        D3D11_SO_NO_RASTERIZED_STREAM, NULL, &gs.get());

    D3D11_INPUT_ELEMENT_DESC elements[] = {
        {"VS_POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    DXPointer<ID3D11InputLayout> layout;

    ASSERT_EQ(S_OK,
              device->CreateInputLayout(elements, 1, vs2.blob_output.data(),
                                        vs2.blob_output.size(), &layout.get()));

    DXPointer<ID3D11Buffer> stream_buffer;
    uint32_t buffer_size = 1000000;

    D3D11_BUFFER_DESC bufferDesc = {
        buffer_size,
        D3D11_USAGE_DEFAULT,
        D3D11_BIND_STREAM_OUTPUT | D3D11_BIND_VERTEX_BUFFER,
        0,
        0,
        0};
    device->CreateBuffer(&bufferDesc, NULL, &stream_buffer.get());

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
    context->GSSetShader(gs.get(), nullptr, 0);

    UINT offset[1] = {0};
    context->SOSetTargets(1, &stream_buffer.get(), offset);

    context->Draw(3, 0);

    context->VSSetShader(vs2.shader.get(), nullptr, 0);
    context->PSSetShader(ps.shader.get(), nullptr, 0);
    context->GSSetShader(nullptr, nullptr, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->IASetInputLayout(layout.get());
    uint32_t strides[] = {sizeof(float) * 4};
    uint32_t offsets[] = {0};
    context->SOSetTargets(0, nullptr, nullptr);
    context->IASetVertexBuffers(0, 1, &stream_buffer.get(), strides, offsets);

    context->Draw(3, 0);

    ID3D11RenderTargetView* null_view = nullptr;
    context->OMSetRenderTargets(1, &null_view, nullptr);
  });
}

TEST_F(streamoutput, streamoutPointList) {
  RunTest([](ID3D11Device* device, ID3D11DeviceContext* context,
             ID3D11RenderTargetView* view) {
    // Render stuff using device, and context to get it into texture2d.
    const float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    context->ClearRenderTargetView(view, clear_color);

    ShaderOutput<ID3D11VertexShader> vs;
    ShaderOutput<ID3D11VertexShader> vs2;
    ShaderOutput<ID3D11PixelShader> ps;
    DXPointer<ID3D11GeometryShader> gs;
    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Vertex Shader", "vs_5_0",
                                         g_vsCode.c_str(), &vs));
    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Pixel Shader", "ps_5_0",
                                         g_psCode.c_str(), &ps));
    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Vertex Shader2", "vs_5_0",
                                         g_vs2Code.c_str(), &vs2));

    D3D11_SO_DECLARATION_ENTRY pDecl[] = {
        // semantic name, semantic index, start component, component count, output slot
        {0, "SV_POSITION", 0, 0, 4, 0},  // output all components of position
    };
    device->CreateGeometryShaderWithStreamOutput(
        vs.blob_output.data(), vs.blob_output.size(), pDecl, 1, NULL, 0,
        D3D11_SO_NO_RASTERIZED_STREAM, NULL, &gs.get());

    D3D11_INPUT_ELEMENT_DESC elements[] = {
        {"VS_POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    DXPointer<ID3D11InputLayout> layout;

    ASSERT_EQ(S_OK,
              device->CreateInputLayout(elements, 1, vs2.blob_output.data(),
                                        vs2.blob_output.size(), &layout.get()));

    DXPointer<ID3D11Buffer> stream_buffer;
    uint32_t buffer_size = 1000000;

    D3D11_BUFFER_DESC bufferDesc = {
        buffer_size,
        D3D11_USAGE_DEFAULT,
        D3D11_BIND_STREAM_OUTPUT | D3D11_BIND_VERTEX_BUFFER,
        0,
        0,
        0};
    device->CreateBuffer(&bufferDesc, NULL, &stream_buffer.get());

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
    context->GSSetShader(gs.get(), nullptr, 0);

    UINT offset[1] = {0};
    context->SOSetTargets(1, &stream_buffer.get(), offset);

    context->Draw(3, 0);

    context->VSSetShader(vs2.shader.get(), nullptr, 0);
    context->PSSetShader(ps.shader.get(), nullptr, 0);
    context->GSSetShader(nullptr, nullptr, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->IASetInputLayout(layout.get());
    uint32_t strides[] = {sizeof(float) * 4};
    uint32_t offsets[] = {0};
    context->SOSetTargets(0, nullptr, nullptr);
    context->IASetVertexBuffers(0, 1, &stream_buffer.get(), strides, offsets);

    context->Draw(3, 0);

    ID3D11RenderTargetView* null_view = nullptr;
    context->OMSetRenderTargets(1, &null_view, nullptr);
  });
}
