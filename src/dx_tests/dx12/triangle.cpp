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

const std::string g_hsCode =
    R"(
#define MAX_POINTS 3
struct VS_OUT {
  float4 position : SV_POSITION;
};
struct HS_CONSTANT_DATA {
    float Edges[3] : SV_TessFactor;
    float Inside : SV_InsideTessFactor;
};
HS_CONSTANT_DATA ConstantHS(InputPatch<VS_OUT, MAX_POINTS> ip,
           uint PatchID : SV_PrimitiveID) {
  HS_CONSTANT_DATA output;
  output.Edges[0] = 2;
  output.Edges[1] = 2;
  output.Edges[2] = 2;
  output.Inside = 1;
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
HS_OUT main( InputPatch<VS_OUT, MAX_POINTS> ip,
                            uint i : SV_OutputControlPointID,
                            uint PatchID : SV_PrimitiveID)
{
    HS_OUT output;
    output.pos = ip[i].position.xyz;
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
};

[domain("tri")]
DS_OUT main(HS_CONSTANT_DATA input,
            float3 UVW : SV_DomainLocation,
            const OutputPatch<HS_OUT, 3> patch)
{
    DS_OUT output;
    float3 pointPos = UVW.x * patch[0].pos + UVW.y * patch[1].pos + UVW.z * patch[2].pos;
    output.vPosition = float4(pointPos, 1.0f);
    return output;
}
)";

const std::string g_ps2Code =
    R"(
struct DS_OUT{
    float4 vPosition : SV_POSITION;
};
float4 main(DS_OUT ps_in) : SV_TARGET {
  return float4(1.0, 0.0, 0.0, 1.0);
}
)";

using triangle = render_test<DXGI_FORMAT_R8G8B8A8_UNORM, 30, 30>;

TEST_F(triangle, basic) {
  RunTest([this](IDXGIAdapter*, ID3D12Device* device, ID3D12CommandQueue* queue,
                 ID3D12CommandAllocator* allocator,
                 D3D12_CPU_DESCRIPTOR_HANDLE render_target_view) {
    ShaderOutput<ID3DBlob> vs;
    ShaderOutput<ID3DBlob> ps;
    ASSERT_EQ(S_OK, MakeShaderFromSource("Vertex Shader", "vs_5_0",
                                         g_vsCode.c_str(), &vs));
    ASSERT_EQ(S_OK, MakeShaderFromSource("Pixel Shader", "ps_5_0",
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

    list->OMSetRenderTargets(1, &render_target_view, FALSE, nullptr);

    const float clearColor[] = {0.0f, 0.0f, 0.0f, 1.0f};
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

// There is a discrepancy between vendor's line connectivity some cards
// produce 4 connected lines vs 8 connected lines in others in others.Therefore,
// we use an neighborhood comparator that makes images match in both cases

using tesselation =
    render_test<DXGI_FORMAT_R8G8B8A8_UNORM, 30, 30,
                neighborhood_comparator<DXGI_FORMAT_R8G8B8A8_UNORM, 30, 30, 1>>;

TEST_F(tesselation, basic) {
  RunTest([this](IDXGIAdapter*, ID3D12Device* device, ID3D12CommandQueue* queue,
                 ID3D12CommandAllocator* allocator,
                 D3D12_CPU_DESCRIPTOR_HANDLE render_target_view) {
    ShaderOutput<ID3DBlob> vs;
    ShaderOutput<ID3DBlob> hs;
    ShaderOutput<ID3DBlob> ds;
    ShaderOutput<ID3DBlob> ps;
    ASSERT_EQ(S_OK, MakeShaderFromSource("Vertex Shader", "vs_5_0",
                                         g_vsCode.c_str(), &vs));
    ASSERT_EQ(S_OK, MakeShaderFromSource("Hull Shader", "hs_5_0",
                                         g_hsCode.c_str(), &hs));
    ASSERT_EQ(S_OK, MakeShaderFromSource("Domain Shader", "ds_5_0",
                                         g_dsCode.c_str(), &ds));
    ASSERT_EQ(S_OK, MakeShaderFromSource("Pixel Shader", "ps_5_0",
                                         g_ps2Code.c_str(), &ps));

    DXPointer<ID3D12RootSignature> rootSignature = CreateRootSignature(device);
    ASSERT_FALSE(!rootSignature);

    triangle_data td = CreateTriangleVB(device);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = this->defaultPsoDesc;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    psoDesc.InputLayout = {&td.inputElementDesc, 1};
    psoDesc.pRootSignature = rootSignature.get();
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    psoDesc.VS = {reinterpret_cast<UINT8*>(vs.shader->GetBufferPointer()),
                  vs.shader->GetBufferSize()};
    psoDesc.HS = {reinterpret_cast<UINT8*>(hs.shader->GetBufferPointer()),
                  hs.shader->GetBufferSize()};
    psoDesc.DS = {reinterpret_cast<UINT8*>(ds.shader->GetBufferPointer()),
                  ds.shader->GetBufferSize()};
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

    list->SetGraphicsRootSignature(rootSignature.get());
    list->RSSetViewports(1, &defaultViewport);
    list->RSSetScissorRects(1, &defaultScissorRect);

    list->OMSetRenderTargets(1, &render_target_view, FALSE, nullptr);

    const float clearColor[] = {0.0f, 0.0f, 0.0f, 1.0f};
    list->ClearRenderTargetView(render_target_view, clearColor, 0, nullptr);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
    list->IASetVertexBuffers(0, 1, &td.vertexBufferView);
    list->DrawInstanced(3, 1, 0, 0);

    list->Close();
    ID3D12CommandList* cl = list.get();
    queue->ExecuteCommandLists(1, &cl);
    wait_for_completion(queue);
  });
}
