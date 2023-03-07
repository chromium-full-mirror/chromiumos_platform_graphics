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
    float4 color : COLOR;
};

VS_OUT main(float4 pos : POSITION)
{
    VS_OUT vso;
    vso.position = float4(pos.xyz, 1.0);
    // color far quads red and near quads green
    vso.color = pos.z < 0.5 ? float4(1.0, 0.0, 0.0, 1.0) : float4(0.0, 1.0, 0.0, 1.0);
    return vso;
})";

const std::string g_psCode =
    R"(
struct VS_OUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

float4 main(VS_OUT vso) : SV_TARGET
{
    return vso.color;
})";

struct OcclusionTestParams {
  std::string name;
  D3D12_QUERY_TYPE query_type;
  double result;
  Vertex quads[8];
};

static const double kResultDelta = 1.1f;

std::string PrintTestParams(const testing::TestParamInfo<OcclusionTestParams>& param) {
  return param.param.name;
}

using query_base = render_test<DXGI_FORMAT_R8G8B8A8_UNORM, 30, 30>;
class occlusion : public query_base,
                  public testing::WithParamInterface<OcclusionTestParams> {};

TEST_P(occlusion, simple) {
  RunTest([this](IDXGIAdapter*, ID3D12Device* device, ID3D12CommandQueue* queue,
                 ID3D12CommandAllocator* allocator,
                 D3D12_CPU_DESCRIPTOR_HANDLE render_target_view) {
    ShaderOutput<ID3DBlob> vs;
    ShaderOutput<ID3DBlob> ps;
    ASSERT_EQ(S_OK, MakeShaderFromSource("Vertex Shader", "vs_5_0",
                                         g_vsCode.c_str(), &vs));
    ASSERT_EQ(S_OK, MakeShaderFromSource("Pixel Shader", "ps_5_0",
                                         g_psCode.c_str(), &ps));

    DXPointer<ID3D12RootSignature> root_signature = CreateRootSignature(device);
    ASSERT_FALSE(!root_signature);

    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = this->defaultPsoDesc;
    pso_desc.InputLayout = {inputElementDescs, _countof(inputElementDescs)};
    pso_desc.pRootSignature = root_signature.get();
    pso_desc.VS = {reinterpret_cast<UINT8*>(vs.shader->GetBufferPointer()),
                   vs.shader->GetBufferSize()};
    pso_desc.PS = {reinterpret_cast<UINT8*>(ps.shader->GetBufferPointer()),
                   ps.shader->GetBufferSize()};
    pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso_desc.DepthStencilState = {
        true,
        D3D12_DEPTH_WRITE_MASK_ALL,
        D3D12_COMPARISON_FUNC_LESS,
        FALSE,
        D3D12_DEFAULT_STENCIL_READ_MASK,
        D3D12_DEFAULT_STENCIL_WRITE_MASK,
        {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP,
         D3D12_COMPARISON_FUNC_ALWAYS},
        {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP,
         D3D12_COMPARISON_FUNC_ALWAYS}};

    DXPointer<ID3D12PipelineState> pso;
    ASSERT_EQ(S_OK, device->CreateGraphicsPipelineState(
                        &pso_desc, IID_PPV_ARGS(&pso.get())));

    const UINT vertex_buffer_size = sizeof(GetParam().quads);

    DXPointer<ID3D12Resource> vertex_buffer = CreateUploadBuffer(
        device, vertex_buffer_size);
    ASSERT_FALSE(!vertex_buffer);

    UINT8* vertex_data_begin;
    D3D12_RANGE read_range = {0, 0};  // We do not intend to read from this
                                      // resource on the CPU.
    ASSERT_EQ(S_OK,
              vertex_buffer->Map(0, &read_range,
                                 reinterpret_cast<void**>(&vertex_data_begin)));

    memcpy(vertex_data_begin, GetParam().quads, vertex_buffer_size);
    vertex_buffer->Unmap(0, nullptr);

    D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
    vertexBufferView.BufferLocation = vertex_buffer->GetGPUVirtualAddress();
    vertexBufferView.StrideInBytes = sizeof(Vertex);
    vertexBufferView.SizeInBytes = vertex_buffer_size;

    DXPointer<ID3D12DescriptorHeap> dsv_desc_heap;
    D3D12_DESCRIPTOR_HEAP_DESC dsv_desc_heap_desc = {
        D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
    ASSERT_EQ(S_OK, device->CreateDescriptorHeap(
                        &dsv_desc_heap_desc, IID_PPV_ARGS(&dsv_desc_heap.get())));

    D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle =
        dsv_desc_heap->GetCPUDescriptorHandleForHeapStart();

    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {
        DXGI_FORMAT_D32_FLOAT, D3D12_DSV_DIMENSION_TEXTURE2D, D3D12_DSV_FLAG_NONE};
    D3D12_HEAP_PROPERTIES ds_heap_desc = {D3D12_HEAP_TYPE_DEFAULT,
                                          D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                                          D3D12_MEMORY_POOL_UNKNOWN, 1, 1};
    D3D12_RESOURCE_DESC ds_desc = {D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                                   0,
                                   width,
                                   height,
                                   1,
                                   0,
                                   DXGI_FORMAT_D32_FLOAT,
                                   {1, 0},
                                   D3D12_TEXTURE_LAYOUT_UNKNOWN,
                                   D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL};
    DXPointer<ID3D12Resource> ds_buffer;
    D3D12_CLEAR_VALUE depth_clear_value = {};
    depth_clear_value.Format = DXGI_FORMAT_D32_FLOAT;
    depth_clear_value.DepthStencil.Depth = 1.0f;
    depth_clear_value.DepthStencil.Stencil = 0;
    device->CreateCommittedResource(&ds_heap_desc, D3D12_HEAP_FLAG_NONE,
                                    &ds_desc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                    &depth_clear_value,
                                    IID_PPV_ARGS(&ds_buffer.get()));

    device->CreateDepthStencilView(ds_buffer.get(), &dsv_desc, dsv_handle);

    DXPointer<ID3D12QueryHeap> query_heap;
    D3D12_QUERY_HEAP_DESC query_heap_desc = {};
    query_heap_desc.Count = 1;
    query_heap_desc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
    ASSERT_EQ(S_OK, device->CreateQueryHeap(&query_heap_desc,
                                            IID_PPV_ARGS(&query_heap.get())));

    D3D12_RESOURCE_DESC query_result_desc = {D3D12_RESOURCE_DIMENSION_BUFFER,
                                             0,
                                             8,
                                             1,
                                             1,
                                             1,
                                             DXGI_FORMAT_UNKNOWN,
                                             {1, 0},
                                             D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                                             D3D12_RESOURCE_FLAG_NONE};
    D3D12_HEAP_PROPERTIES query_result_heap_desc = {
        D3D12_HEAP_TYPE_CUSTOM, D3D12_CPU_PAGE_PROPERTY_WRITE_BACK,
        D3D12_MEMORY_POOL_L0, 1, 1};
    DXPointer<ID3D12Resource> query_result;
    ASSERT_EQ(S_OK, device->CreateCommittedResource(
                        &query_result_heap_desc, D3D12_HEAP_FLAG_NONE,
                        &query_result_desc, D3D12_RESOURCE_STATE_COPY_DEST,
                        nullptr, IID_PPV_ARGS(&query_result.get())));

    DXPointer<ID3D12GraphicsCommandList> list;
    HRESULT hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           allocator, pso.get(),
                                           __uuidof(ID3D12GraphicsCommandList),
                                           reinterpret_cast<void**>(&list.get()));
    ASSERT_EQ(S_OK, hr);

    D3D12_VIEWPORT viewport = defaultViewport;
    viewport.MaxDepth = 1.0f;

    list->SetGraphicsRootSignature(root_signature.get());
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &defaultScissorRect);

    list->OMSetRenderTargets(1, &render_target_view, FALSE, &dsv_handle);

    const float clear_color[] = {0.0f, 0.0f, 0.0f, 1.0f};
    list->ClearRenderTargetView(render_target_view, clear_color, 0, nullptr);
    list->ClearDepthStencilView(dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0,
                                nullptr);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    list->IASetVertexBuffers(0, 1, &vertexBufferView);
    // draw near quad
    list->DrawInstanced(4, 1, 0, 0);
    // draw far quad
    list->BeginQuery(query_heap.get(), GetParam().query_type, 0);
    list->DrawInstanced(4, 1, 4, 0);
    list->EndQuery(query_heap.get(), GetParam().query_type, 0);

    list->ResolveQueryData(query_heap.get(), GetParam().query_type, 0, 1,
                           query_result.get(), 0);
    D3D12_RESOURCE_BARRIER barrier = {
        D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        D3D12_RESOURCE_BARRIER_FLAG_NONE,
        {{query_result.get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
          D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON}}};
    list->ResourceBarrier(1, &barrier);

    list->Close();
    ID3D12CommandList* cl = list.get();
    queue->ExecuteCommandLists(1, &cl);
    wait_for_completion(queue);

    UINT8* query_data = nullptr;
    read_range.End = 8;
    query_result->Map(0, &read_range, reinterpret_cast<void**>(&query_data));
    if (GetParam().query_type == D3D12_QUERY_TYPE_OCCLUSION) {
      EXPECT_NEAR(static_cast<double>(*reinterpret_cast<UINT64*>(query_data)),
                  GetParam().result, kResultDelta);
    } else if (GetParam().query_type == D3D12_QUERY_TYPE_BINARY_OCCLUSION) {
      if (GetParam().result == 0.0f) {
        EXPECT_EQ(*reinterpret_cast<UINT64*>(query_data), 0);
      } else {
        EXPECT_NE(*reinterpret_cast<UINT64*>(query_data), 0);
      }
    } else {
      FAIL() << "Invalid query type";
    }
    query_result->Unmap(0, nullptr);
  });
}

OcclusionTestParams noOcclusion = {"noOcclusion",
                                   D3D12_QUERY_TYPE_OCCLUSION,
                                   63.0f,
                                   {
                                       {0.1f, 0.6f, 0.3f},
                                       {0.6f, 0.6f, 0.3f},
                                       {0.1f, -0.6f, 0.3f},
                                       {0.6f, -0.6f, 0.3f},
                                       {-0.6f, 0.3f, 0.6f},
                                       {-0.1f, 0.3f, 0.6f},
                                       {-0.6f, -0.3f, 0.6f},
                                       {-0.1f, -0.3f, 0.6f},
                                   }};
OcclusionTestParams binaryNoOcclusion = {"binaryNoOcclusion",
                                         D3D12_QUERY_TYPE_BINARY_OCCLUSION,
                                         1.0f,
                                         {
                                             {0.1f, 0.6f, 0.3f},
                                             {0.6f, 0.6f, 0.3f},
                                             {0.1f, -0.6f, 0.3f},
                                             {0.6f, -0.6f, 0.3f},
                                             {-0.6f, 0.3f, 0.6f},
                                             {-0.1f, 0.3f, 0.6f},
                                             {-0.6f, -0.3f, 0.6f},
                                             {-0.1f, -0.3f, 0.6f},
                                         }};

OcclusionTestParams partialOcclusion = {"partialOcclusion",
                                        D3D12_QUERY_TYPE_OCCLUSION,
                                        36.0f,
                                        {
                                            {-0.3f, 0.6f, 0.3f},
                                            {0.6f, 0.6f, 0.3f},
                                            {-0.3f, -0.6f, 0.3f},
                                            {0.6f, -0.6f, 0.3f},
                                            {-0.6f, 0.3f, 0.6f},
                                            {0.3f, 0.3f, 0.6f},
                                            {-0.6f, -0.3f, 0.6f},
                                            {0.3f, -0.3f, 0.6f},
                                        }};
OcclusionTestParams binaryPartialOcclusion = {"binaryPartialOcclusion",
                                              D3D12_QUERY_TYPE_BINARY_OCCLUSION,
                                              1.0f,
                                              {
                                                  {-0.3f, 0.6f, 0.3f},
                                                  {0.6f, 0.6f, 0.3f},
                                                  {-0.3f, -0.6f, 0.3f},
                                                  {0.6f, -0.6f, 0.3f},
                                                  {-0.6f, 0.3f, 0.6f},
                                                  {0.3f, 0.3f, 0.6f},
                                                  {-0.6f, -0.3f, 0.6f},
                                                  {0.3f, -0.3f, 0.6f},
                                              }};

OcclusionTestParams fullOcclusion = {"fullOcclusion",
                                     D3D12_QUERY_TYPE_OCCLUSION,
                                     0.0f,
                                     {
                                         {-0.6f, 0.6f, 0.3f},
                                         {0.6f, 0.6f, 0.3f},
                                         {-0.6f, -0.6f, 0.3f},
                                         {0.6f, -0.6f, 0.3f},
                                         {-0.3f, 0.3f, 0.6f},
                                         {0.3f, 0.3f, 0.6f},
                                         {-0.3f, -0.3f, 0.6f},
                                         {0.3f, -0.3f, 0.6f},
                                     }};
OcclusionTestParams binaryFullOcclusion = {"binaryFullOcclusion",
                                           D3D12_QUERY_TYPE_BINARY_OCCLUSION,
                                           0.0f,
                                           {
                                               {-0.6f, 0.6f, 0.3f},
                                               {0.6f, 0.6f, 0.3f},
                                               {-0.6f, -0.6f, 0.3f},
                                               {0.6f, -0.6f, 0.3f},
                                               {-0.3f, 0.3f, 0.6f},
                                               {0.3f, 0.3f, 0.6f},
                                               {-0.3f, -0.3f, 0.6f},
                                               {0.3f, -0.3f, 0.6f},
                                           }};

INSTANTIATE_TEST_SUITE_P(query, occlusion,
                         testing::Values(noOcclusion, binaryNoOcclusion,
                                         partialOcclusion, binaryPartialOcclusion,
                                         fullOcclusion, binaryFullOcclusion),
                         &PrintTestParams);
