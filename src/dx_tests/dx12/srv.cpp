// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <d3d12.h>
#include <d3dcommon.h>
#include <dxgiformat.h>
#include <gtest/gtest.h>
#include <stdint.h>
#include <string.h>
#include <windows.h>

#include <functional>
#include <string>

#include "common_test_utils.h"
#include "dx12/root_signature_builder.h"
#include "dx_pointer.h"
#include "pixel_test_fixture.h"
#include "test_utils.h"

template <DXGI_FORMAT FORMAT, size_t WIDTH, size_t HEIGHT,
          size_t max_num_fuzzy_texels, size_t max_per_channel_texel_difference_percent>
struct fuzzy_image_comparator;

const std::string g_vsCode =
    R"(
StructuredBuffer<float4> t0 : register(t0);

struct VS_OUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

VS_OUT main(float4 pos : POSITION)
{
    VS_OUT vso;
    vso.position = pos;
    vso.color = t0[0];
    return vso;
})";

const std::string g_psCode =
    R"(
StructuredBuffer<float4> t1 : register(t1);

struct PS_IN
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

float4 main(PS_IN psi) : SV_TARGET
{
    psi.color.g = t1[0].g;
    return psi.color;
})";

using srv = render_test<DXGI_FORMAT_R8G8B8A8_UNORM, 30, 30,
                        fuzzy_image_comparator<DXGI_FORMAT_R8G8B8A8_UNORM, 30,
                                               30, static_cast<size_t>(~0), 1>>;

// Our SRV data is a float4, therefore 16 bytes.
static constexpr size_t kSrvSize = 16;

TEST_F(srv, basicVertexAndFragment)
{
    RunTest([this](IDXGIAdapter *, ID3D12Device *device, ID3D12CommandQueue *queue,
                   ID3D12CommandAllocator *allocator,
                   D3D12_CPU_DESCRIPTOR_HANDLE render_target_view)
            {
                ShaderOutput<ID3DBlob> vs;
                ShaderOutput<ID3DBlob> ps;
                ASSERT_EQ(S_OK, MakeShaderFromSource("Vertex Shader", "vs_5_1",
                                                     g_vsCode.c_str(), &vs));
                ASSERT_EQ(S_OK, MakeShaderFromSource("Pixel Shader", "ps_5_1",
                                                     g_psCode.c_str(), &ps));

                D3D12_HEAP_PROPERTIES srv_heap_props = {D3D12_HEAP_TYPE_UPLOAD,
                                                        D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                                                        D3D12_MEMORY_POOL_UNKNOWN, 1, 1};
                D3D12_RANGE read_range = {0, 0}; // We do not intend to read from any
                                                 // resources on the CPU.

                DXPointer<ID3D12Resource> vertex_srv;
                D3D12_RESOURCE_DESC vertex_srv_desc = {D3D12_RESOURCE_DIMENSION_BUFFER,
                                                       0,
                                                       kSrvSize,
                                                       1,
                                                       1,
                                                       1,
                                                       DXGI_FORMAT_UNKNOWN,
                                                       {1, 0},
                                                       D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                                                       D3D12_RESOURCE_FLAG_NONE};
                ASSERT_EQ(S_OK, device->CreateCommittedResource(
                                    &srv_heap_props, D3D12_HEAP_FLAG_NONE, &vertex_srv_desc,
                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                    IID_PPV_ARGS(&vertex_srv.get())));

                float vertex_srv_data[4] = {0.0, 0.0, 0.5, 1.0};
                UINT8 *vertex_srv_data_begin = nullptr;
                ASSERT_EQ(S_OK,
                          vertex_srv->Map(0, &read_range,
                                          reinterpret_cast<void **>(&vertex_srv_data_begin)));
                memcpy(vertex_srv_data_begin, vertex_srv_data, sizeof(vertex_srv_data));
                vertex_srv->Unmap(0, nullptr);

                DXPointer<ID3D12Resource> fragment_srv;
                D3D12_RESOURCE_DESC fragment_srv_desc = {D3D12_RESOURCE_DIMENSION_BUFFER,
                                                         0,
                                                         kSrvSize,
                                                         1,
                                                         1,
                                                         1,
                                                         DXGI_FORMAT_UNKNOWN,
                                                         {1, 0},
                                                         D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                                                         D3D12_RESOURCE_FLAG_NONE};
                ASSERT_EQ(S_OK, device->CreateCommittedResource(
                                    &srv_heap_props, D3D12_HEAP_FLAG_NONE,
                                    &fragment_srv_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                    nullptr, IID_PPV_ARGS(&fragment_srv.get())));

                float fragment_srv_data[4] = {0.0, 1.0, 0.0, 1.0};
                UINT8 *fragment_srv_data_begin = nullptr;
                ASSERT_EQ(S_OK, fragment_srv->Map(
                                    0, &read_range,
                                    reinterpret_cast<void **>(&fragment_srv_data_begin)));
                memcpy(fragment_srv_data_begin, fragment_srv_data, sizeof(fragment_srv_data));
                fragment_srv->Unmap(0, nullptr);

                RootSignatureBuilder builder;
                D3D12_ROOT_PARAMETER param = {};
                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
                param.Descriptor = {0, 0};
                param.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
                builder.AddParameter(param);
                param.Descriptor = {1, 0};
                param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
                builder.AddParameter(param);

                DXPointer<ID3D12RootSignature> root_signature = builder.commit(device);
                ASSERT_FALSE(!root_signature);

                triangle_data t = CreateTriangleVB(device);

                D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = defaultPsoDesc;
                pso_desc.InputLayout = {&t.inputElementDesc, 1};
                pso_desc.pRootSignature = root_signature.get();
                pso_desc.VS = {reinterpret_cast<UINT8 *>(vs.shader->GetBufferPointer()),
                               vs.shader->GetBufferSize()};
                pso_desc.PS = {reinterpret_cast<UINT8 *>(ps.shader->GetBufferPointer()),
                               ps.shader->GetBufferSize()};

                DXPointer<ID3D12PipelineState> pso;
                ASSERT_EQ(S_OK, device->CreateGraphicsPipelineState(
                                    &pso_desc, IID_PPV_ARGS(&pso.get())));

                DXPointer<ID3D12GraphicsCommandList> list;
                HRESULT hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                       allocator, pso.get(),
                                                       __uuidof(ID3D12GraphicsCommandList),
                                                       reinterpret_cast<void **>(&list.get()));
                ASSERT_EQ(S_OK, hr);

                list->SetGraphicsRootSignature(root_signature.get());
                list->SetGraphicsRootShaderResourceView(0, vertex_srv->GetGPUVirtualAddress());
                list->SetGraphicsRootShaderResourceView(
                    1, fragment_srv->GetGPUVirtualAddress());

                list->RSSetViewports(1, &defaultViewport);
                list->RSSetScissorRects(1, &defaultScissorRect);

                list->OMSetRenderTargets(1, &render_target_view, FALSE, nullptr);

                const float clearColor[] = {0.0f, 0.0f, 0.0f, 1.0f};
                list->ClearRenderTargetView(render_target_view, clearColor, 0, nullptr);
                list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                list->IASetVertexBuffers(0, 1, &t.vertexBufferView);
                list->DrawInstanced(3, 1, 0, 0);

                list->Close();
                ID3D12CommandList *cl = list.get();
                queue->ExecuteCommandLists(1, &cl);
                wait_for_completion(queue);
            });
}

TEST_F(srv, basicVertexAndFragmentInDescriptorHeap)
{
    RunTest([this](IDXGIAdapter *, ID3D12Device *device, ID3D12CommandQueue *queue,
                   ID3D12CommandAllocator *allocator,
                   D3D12_CPU_DESCRIPTOR_HANDLE render_target_view)
            {
                ShaderOutput<ID3DBlob> vs;
                ShaderOutput<ID3DBlob> ps;
                ASSERT_EQ(S_OK, MakeShaderFromSource("Vertex Shader", "vs_5_1",
                                                     g_vsCode.c_str(), &vs));
                ASSERT_EQ(S_OK, MakeShaderFromSource("Pixel Shader", "ps_5_1",
                                                     g_psCode.c_str(), &ps));

                D3D12_HEAP_PROPERTIES srv_heap_props = {D3D12_HEAP_TYPE_UPLOAD,
                                                        D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                                                        D3D12_MEMORY_POOL_UNKNOWN, 1, 1};
                D3D12_RANGE read_range = {0, 0}; // We do not intend to read from any
                                                 // resources on the CPU.

                DXPointer<ID3D12Resource> vertex_srv;
                D3D12_RESOURCE_DESC vertex_srv_desc = {D3D12_RESOURCE_DIMENSION_BUFFER,
                                                       0,
                                                       kSrvSize,
                                                       1,
                                                       1,
                                                       1,
                                                       DXGI_FORMAT_UNKNOWN,
                                                       {1, 0},
                                                       D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                                                       D3D12_RESOURCE_FLAG_NONE};
                ASSERT_EQ(S_OK, device->CreateCommittedResource(
                                    &srv_heap_props, D3D12_HEAP_FLAG_NONE, &vertex_srv_desc,
                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                    IID_PPV_ARGS(&vertex_srv.get())));

                float vertex_srv_data[4] = {0.0, 0.0, 0.5, 1.0};
                UINT8 *vertex_srv_data_begin = nullptr;
                ASSERT_EQ(S_OK,
                          vertex_srv->Map(0, &read_range,
                                          reinterpret_cast<void **>(&vertex_srv_data_begin)));
                memcpy(vertex_srv_data_begin, vertex_srv_data, sizeof(vertex_srv_data));
                vertex_srv->Unmap(0, nullptr);

                DXPointer<ID3D12Resource> fragment_srv;
                D3D12_RESOURCE_DESC fragment_srv_desc = {D3D12_RESOURCE_DIMENSION_BUFFER,
                                                         0,
                                                         kSrvSize,
                                                         1,
                                                         1,
                                                         1,
                                                         DXGI_FORMAT_UNKNOWN,
                                                         {1, 0},
                                                         D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                                                         D3D12_RESOURCE_FLAG_NONE};
                ASSERT_EQ(S_OK, device->CreateCommittedResource(
                                    &srv_heap_props, D3D12_HEAP_FLAG_NONE,
                                    &fragment_srv_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                    nullptr, IID_PPV_ARGS(&fragment_srv.get())));

                float fragment_srv_data[4] = {0.0, 1.0, 0.0, 1.0};
                UINT8 *fragment_srv_data_begin = nullptr;
                ASSERT_EQ(S_OK, fragment_srv->Map(
                                    0, &read_range,
                                    reinterpret_cast<void **>(&fragment_srv_data_begin)));
                memcpy(fragment_srv_data_begin, fragment_srv_data, sizeof(fragment_srv_data));
                fragment_srv->Unmap(0, nullptr);

                RootSignatureBuilder builder;
                D3D12_ROOT_PARAMETER param = {};
                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                constexpr uint32_t kNumDescriptorRanges = 2;
                param.DescriptorTable.NumDescriptorRanges = kNumDescriptorRanges;

                D3D12_DESCRIPTOR_RANGE ranges[kNumDescriptorRanges] = {
                    {
                        D3D12_DESCRIPTOR_RANGE_TYPE_SRV, // RangeType
                        1,                               // NumDescriptors
                        0,                               // BaseShaderRegister
                        0,                               // RegisterSpace
                        0,                               // OffsetInDescriptorsFromTableStart
                    },
                    {
                        D3D12_DESCRIPTOR_RANGE_TYPE_SRV,      // RangeType
                        1,                                    // NumDescriptors
                        1,                                    // BaseShaderRegister
                        0,                                    // RegisterSpace
                        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND, // OffsetInDescriptorsFromTableStart
                    }};
                param.DescriptorTable.pDescriptorRanges = ranges;
                param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                builder.AddParameter(param);

                DXPointer<ID3D12RootSignature> root_signature = builder.commit(device);
                ASSERT_FALSE(!root_signature);

                triangle_data t = CreateTriangleVB(device);

                D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = defaultPsoDesc;
                pso_desc.InputLayout = {&t.inputElementDesc, 1};
                pso_desc.pRootSignature = root_signature.get();
                pso_desc.VS = {reinterpret_cast<UINT8 *>(vs.shader->GetBufferPointer()),
                               vs.shader->GetBufferSize()};
                pso_desc.PS = {reinterpret_cast<UINT8 *>(ps.shader->GetBufferPointer()),
                               ps.shader->GetBufferSize()};

                DXPointer<ID3D12PipelineState> pso;
                ASSERT_EQ(S_OK, device->CreateGraphicsPipelineState(
                                    &pso_desc, IID_PPV_ARGS(&pso.get())));

                DXPointer<ID3D12DescriptorHeap> desc_heap;
                D3D12_DESCRIPTOR_HEAP_DESC desc_heap_desc{};
                desc_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                desc_heap_desc.NumDescriptors = 2;
                desc_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                device->CreateDescriptorHeap(&desc_heap_desc, __uuidof(ID3D12DescriptorHeap),
                                             reinterpret_cast<void **>(&desc_heap.get()));

                D3D12_CPU_DESCRIPTOR_HANDLE heap_start =
                    desc_heap->GetCPUDescriptorHandleForHeapStart();

                D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
                srv_desc.Format = DXGI_FORMAT_UNKNOWN;
                srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srv_desc.Buffer = {0, 1, kSrvSize, D3D12_BUFFER_SRV_FLAG_NONE};
                device->CreateShaderResourceView(vertex_srv.get(), &srv_desc, heap_start);
                heap_start.ptr += device->GetDescriptorHandleIncrementSize(
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                device->CreateShaderResourceView(fragment_srv.get(), &srv_desc, heap_start);

                DXPointer<ID3D12GraphicsCommandList> list;
                HRESULT hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                       allocator, pso.get(),
                                                       __uuidof(ID3D12GraphicsCommandList),
                                                       reinterpret_cast<void **>(&list.get()));
                ASSERT_EQ(S_OK, hr);

                ID3D12DescriptorHeap *heap = desc_heap.get();
                list->SetGraphicsRootSignature(root_signature.get());
                list->SetDescriptorHeaps(1, &heap);
                list->SetGraphicsRootDescriptorTable(
                    0, heap->GetGPUDescriptorHandleForHeapStart());

                list->RSSetViewports(1, &defaultViewport);
                list->RSSetScissorRects(1, &defaultScissorRect);

                list->OMSetRenderTargets(1, &render_target_view, FALSE, nullptr);

                const float clearColor[] = {0.0f, 0.0f, 0.0f, 1.0f};
                list->ClearRenderTargetView(render_target_view, clearColor, 0, nullptr);
                list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                list->IASetVertexBuffers(0, 1, &t.vertexBufferView);
                list->DrawInstanced(3, 1, 0, 0);

                list->Close();
                ID3D12CommandList *cl = list.get();
                queue->ExecuteCommandLists(1, &cl);
                wait_for_completion(queue);
            });
}
