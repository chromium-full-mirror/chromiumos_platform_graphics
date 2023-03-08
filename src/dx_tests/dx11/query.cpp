// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <d3d11.h>
#include <d3dcommon.h>
#include <dxgiformat.h>
#include <gtest/gtest.h>
#include <stdint.h>
#include <windows.h>
#include <functional>
#include <istream>
#include <string>

#include "dx_pointer.h"
#include "pixel_test_fixture.h"
#include "test_utils.h"

using query = render_test<DXGI_FORMAT_R32G32B32A32_UINT, 30, 30>;

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

const std::string g_psCode = R"(
    struct PS_IN {
      float4 pos : SV_POSITION;
    };
    float4 main(PS_IN ps_in) : SV_TARGET {
       return float4(0.0f, 1.0f, 0.0f, 1.0f);
    })";

const std::string g_ps2Code = R"(
    cbuffer MyBuffer {
        uint query_result : packoffset(c0.x);
    }
    struct PS_IN {
      float4 pos : SV_POSITION;
    };
    float4 main(PS_IN ps_in) : SV_TARGET {
       return float4(query_result, 0.0f, 0.0f, 1.0f);
    })";

const std::string g_psCodePipelineStatistics = R"(
    cbuffer MyBuffer {
        uint4 query_data;
    }
    struct PS_IN {
      float4 pos : SV_POSITION;
    };
    uint4 main(PS_IN ps_in) : SV_TARGET {
       return uint4(query_data);
    })";

TEST_F(query, occlusion) {
  RunTest([](ID3D11Device* device, ID3D11DeviceContext* context,
             ID3D11RenderTargetView* view) {
    // Render stuff using device, and context to get it into texture2d.
    const float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    context->ClearRenderTargetView(view, clear_color);

    ShaderOutput<ID3D11VertexShader> vs;
    ShaderOutput<ID3D11PixelShader> ps;
    ShaderOutput<ID3D11PixelShader> ps2;

    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Vertex Shader", "vs_5_0",
                                         g_vsCode.c_str(), &vs));
    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Pixel Shader", "ps_5_0",
                                         g_psCode.c_str(), &ps));
    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Pixel Shader", "ps_5_0",
                                         g_ps2Code.c_str(), &ps2));

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
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    context->VSSetShader(vs.shader.get(), nullptr, 0);
    context->PSSetShader(ps.shader.get(), nullptr, 0);

    D3D11_QUERY_DESC queryDesc;
    queryDesc.Query = D3D11_QUERY_OCCLUSION;
    queryDesc.MiscFlags = 0;
    DXPointer<ID3D11Query> pQuery;
    device->CreateQuery(&queryDesc, &pQuery.get());
    context->Begin(pQuery.get());
    context->Draw(3, 0);
    context->End(pQuery.get());
    UINT64 queryData;  // This data type is different depending on the query type

    while (S_OK != context->GetData(pQuery.get(), &queryData, sizeof(UINT64), 0)) {
    }

    DXPointer<ID3D11Buffer> constant_buffer;

    D3D11_BUFFER_DESC bufferDesc = {
        16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
    uint32_t query_result = static_cast<uint32_t>(queryData);
    D3D11_SUBRESOURCE_DATA dat;
    dat.pSysMem = &query_result;
    dat.SysMemPitch = 4;
    dat.SysMemSlicePitch = 4;
    device->CreateBuffer(&bufferDesc, &dat, &constant_buffer.get());
    context->PSSetShader(ps2.shader.get(), nullptr, 0);
    context->PSSetConstantBuffers(0, 1, &constant_buffer.get());
    context->Draw(3, 0);

    ID3D11RenderTargetView* null_view = nullptr;
    context->OMSetRenderTargets(1, &null_view, nullptr);
  });
}

TEST_F(query, pipelineStatistics) {
  RunTest([](ID3D11Device* device, ID3D11DeviceContext* context,
             ID3D11RenderTargetView* view) {
    // Render stuff using device, and context to get it into texture2d.
    const float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    context->ClearRenderTargetView(view, clear_color);

    ShaderOutput<ID3D11VertexShader> vs;
    ShaderOutput<ID3D11PixelShader> ps;
    ShaderOutput<ID3D11PixelShader> ps2;

    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Vertex Shader", "vs_5_0",
                                         g_vsCode.c_str(), &vs));
    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Pixel Shader", "ps_5_0",
                                         g_psCode.c_str(), &ps));
    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Pixel Shader", "ps_5_0",
                                         g_psCodePipelineStatistics.c_str(), &ps2));

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
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    context->VSSetShader(vs.shader.get(), nullptr, 0);
    context->PSSetShader(ps.shader.get(), nullptr, 0);

    D3D11_QUERY_DESC queryDesc;
    queryDesc.Query = D3D11_QUERY_PIPELINE_STATISTICS;
    queryDesc.MiscFlags = 0;
    DXPointer<ID3D11Query> pQuery;
    device->CreateQuery(&queryDesc, &pQuery.get());
    context->Begin(pQuery.get());
    context->Draw(3, 0);
    context->End(pQuery.get());
    D3D11_QUERY_DATA_PIPELINE_STATISTICS queryData;  // This data type is different
                                                     // depending on the query type

    while (S_OK !=
           context->GetData(pQuery.get(), &queryData,
                            sizeof(D3D11_QUERY_DATA_PIPELINE_STATISTICS), 0)) {
    }

    // Not all drivers use exactly the same number of PS invocations.
    uint16_t ps_invocations = queryData.PSInvocations != 0 ? 0xFFFF : 0;
    uint32_t data[4];
    data[0] = static_cast<uint16_t>(queryData.IAPrimitives) |
              (static_cast<uint16_t>(queryData.IAVertices) << 16);
    data[1] = static_cast<uint16_t>(queryData.VSInvocations) |
              (ps_invocations << 16);
    data[2] = static_cast<uint16_t>(queryData.CInvocations) |
              (static_cast<uint16_t>(queryData.CPrimitives) << 16);
    data[3] = static_cast<uint32_t>(
        queryData.GSInvocations + queryData.GSPrimitives + queryData.DSInvocations +
        queryData.CSInvocations + queryData.HSInvocations);  // Should all be 0

    DXPointer<ID3D11Buffer> constant_buffer;

    D3D11_BUFFER_DESC bufferDesc = {
        16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};

    D3D11_SUBRESOURCE_DATA dat;
    dat.pSysMem = &data;
    dat.SysMemPitch = sizeof(data);
    dat.SysMemSlicePitch = sizeof(data);
    device->CreateBuffer(&bufferDesc, &dat, &constant_buffer.get());
    context->PSSetShader(ps2.shader.get(), nullptr, 0);
    context->PSSetConstantBuffers(0, 1, &constant_buffer.get());
    context->Draw(3, 0);

    ID3D11RenderTargetView* null_view = nullptr;
    context->OMSetRenderTargets(1, &null_view, nullptr);
  });
}
