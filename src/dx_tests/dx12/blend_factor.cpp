// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <d3d12.h>
#include <d3dcommon.h>
#include <dxgiformat.h>
#include <gtest/gtest.h>
#include <string.h>
#include <windows.h>
#include <functional>
#include <string>

#include "common_test_utils.h"
#include "dx_pointer.h"
#include "pixel_test_fixture.h"
#include "test_utils.h"

template <DXGI_FORMAT FORMAT, size_t WIDTH, size_t HEIGHT,
          size_t max_num_fuzzy_texels, size_t max_per_channel_texel_difference_percent>
struct fuzzy_image_comparator;

const std::string g_vsCode =
    R"(
struct VS_OUT
{
    float4 position : SV_POSITION;
};

VS_OUT main(float4 pos : POSITION)
{
    VS_OUT vso;
    pos.w = 1.0f;
    vso.position = pos;
    return vso;
})";

const std::string g_psCode =
    R"(
float4 main() : SV_TARGET
{
    return float4(1.0, 0.0, 0.0, 1.0f);
})";

struct BlendFactor {
  FLOAT r = 0.0f;
  FLOAT g = 0.0f;
  FLOAT b = 0.0f;
  FLOAT a = 0.0f;
};

using blendfactor =
    render_test<DXGI_FORMAT_R8G8B8A8_UNORM, 30, 30,
                fuzzy_image_comparator<DXGI_FORMAT_R8G8B8A8_UNORM, 30, 30,
                                       static_cast<size_t>(~0), 1>,
                BlendFactor>;

TEST_P(blendfactor, basic) {
  RunTest([this](IDXGIAdapter*, ID3D12Device* device, ID3D12CommandQueue* queue,
                 ID3D12CommandAllocator* allocator,
                 D3D12_CPU_DESCRIPTOR_HANDLE render_target_view) {
    ShaderOutput<ID3DBlob> vs;
    ShaderOutput<ID3DBlob> ps;
    ASSERT_EQ(S_OK, MakeShaderFromSource("Vertex Shader", "vs_5_1",
                                         g_vsCode.c_str(), &vs));
    ASSERT_EQ(S_OK, MakeShaderFromSource("Pixel Shader", "ps_5_1",
                                         g_psCode.c_str(), &ps));

    DXPointer<ID3D12RootSignature> rootSignature = CreateRootSignature(device);
    ASSERT_FALSE(!rootSignature);

    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = this->defaultPsoDesc;
    psoDesc.InputLayout = {inputElementDescs, _countof(inputElementDescs)};
    psoDesc.pRootSignature = rootSignature.get();
    psoDesc.VS = {reinterpret_cast<UINT8*>(vs.shader->GetBufferPointer()),
                  vs.shader->GetBufferSize()};
    psoDesc.PS = {reinterpret_cast<UINT8*>(ps.shader->GetBufferPointer()),
                  ps.shader->GetBufferSize()};

    psoDesc.BlendState.RenderTarget[0].BlendEnable = true;
    psoDesc.BlendState.RenderTarget[0].LogicOpEnable = false;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_BLEND_FACTOR;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_BLEND_FACTOR;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_BLEND_FACTOR;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_BLEND_FACTOR;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    DXPointer<ID3D12PipelineState> pso;
    ASSERT_EQ(S_OK, device->CreateGraphicsPipelineState(
                        &psoDesc, IID_PPV_ARGS(&pso.get())));

    Vertex triangleVertices[] = {
        {0.0f, 0.5f, 0.0f}, {0.5f, -0.5f, 0.0f}, {-0.5f, -0.5f, 0.0f}};
    const UINT vertexBufferSize = sizeof(triangleVertices);

    DXPointer<ID3D12Resource> vertexBuffer = CreateUploadBuffer(
        device, vertexBufferSize);
    ASSERT_FALSE(!vertexBuffer);

    UINT8* pVertexDataBegin;
    D3D12_RANGE readRange = {0, 0};  // We do not intend to read from this
                                     // resource on the CPU.
    ASSERT_EQ(S_OK, vertexBuffer->Map(0, &readRange,
                                      reinterpret_cast<void**>(&pVertexDataBegin)));

    memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
    vertexBuffer->Unmap(0, nullptr);

    D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
    vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
    vertexBufferView.StrideInBytes = sizeof(Vertex);
    vertexBufferView.SizeInBytes = vertexBufferSize;

    DXPointer<ID3D12GraphicsCommandList> list;
    HRESULT hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           allocator, pso.get(),
                                           __uuidof(ID3D12GraphicsCommandList),
                                           reinterpret_cast<void**>(&list.get()));
    ASSERT_EQ(S_OK, hr);

    list->SetGraphicsRootSignature(rootSignature.get());
    list->RSSetViewports(1, &defaultViewport);
    list->RSSetScissorRects(1, &defaultScissorRect);

    FLOAT blendFactor[4] = {GetParam().r, GetParam().g, GetParam().b, GetParam().a};
    list->OMSetBlendFactor(blendFactor);
    list->OMSetRenderTargets(1, &render_target_view, FALSE, nullptr);

    const float clearColor[] = {0.0f, 0.0f, 1.0f, 1.0f};
    list->ClearRenderTargetView(render_target_view, clearColor, 0, nullptr);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->IASetVertexBuffers(0, 1, &vertexBufferView);
    list->DrawInstanced(3, 1, 0, 0);

    list->Close();
    ID3D12CommandList* cl = list.get();
    queue->ExecuteCommandLists(1, &cl);
    wait_for_completion(queue);
  });
}

INSTANTIATE_TEST_SUITE_P(blendfactortests, blendfactor,
                         ::testing::Values(BlendFactor{0.5f, 0.5f, 0.5f, 0.5f},
                                           BlendFactor{0.8f, 0.6f, 0.4f, 0.2f},
                                           BlendFactor{0.2f, 0.4f, 0.6f, 0.8f}));
