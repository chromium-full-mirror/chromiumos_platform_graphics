// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <d3d12.h>
#include <d3dcommon.h>
#include <dxgiformat.h>
#include <gtest/gtest.h>
#include <windows.h>
#include <functional>
#include <string>

#include "common_test_utils.h"
#include "dx_pointer.h"
#include "pixel_test_fixture.h"
#include "root_signature_builder.h"
#include "test_utils.h"

const std::string g_vsCode =
    R"(
struct VS_OUT
{
    float4 position : SV_POSITION;
};

struct root_constants {
    float4 Element1;
};

ConstantBuffer<root_constants> my_root_constants: register(b0);

VS_OUT main(float4 pos : POSITION)
{
    VS_OUT vso;
    pos.w = 1.0f;
    vso.position = pos + my_root_constants.Element1;
    return vso;
})";

const std::string g_psCode =
    R"(

struct root_constants {
    float4 Element1;
};

ConstantBuffer<root_constants> my_root_constants: register(b1);

float4 main() : SV_TARGET
{
    return my_root_constants.Element1;
})";

using rootConstants = render_test<DXGI_FORMAT_R8G8B8A8_UNORM, 30, 30>;

TEST_F(rootConstants, basicVertexFragment) {
  RunTest([this](IDXGIAdapter*, ID3D12Device* device, ID3D12CommandQueue* queue,
                 ID3D12CommandAllocator* allocator,
                 D3D12_CPU_DESCRIPTOR_HANDLE render_target_view) {
    ShaderOutput<ID3DBlob> vs;
    ShaderOutput<ID3DBlob> ps;
    ASSERT_EQ(S_OK, MakeShaderFromSource("Vertex Shader", "vs_5_1",
                                         g_vsCode.c_str(), &vs));
    ASSERT_EQ(S_OK, MakeShaderFromSource("Pixel Shader", "ps_5_1",
                                         g_psCode.c_str(), &ps));
    RootSignatureBuilder builder;
    D3D12_ROOT_PARAMETER param;
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    param.Constants = {0, 0, 4};
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    builder.AddParameter(param);
    param.Constants = {1, 0, 4};
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    builder.AddParameter(param);

    triangle_data t = CreateTriangleVB(device);

    DXPointer<ID3D12RootSignature> rootSignature = builder.commit(device);
    ASSERT_FALSE(!rootSignature);

    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = defaultPsoDesc;
    psoDesc.InputLayout = {inputElementDescs, _countof(inputElementDescs)};
    psoDesc.pRootSignature = rootSignature.get();
    psoDesc.VS = {reinterpret_cast<UINT8*>(vs.shader->GetBufferPointer()),
                  vs.shader->GetBufferSize()};
    psoDesc.PS = {reinterpret_cast<UINT8*>(ps.shader->GetBufferPointer()),
                  ps.shader->GetBufferSize()};

    DXPointer<ID3D12PipelineState> pso;
    ASSERT_EQ(S_OK, device->CreateGraphicsPipelineState(
                        &psoDesc, IID_PPV_ARGS(&pso.get())));

    DXPointer<ID3D12GraphicsCommandList> list;
    HRESULT hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           allocator, pso.get(),
                                           __uuidof(ID3D12GraphicsCommandList),
                                           reinterpret_cast<void**>(&list.get()));
    ASSERT_EQ(S_OK, hr);

    float vertexOffset[4] = {0.1f, -0.1f, 0.0f, 0.0f};
    float fragmentColor[4] = {0.0f, 1.0f, 0.0f, 1.0f};

    list->SetGraphicsRootSignature(rootSignature.get());
    list->RSSetViewports(1, &defaultViewport);
    list->RSSetScissorRects(1, &defaultScissorRect);

    list->SetGraphicsRoot32BitConstants(0, 4, &vertexOffset, 0);
    list->SetGraphicsRoot32BitConstants(1, 4, &fragmentColor, 0);
    list->OMSetRenderTargets(1, &render_target_view, FALSE, nullptr);

    const float clearColor[] = {1.0f, 0.0f, 0.0f, 1.0f};
    list->ClearRenderTargetView(render_target_view, clearColor, 0, nullptr);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->IASetVertexBuffers(0, 1, &t.vertexBufferView);
    list->DrawInstanced(3, 1, 0, 0);

    list->Close();
    ID3D12CommandList* cl = list.get();
    queue->ExecuteCommandLists(1, &cl);
    wait_for_completion(queue);
  });
}
