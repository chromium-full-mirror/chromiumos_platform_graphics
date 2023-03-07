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
#include <cstdint>
#include <functional>
#include <istream>
#include <string>
#include <vector>

#include "dx_pointer.h"
#include "pixel_test_fixture.h"
#include "test_utils.h"

using triangle = render_test<DXGI_FORMAT_R8G8B8A8_UNORM, 30, 30>;

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

const std::string g_vsVBCode =
    R"(
float4 main(float2 position : POSITION): SV_POSITION {
  return float4(position, 0.5, 1.0);
})";

const std::string g_psCode =
    R"(
struct PS_IN {
  float4 pos : SV_POSITION;
};
float4 main(PS_IN ps_in) : SV_TARGET {
  return float4(1.0f, 0.0f, 0.0f, 1.0f);
})";

const std::string g_psCBCode =
    R"(
struct PS_IN {
  float4 pos : SV_POSITION;
};
cbuffer ConstantData : register(b0) {
  float4 color;
}
float4 main(PS_IN ps_in) : SV_TARGET {
  return color;
})";

const std::string g_vsTextCode =
    R"(
struct PS_IN
{
    float4 position : SV_POSITION;
    float2 tex : TEXCOORD0;
};
PS_IN main(uint id: SV_VertexID) {
  PS_IN output;
  if (id == 0) {
    output.position = float4(0.0, 0.5, 0.5, 1.0);
    output.tex = float2(0.5, 1.0);
  }
  if (id == 1) {
    output.position = float4(0.5, -0.5, 0.5, 1.0);
    output.tex = float2(1.0, 0.0);
  }
  if (id == 2) {
    output.position = float4(-0.5, -0.5, 0.5, 1.0);
    output.tex = float2(0.0, 0.0);
  }
  return output;
})";

const std::string g_psTextCode =
    R"(
struct PS_IN
{
    float4 position : SV_POSITION;
    float2 tex : TEXCOORD0;
};
Texture2D<float4> textMap : register(t0);
SamplerState linearSampler : register(s0);
float4 main(PS_IN ps_in) : SV_TARGET {
  return textMap.Sample(linearSampler, ps_in.tex);
})";

TEST_F(triangle, simplesttriangle) {
  RunTest([](ID3D11Device* device, ID3D11DeviceContext* context,
             ID3D11RenderTargetView* view) {
    // Render stuff using device, and context to get it into texture2d.
    const float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    context->ClearRenderTargetView(view, clear_color);

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

    context->OMSetRenderTargets(1, &view, nullptr);

    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    context->VSSetShader(vs.shader.get(), nullptr, 0);
    context->PSSetShader(ps.shader.get(), nullptr, 0);

    context->Draw(3, 0);
    ID3D11RenderTargetView* null_view = nullptr;
    context->OMSetRenderTargets(1, &null_view, nullptr);
  });
}

TEST_F(triangle, constantbuffer) {
  RunTest([](ID3D11Device* device, ID3D11DeviceContext* context,
             ID3D11RenderTargetView* view) {
    // Render stuff using device, and context to get it into texture2d.
    const float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    context->ClearRenderTargetView(view, clear_color);

    ShaderOutput<ID3D11VertexShader> vs;
    ShaderOutput<ID3D11PixelShader> ps;

    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Vertex Shader", "vs_5_0",
                                         g_vsCode.c_str(), &vs));
    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Pixel Shader", "ps_5_0",
                                         g_psCBCode.c_str(), &ps));

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

    struct alignas(16) ConstantData {
      float color[4];
    };
    ConstantData data;
    data.color[0] = 0.0f;
    data.color[1] = 0.0f;
    data.color[2] = 1.0f;
    data.color[3] = 1.0f;

    D3D11_BUFFER_DESC cbDesc;
    cbDesc.ByteWidth = sizeof(ConstantData);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cbDesc.MiscFlags = 0;
    cbDesc.StructureByteStride = 0;

    D3D11_SUBRESOURCE_DATA initData;
    initData.pSysMem = &data;
    initData.SysMemPitch = 0;
    initData.SysMemSlicePitch = 0;

    DXPointer<ID3D11Buffer> constantBuffer;

    ASSERT_EQ(S_OK,
              device->CreateBuffer(&cbDesc, &initData, &constantBuffer.get()));

    context->PSSetConstantBuffers(0, 1, &constantBuffer.get());

    context->Draw(3, 0);
    ID3D11RenderTargetView* null_view = nullptr;
    context->OMSetRenderTargets(1, &null_view, nullptr);
  });
}

TEST_F(triangle, vertexbuffer) {
  RunTest([](ID3D11Device* device, ID3D11DeviceContext* context,
             ID3D11RenderTargetView* view) {
    // Render stuff using device, and context to get it into texture2d.
    const float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    context->ClearRenderTargetView(view, clear_color);

    ShaderOutput<ID3D11VertexShader> vs;
    ShaderOutput<ID3D11PixelShader> ps;

    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Vertex Shader", "vs_5_0",
                                         g_vsVBCode.c_str(), &vs));
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

    // Create a vertex buffer
    DXPointer<ID3D11Buffer> vertexBuffer;
    // Define vertex element
    struct SimpleVertex {
      float pos[2];
    };
    // Vertex buffer data
    std::vector<SimpleVertex> vertexData = {
        {-0.5f, -0.5f}, {0.0f, 0.5f}, {0.5f, -0.5f}};
    // Vertex buffer description.
    D3D11_BUFFER_DESC vBufferDesc;
    vBufferDesc.Usage = D3D11_USAGE_DEFAULT;
    vBufferDesc.ByteWidth = static_cast<UINT>(sizeof(SimpleVertex) *
                                              vertexData.size());
    vBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vBufferDesc.CPUAccessFlags = 0;
    vBufferDesc.MiscFlags = 0;
    // Descriptor for the data.
    D3D11_SUBRESOURCE_DATA initData;
    initData.pSysMem = vertexData.data();
    initData.SysMemPitch = 0;
    initData.SysMemSlicePitch = 0;
    // Create the buffer
    ASSERT_EQ(S_OK,
              device->CreateBuffer(&vBufferDesc, &initData, &vertexBuffer.get()));
    // Set vertex buffer
    UINT stride = sizeof(SimpleVertex);
    UINT offset = 0;
    context->IASetVertexBuffers(0, 1, &vertexBuffer.get(), &stride, &offset);
    // Create input layout
    D3D11_INPUT_ELEMENT_DESC inputDesc[] = {{"POSITION", 0,
                                             DXGI_FORMAT_R32G32_FLOAT, 0, 0,
                                             D3D11_INPUT_PER_VERTEX_DATA, 0}};

    DXPointer<ID3D11InputLayout> vertexShaderInputLayout;

    ASSERT_EQ(S_OK, device->CreateInputLayout(inputDesc, 1, vs.blob_output.data(),
                                              vs.blob_output.size(),
                                              &vertexShaderInputLayout.get()));
    // Set input layout
    context->IASetInputLayout(vertexShaderInputLayout.get());

    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    context->VSSetShader(vs.shader.get(), nullptr, 0);
    context->PSSetShader(ps.shader.get(), nullptr, 0);

    context->Draw(3, 0);
    ID3D11RenderTargetView* null_view = nullptr;
    context->OMSetRenderTargets(1, &null_view, nullptr);
  });
}

TEST_F(triangle, indexbuffer) {
  RunTest([](ID3D11Device* device, ID3D11DeviceContext* context,
             ID3D11RenderTargetView* view) {
    const float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    context->ClearRenderTargetView(view, clear_color);

    ShaderOutput<ID3D11VertexShader> vs;
    ShaderOutput<ID3D11PixelShader> ps;

    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Vertex Shader", "vs_5_0",
                                         g_vsVBCode.c_str(), &vs));
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

    // Create a vertex buffer
    DXPointer<ID3D11Buffer> vertexBuffer;
    // Define vertex element
    struct SimpleVertex {
      float pos[2];
    };
    // Vertex buffer data
    std::vector<SimpleVertex> vertexData = {
        {-0.5f, -0.5f}, {-0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, -0.5f}, {0.0f, 0.0f}};
    // Vertex buffer description.
    D3D11_BUFFER_DESC vBufferDesc;
    vBufferDesc.Usage = D3D11_USAGE_DEFAULT;
    vBufferDesc.ByteWidth = static_cast<UINT>(sizeof(SimpleVertex) *
                                              vertexData.size());
    vBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vBufferDesc.CPUAccessFlags = 0;
    vBufferDesc.MiscFlags = 0;
    // Descriptor for the vertex data.
    D3D11_SUBRESOURCE_DATA vInitData;
    vInitData.pSysMem = vertexData.data();
    vInitData.SysMemPitch = 0;
    vInitData.SysMemSlicePitch = 0;
    // Create the vertex buffer
    ASSERT_EQ(S_OK, device->CreateBuffer(&vBufferDesc, &vInitData,
                                         &vertexBuffer.get()));
    // Set vertex buffer
    UINT stride = sizeof(SimpleVertex);
    UINT offset = 0;
    context->IASetVertexBuffers(0, 1, &vertexBuffer.get(), &stride, &offset);

    // Create a index buffer
    DXPointer<ID3D11Buffer> indexBuffer;
    // Index buffer data
    std::vector<unsigned int> indexData = {0, 1, 4, 4, 2, 3};
    // Fill in a buffer description.
    D3D11_BUFFER_DESC iBufferDesc;
    iBufferDesc.Usage = D3D11_USAGE_DEFAULT;
    iBufferDesc.ByteWidth = static_cast<UINT>(
        (sizeof(unsigned int) * indexData.size()));
    iBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    iBufferDesc.CPUAccessFlags = 0;
    iBufferDesc.MiscFlags = 0;
    // Descriptor for the index data.
    D3D11_SUBRESOURCE_DATA iInitData;
    iInitData.pSysMem = indexData.data();
    iInitData.SysMemPitch = 0;
    iInitData.SysMemSlicePitch = 0;
    // Create the index buffer
    ASSERT_EQ(S_OK,
              device->CreateBuffer(&iBufferDesc, &iInitData, &indexBuffer.get()));
    // Set the buffer
    context->IASetIndexBuffer(indexBuffer.get(), DXGI_FORMAT_R32_UINT, 0);

    // Create input layout
    D3D11_INPUT_ELEMENT_DESC inputDesc[] = {{"POSITION", 0,
                                             DXGI_FORMAT_R32G32_FLOAT, 0, 0,
                                             D3D11_INPUT_PER_VERTEX_DATA, 0}};

    DXPointer<ID3D11InputLayout> vertexShaderInputLayout;
    ASSERT_EQ(S_OK, device->CreateInputLayout(inputDesc, 1, vs.blob_output.data(),
                                              vs.blob_output.size(),
                                              &vertexShaderInputLayout.get()));
    // Set input layout
    context->IASetInputLayout(vertexShaderInputLayout.get());

    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    context->VSSetShader(vs.shader.get(), nullptr, 0);
    context->PSSetShader(ps.shader.get(), nullptr, 0);

    context->DrawIndexed(static_cast<UINT>(indexData.size()), 0, 0);
    ID3D11RenderTargetView* null_view = nullptr;
    context->OMSetRenderTargets(1, &null_view, nullptr);
  });
}

TEST_F(triangle, simpletexture) {
  RunTest([](ID3D11Device* device, ID3D11DeviceContext* context,
             ID3D11RenderTargetView* view) {
    // Render stuff using device, and context to get it into texture2d.
    const float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    context->ClearRenderTargetView(view, clear_color);

    ShaderOutput<ID3D11VertexShader> vs;
    ShaderOutput<ID3D11PixelShader> ps;

    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Vertex Shader", "vs_5_0",
                                         g_vsTextCode.c_str(), &vs));
    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Pixel Shader", "ps_5_0",
                                         g_psTextCode.c_str(), &ps));

    D3D11_VIEWPORT omViewport;
    omViewport.TopLeftX = 0.0f;
    omViewport.TopLeftY = 0.0f;
    omViewport.Width = width;
    omViewport.Height = height;
    omViewport.MinDepth = 0.0f;
    omViewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &omViewport);
    context->OMSetRenderTargets(1, &view, nullptr);

    // Texture data (a chessboard pattern)
    const size_t boardSize = 8;
    const uint32_t black = {0xFF000000};
    const uint32_t white = {0xFFFFFFFF};
    std::vector<uint32_t> pixels(boardSize * boardSize, white);
    for (size_t j = 0; j < boardSize; ++j) {
      for (size_t i = 0; i < boardSize; ++i) {
        if ((i + j) % 2 != 0) {
          pixels[j * boardSize + i] = black;
        }
      }
    }
    // Create texture
    D3D11_TEXTURE2D_DESC tDesc;
    tDesc.Width = boardSize;
    tDesc.Height = boardSize;
    tDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    tDesc.MipLevels = 1;
    tDesc.ArraySize = 1;
    tDesc.SampleDesc.Count = 1;
    tDesc.SampleDesc.Quality = 0;
    tDesc.Usage = D3D11_USAGE_IMMUTABLE;
    tDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    tDesc.CPUAccessFlags = 0;
    tDesc.MiscFlags = 0;
    // Descriptor for the data.
    D3D11_SUBRESOURCE_DATA tInitData;
    tInitData.pSysMem = pixels.data();
    tInitData.SysMemPitch = static_cast<UINT>(sizeof(uint32_t) * tDesc.Width);
    tInitData.SysMemSlicePitch = 0;
    DXPointer<ID3D11Texture2D> texture;
    ASSERT_EQ(S_OK, device->CreateTexture2D(&tDesc, &tInitData, &texture.get()));
    // Create texture's SRV
    DXPointer<ID3D11ShaderResourceView> texView;
    D3D11_SHADER_RESOURCE_VIEW_DESC tViewDesc;
    tViewDesc.Format = tDesc.Format;
    tViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    tViewDesc.Texture2D.MostDetailedMip = 0;
    tViewDesc.Texture2D.MipLevels = 1;
    ASSERT_EQ(S_OK, device->CreateShaderResourceView(texture.get(), &tViewDesc,
                                                     &texView.get()));
    // Sampler
    DXPointer<ID3D11SamplerState> texSamplerState;
    D3D11_SAMPLER_DESC sDesc;
    sDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sDesc.MinLOD = 0;
    sDesc.MaxLOD = D3D11_FLOAT32_MAX;
    sDesc.MipLODBias = 0;
    sDesc.MaxAnisotropy = 1;
    ASSERT_EQ(S_OK, device->CreateSamplerState(&sDesc, &texSamplerState.get()));

    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    context->VSSetShader(vs.shader.get(), nullptr, 0);
    context->PSSetShader(ps.shader.get(), nullptr, 0);
    // Set shader resources
    context->PSSetShaderResources(0, 1, &texView.get());
    context->PSSetSamplers(0, 1, &texSamplerState.get());
    context->Draw(3, 0);
    ID3D11RenderTargetView* null_view = nullptr;
    context->OMSetRenderTargets(1, &null_view, nullptr);
  });
}

TEST_F(triangle, deferredtriangle) {
  RunTest([](ID3D11Device* device, ID3D11DeviceContext* immediate_context,
             ID3D11RenderTargetView* view) {
    // Create deferred context
    DXPointer<ID3D11DeviceContext> context;
    ASSERT_EQ(S_OK, device->CreateDeferredContext(0, &context.get()));

    // Render stuff using device, and context to get it into texture2d.
    const float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    context->ClearRenderTargetView(view, clear_color);

    ShaderOutput<ID3D11VertexShader> vs;
    ShaderOutput<ID3D11PixelShader> ps;

    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Vertex Shader", "vs_5_0",
                                         g_vsCode.c_str(), &vs));
    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Pixel Shader", "ps_5_0",
                                         g_psCBCode.c_str(), &ps));

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

    struct alignas(16) ConstantData {
      float color[4];
    };
    ConstantData data;
    data.color[0] = 0.0f;
    data.color[1] = 0.0f;
    data.color[2] = 1.0f;
    data.color[3] = 1.0f;

    D3D11_BUFFER_DESC cbDesc;
    cbDesc.ByteWidth = sizeof(ConstantData);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cbDesc.MiscFlags = 0;
    cbDesc.StructureByteStride = 0;

    D3D11_SUBRESOURCE_DATA initData;
    initData.pSysMem = &data;
    initData.SysMemPitch = 0;
    initData.SysMemSlicePitch = 0;

    DXPointer<ID3D11Buffer> constantBuffer;

    ASSERT_EQ(S_OK,
              device->CreateBuffer(&cbDesc, &initData, &constantBuffer.get()));

    context->PSSetConstantBuffers(0, 1, &constantBuffer.get());

    context->Draw(3, 0);
    ID3D11RenderTargetView* null_view = nullptr;
    context->OMSetRenderTargets(1, &null_view, nullptr);

    // Execute deferred context
    DXPointer<ID3D11CommandList> command_list;
    ASSERT_EQ(S_OK, context->FinishCommandList(FALSE, &command_list.get()));

    immediate_context->ExecuteCommandList(command_list.get(), FALSE);
  });
}
