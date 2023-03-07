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
#include "dx_pointer.h"
#include "pixel_test_fixture.h"
#include "root_signature_builder.h"
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

cbuffer vertex_cbv : register(b0)
{
    float Element1 : packoffset(c0);
}

VS_OUT main(float4 pos : POSITION)
{
    VS_OUT vso;
    pos.w = 1.0f;
    vso.position = pos + Element1;
    return vso;
})";

const std::string g_psCode =
    R"(
cbuffer fragment_cbv : register(b1)
{
    float4 Element1 : packoffset(c0);
}

float4 main() : SV_TARGET
{
    return Element1;
})";

using cbv = render_test<DXGI_FORMAT_R8G8B8A8_UNORM, 30, 30,
                        fuzzy_image_comparator<DXGI_FORMAT_R8G8B8A8_UNORM, 30,
                                               30, static_cast<size_t>(~0), 1>>;
static constexpr size_t kSeqeutialBufferBind = 64;
static constexpr size_t kBufferElements = kSeqeutialBufferBind + 4;
static constexpr size_t kBufferBindOffset = kSeqeutialBufferBind * sizeof(float);
static constexpr size_t kBufferSize = sizeof(float) * kBufferElements;
static constexpr size_t kPaddedBufferSize = sizeof(float) *
                                            kSeqeutialBufferBind * 2;

TEST_F(cbv, basicVertexFragment)
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

                DXPointer<ID3D12Resource> cbv;
                D3D12_HEAP_PROPERTIES heap_props = {D3D12_HEAP_TYPE_UPLOAD,
                                                    D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                                                    D3D12_MEMORY_POOL_UNKNOWN, 1, 1};
                D3D12_RESOURCE_DESC desc = {D3D12_RESOURCE_DIMENSION_BUFFER,
                                            0,
                                            kBufferSize, // To use offsets, they have to be
                                                         // 256 bytes apart
                                            1,
                                            1,
                                            1,
                                            DXGI_FORMAT_UNKNOWN,
                                            {1, 0},
                                            D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                                            D3D12_RESOURCE_FLAG_NONE};
                ASSERT_EQ(S_OK, device->CreateCommittedResource(
                                    &heap_props, D3D12_HEAP_FLAG_NONE, &desc,
                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                    IID_PPV_ARGS(&cbv.get())));
                D3D12_RANGE read_range = {0, 0}; // We do not intend to read from this
                                                 // resource on the CPU.

                float cbv_data[kBufferElements] = {};
                cbv_data[0] = 0.1f;
                cbv_data[1] = 0.1f;
                cbv_data[2] = 0.0f;
                cbv_data[3] = 0.0f;

                cbv_data[kSeqeutialBufferBind] = 0.0f;
                cbv_data[kSeqeutialBufferBind + 1] = 1.0f;
                cbv_data[kSeqeutialBufferBind + 2] = 0.5f;
                cbv_data[kSeqeutialBufferBind + 3] = 1.0f;

                float *cbv_data_ptr;
                ASSERT_EQ(S_OK,
                          cbv->Map(0, &read_range, reinterpret_cast<void **>(&cbv_data_ptr)));
                memcpy(cbv_data_ptr, cbv_data, kBufferSize);
                cbv->Unmap(0, nullptr);

                RootSignatureBuilder builder;
                D3D12_ROOT_PARAMETER param;
                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;

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
                list->SetGraphicsRootConstantBufferView(0, cbv->GetGPUVirtualAddress());
                list->SetGraphicsRootConstantBufferView(
                    1, cbv->GetGPUVirtualAddress() + kBufferBindOffset);
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

TEST_F(cbv, basicVertexFragmentInDescriptorHeap)
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

                DXPointer<ID3D12Resource> cbv;
                D3D12_HEAP_PROPERTIES heapProps = {D3D12_HEAP_TYPE_UPLOAD,
                                                   D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                                                   D3D12_MEMORY_POOL_UNKNOWN, 1, 1};
                D3D12_RESOURCE_DESC desc = {D3D12_RESOURCE_DIMENSION_BUFFER,
                                            0,
                                            kPaddedBufferSize, // To use offsets, they have
                                                               // to be 256 bytes apart
                                            1,
                                            1,
                                            1,
                                            DXGI_FORMAT_UNKNOWN,
                                            {1, 0},
                                            D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                                            D3D12_RESOURCE_FLAG_NONE};
                ASSERT_EQ(S_OK, device->CreateCommittedResource(
                                    &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                    IID_PPV_ARGS(&cbv.get())));
                D3D12_RANGE readRange = {0, 0}; // We do not intend to read from this
                                                // resource on the CPU.

                float cbv_data[kBufferElements] = {};
                cbv_data[0] = 0.1f;
                cbv_data[1] = 0.1f;
                cbv_data[2] = 0.0f;
                cbv_data[3] = 0.0f;

                cbv_data[kSeqeutialBufferBind] = 0.0f;
                cbv_data[kSeqeutialBufferBind + 1] = 1.0f;
                cbv_data[kSeqeutialBufferBind + 2] = 0.5f;
                cbv_data[kSeqeutialBufferBind + 3] = 1.0f;

                float *cbv_data_ptr;
                ASSERT_EQ(S_OK,
                          cbv->Map(0, &readRange, reinterpret_cast<void **>(&cbv_data_ptr)));
                memcpy(cbv_data_ptr, cbv_data, kBufferSize);
                cbv->Unmap(0, nullptr);

                RootSignatureBuilder builder;
                D3D12_ROOT_PARAMETER param;
                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                constexpr uint32_t kNumDescriptorRanges = 2;
                param.DescriptorTable.NumDescriptorRanges = kNumDescriptorRanges;

                D3D12_DESCRIPTOR_RANGE range[kNumDescriptorRanges] = {
                    {
                        D3D12_DESCRIPTOR_RANGE_TYPE_CBV, // RangeType
                        1,                               // NumDescriptors
                        0,                               // BaseShaderRegister
                        0,                               // RegisterSpace
                        0,                               // OffsetInDescriptorsFromTableStart
                    },
                    {
                        D3D12_DESCRIPTOR_RANGE_TYPE_CBV,      // RangeType
                        1,                                    // NumDescriptors
                        1,                                    // BaseShaderRegister
                        0,                                    // RegisterSpace
                        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND, // OffsetInDescriptorsFromTableStart
                    }};
                param.DescriptorTable.pDescriptorRanges = range;
                param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                builder.AddParameter(param);
                DXPointer<ID3D12RootSignature> rootSignature = builder.commit(device);
                ASSERT_FALSE(!rootSignature);

                triangle_data t = CreateTriangleVB(device);

                D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = defaultPsoDesc;
                psoDesc.InputLayout = {&t.inputElementDesc, 1};
                psoDesc.pRootSignature = rootSignature.get();
                psoDesc.VS = {reinterpret_cast<UINT8 *>(vs.shader->GetBufferPointer()),
                              vs.shader->GetBufferSize()};
                psoDesc.PS = {reinterpret_cast<UINT8 *>(ps.shader->GetBufferPointer()),
                              ps.shader->GetBufferSize()};

                DXPointer<ID3D12PipelineState> pso;
                ASSERT_EQ(S_OK, device->CreateGraphicsPipelineState(
                                    &psoDesc, IID_PPV_ARGS(&pso.get())));

                DXPointer<ID3D12DescriptorHeap> desc_heap;
                D3D12_DESCRIPTOR_HEAP_DESC desc_heap_desc{};
                desc_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                desc_heap_desc.NumDescriptors = 2;
                desc_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                device->CreateDescriptorHeap(&desc_heap_desc, __uuidof(ID3D12DescriptorHeap),
                                             reinterpret_cast<void **>(&desc_heap.get()));
                D3D12_CPU_DESCRIPTOR_HANDLE heap_start =
                    desc_heap->GetCPUDescriptorHandleForHeapStart();
                D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc;
                cbv_desc.BufferLocation = cbv->GetGPUVirtualAddress();
                cbv_desc.SizeInBytes = 256;

                device->CreateConstantBufferView(&cbv_desc, heap_start);
                cbv_desc.BufferLocation = cbv->GetGPUVirtualAddress() + kBufferBindOffset;
                heap_start.ptr += device->GetDescriptorHandleIncrementSize(
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                device->CreateConstantBufferView(&cbv_desc, heap_start);
                DXPointer<ID3D12GraphicsCommandList> list;
                HRESULT hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                       allocator, pso.get(),
                                                       __uuidof(ID3D12GraphicsCommandList),
                                                       reinterpret_cast<void **>(&list.get()));
                ASSERT_EQ(S_OK, hr);
                ID3D12DescriptorHeap *heap = desc_heap.get();
                list->SetGraphicsRootSignature(rootSignature.get());
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
