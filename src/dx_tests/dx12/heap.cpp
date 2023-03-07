// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <d3d12.h>
#include <d3dcommon.h>
#include <dxgicommon.h>
#include <dxgiformat.h>
#include <gtest/gtest.h>
#include <string.h>
#include <windows.h>
#include <array>
#include <cstdint>
#include <functional>
#include <string>

#include "common_test_utils.h"
#include "dx_pointer.h"
#include "dxgi_formats.h"
#include "pixel_test_fixture.h"
#include "root_signature_builder.h"
#include "test_utils.h"

namespace
{

    const std::string g_vsCode = R"(
struct VS_OUT {
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

VS_OUT main(float4 pos : POSITION) {
    VS_OUT vso;
    pos.w = 1.0f;
    vso.position = pos;
    vso.texCoord = 0.5f * (pos.xy + 1.0f);
    return vso;
})";

    const std::string g_psRedCode = R"(
float4 main() : SV_TARGET {
    return float4(1.0, 0.0, 0.0, 1.0f);
})";

    const std::string g_psTexturedCode = R"(
struct VS_OUT {
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

Texture2D    myTexture : register(t0);
SamplerState mySampler : register(s0);

float4 main(VS_OUT input) : SV_TARGET {
    return myTexture.Sample(mySampler, input.texCoord);
})";

    // If the `value` is divisible by the `alignment`, then that value is returned.
    // Otherwise find the next larger multiple of alignment and return that instead.
    template <typename T, typename U>
    constexpr T nextAlignedOffset(const T value, const U alignment)
    {
        return alignment * ((value / alignment) + ((value % alignment) > 0));
    }

    DXPointer<ID3D12PipelineState> createPipelineState(
        ID3D12Device *device, D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc,
        ID3D12RootSignature *rootSignature, const char *vsSource,
        const char *psSource)
    {
        ShaderOutput<ID3DBlob> vs;
        ShaderOutput<ID3DBlob> ps;
        EXPECT_EQ(S_OK, MakeShaderFromSource("Vertex Shader", "vs_5_0", vsSource, &vs));
        EXPECT_EQ(S_OK, MakeShaderFromSource("Pixel Shader", "ps_5_0", psSource, &ps));

        const D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

        psoDesc.InputLayout = {inputElementDescs, _countof(inputElementDescs)};
        psoDesc.pRootSignature = rootSignature;
        psoDesc.VS = {reinterpret_cast<UINT8 *>(vs.shader->GetBufferPointer()),
                      vs.shader->GetBufferSize()};
        psoDesc.PS = {reinterpret_cast<UINT8 *>(ps.shader->GetBufferPointer()),
                      ps.shader->GetBufferSize()};

        DXPointer<ID3D12PipelineState> pso;
        EXPECT_EQ(S_OK, device->CreateGraphicsPipelineState(
                            &psoDesc, IID_PPV_ARGS(&pso.get())));

        return pso;
    }

} // namespace

using heap = render_test<DXGI_FORMAT_R8G8B8A8_UNORM, 30, 30>;

TEST_F(heap, buffer)
{
    RunTest([this](IDXGIAdapter *, ID3D12Device *device, ID3D12CommandQueue *queue,
                   ID3D12CommandAllocator *allocator,
                   D3D12_CPU_DESCRIPTOR_HANDLE render_target_view)
            {
                auto rootSignature = RootSignatureBuilder().commit(device);
                auto pso = createPipelineState(device, defaultPsoDesc, rootSignature.get(),
                                               g_vsCode.c_str(), g_psRedCode.c_str());

                const D3D12_HEAP_DESC heapDesc{
                    nextAlignedOffset(10'000'000ULL,
                                      D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT), // SizeInBytes
                    D3D12_HEAP_PROPERTIES{
                        D3D12_HEAP_TYPE_UPLOAD,                 // Type
                        D3D12_CPU_PAGE_PROPERTY_UNKNOWN,        // CPUPageProperty
                        D3D12_MEMORY_POOL_UNKNOWN,              // MemoryPoolPreference
                        0,                                      // CreationNodeMask
                        0,                                      // VisibleNodeMask
                    },                                          // Properties
                    D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT, // Alignment
                    D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS,         // Flags
                };

                DXPointer<ID3D12Heap> heap;
                ASSERT_EQ(S_OK, device->CreateHeap(&heapDesc, IID_PPV_ARGS(&heap.get())));

                const Vertex triangleVertices[] = {
                    {0.0f, 0.5f, 0.0f}, {0.5f, -0.5f, 0.0f}, {-0.5f, -0.5f, 0.0f}};
                const UINT vertexBufferSize = sizeof(triangleVertices);
                const D3D12_RESOURCE_DESC resourceDesc{
                    D3D12_RESOURCE_DIMENSION_BUFFER,            // Dimension
                    D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT, // Alignment
                    vertexBufferSize,                           // Width
                    1,                                          // Height
                    1,                                          // DepthOrArraySize
                    1,                                          // MipLevels
                    DXGI_FORMAT_UNKNOWN,                        // Format
                    DXGI_SAMPLE_DESC{
                        1,                          // Count
                        0,                          // Quality
                    },                              // SampleDesc
                    D3D12_TEXTURE_LAYOUT_ROW_MAJOR, // Layout
                    D3D12_RESOURCE_FLAG_NONE,       // Flags
                };

                DXPointer<ID3D12Resource> vertexBuffer;
                ASSERT_EQ(S_OK,
                          device->CreatePlacedResource(
                              heap.get(),
                              nextAlignedOffset(1'000'000ULL,
                                                D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT),
                              &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                              IID_PPV_ARGS(&vertexBuffer.get())));
                ASSERT_FALSE(!vertexBuffer);

                UINT8 *pVertexDataBegin = nullptr;
                D3D12_RANGE readRange = {0, 0}; // We do not intend to read from this
                                                // resource on the CPU.
                ASSERT_EQ(S_OK, vertexBuffer->Map(0, &readRange,
                                                  reinterpret_cast<void **>(&pVertexDataBegin)));

                memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
                vertexBuffer->Unmap(0, nullptr);

                D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
                vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
                vertexBufferView.StrideInBytes = sizeof(Vertex);
                vertexBufferView.SizeInBytes = vertexBufferSize;

                DXPointer<ID3D12GraphicsCommandList> list;
                HRESULT hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                       allocator, pso.get(),
                                                       IID_PPV_ARGS(&list.get()));
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
                ID3D12CommandList *cl = list.get();
                queue->ExecuteCommandLists(1, &cl);
                wait_for_completion(queue);
            });
}

TEST_F(heap, texture)
{
    RunTest([this](IDXGIAdapter *, ID3D12Device *device, ID3D12CommandQueue *queue,
                   ID3D12CommandAllocator *allocator,
                   D3D12_CPU_DESCRIPTOR_HANDLE render_target_view)
            {
                const D3D12_HEAP_DESC heapDesc{
                    nextAlignedOffset(10'000'000ULL,
                                      D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT), // SizeInBytes
                    D3D12_HEAP_PROPERTIES{
                        D3D12_HEAP_TYPE_DEFAULT,                   // Type
                        D3D12_CPU_PAGE_PROPERTY_UNKNOWN,           // CPUPageProperty
                        D3D12_MEMORY_POOL_UNKNOWN,                 // MemoryPoolPreference
                        0,                                         // CreationNodeMask
                        0,                                         // VisibleNodeMask
                    },                                             // Properties
                    D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,    // Alignment
                    D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES, // Flags
                };
                DXPointer<ID3D12Heap> heap;
                ASSERT_EQ(S_OK, device->CreateHeap(&heapDesc, IID_PPV_ARGS(&heap.get())));

                const size_t textureSize = 8;
                const D3D12_RESOURCE_DESC resourceDesc{
                    D3D12_RESOURCE_DIMENSION_TEXTURE2D,         // Dimension
                    D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT, // Alignment
                    textureSize,                                // Width
                    textureSize,                                // Height
                    1,                                          // DepthOrArraySize
                    1,                                          // MipLevels
                    DXGI_FORMAT_R8G8B8A8_UNORM,                 // Format
                    DXGI_SAMPLE_DESC{
                        1,                        // Count
                        0,                        // Quality
                    },                            // SampleDesc
                    D3D12_TEXTURE_LAYOUT_UNKNOWN, // Layout
                    D3D12_RESOURCE_FLAG_NONE,     // Flags
                };
                DXPointer<ID3D12Resource> texture;
                ASSERT_EQ(S_OK, device->CreatePlacedResource(
                                    heap.get(), 0ULL, &resourceDesc,
                                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                                    IID_PPV_ARGS(&texture.get())));
                ASSERT_FALSE(!texture);

                RootSignatureBuilder rootSignatureBuilder;

                D3D12_ROOT_PARAMETER textureParam = {};
                textureParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                D3D12_DESCRIPTOR_RANGE textureRange{
                    D3D12_DESCRIPTOR_RANGE_TYPE_SRV, // RangeType
                    1,                               // NumDescriptors
                    0,                               // BaseShaderRegister
                    0,                               // RegisterSpace
                    0,                               // OffsetInDescriptorsFromTableStart
                };
                textureParam.DescriptorTable = D3D12_ROOT_DESCRIPTOR_TABLE{1, &textureRange};
                textureParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
                rootSignatureBuilder.AddParameter(textureParam);

                D3D12_ROOT_PARAMETER samplerParam = {};
                samplerParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                D3D12_DESCRIPTOR_RANGE samplerRange{
                    D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, // RangeType
                    1,                                   // NumDescriptors
                    0,                                   // BaseShaderRegister
                    0,                                   // RegisterSpace
                    0,                                   // OffsetInDescriptorsFromTableStart
                };
                samplerParam.DescriptorTable = D3D12_ROOT_DESCRIPTOR_TABLE{1, &samplerRange};
                samplerParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
                rootSignatureBuilder.AddParameter(samplerParam);

                auto rootSignature = rootSignatureBuilder.commit(device);
                auto pso = createPipelineState(device, defaultPsoDesc, rootSignature.get(),
                                               g_vsCode.c_str(), g_psTexturedCode.c_str());

                DXPointer<ID3D12DescriptorHeap> texDescHeap;
                D3D12_DESCRIPTOR_HEAP_DESC texDescHeapDesc = {};
                texDescHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                texDescHeapDesc.NumDescriptors = 1;
                texDescHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                device->CreateDescriptorHeap(&texDescHeapDesc,
                                             IID_PPV_ARGS(&texDescHeap.get()));

                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srvDesc.Texture2D = {};
                srvDesc.Texture2D.MipLevels = 1;
                device->CreateShaderResourceView(
                    texture.get(), &srvDesc, texDescHeap->GetCPUDescriptorHandleForHeapStart());

                DXPointer<ID3D12DescriptorHeap> samplerDescHeap;
                D3D12_DESCRIPTOR_HEAP_DESC samplerDescHeapDesc = {};
                samplerDescHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
                samplerDescHeapDesc.NumDescriptors = 1;
                samplerDescHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                device->CreateDescriptorHeap(&samplerDescHeapDesc,
                                             IID_PPV_ARGS(&samplerDescHeap.get()));

                D3D12_SAMPLER_DESC samplerDesc = {};
                samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
                samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                device->CreateSampler(
                    &samplerDesc, samplerDescHeap->GetCPUDescriptorHandleForHeapStart());

                Vertex triangleVertices[] = {
                    {0.0f, 0.5f, 0.0f}, {0.5f, -0.5f, 0.0f}, {-0.5f, -0.5f, 0.0f}};
                const UINT vertexBufferSize = sizeof(triangleVertices);

                DXPointer<ID3D12Resource> vertexBuffer = CreateUploadBuffer(
                    device, vertexBufferSize);
                ASSERT_FALSE(!vertexBuffer);

                UINT8 *pVertexDataBegin = nullptr;
                D3D12_RANGE readRange = {0, 0}; // We do not intend to read from this
                                                // resource on the CPU.
                ASSERT_EQ(S_OK, vertexBuffer->Map(0, &readRange,
                                                  reinterpret_cast<void **>(&pVertexDataBegin)));
                memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
                vertexBuffer->Unmap(0, nullptr);

                D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
                vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
                vertexBufferView.StrideInBytes = sizeof(Vertex);
                vertexBufferView.SizeInBytes = vertexBufferSize;

                DXPointer<ID3D12GraphicsCommandList> list;
                HRESULT hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                       allocator, pso.get(),
                                                       IID_PPV_ARGS(&list.get()));
                ASSERT_EQ(S_OK, hr);

                auto [img, scratchImg] =
                    CreateAndFillImage<DXGI_FORMAT_R8G8B8A8_UNORM, textureSize, textureSize>(
                        device, list.get(), [](size_t x, size_t, size_t c)
                        {
                            static const std::array<uint32_t, 4> colors{
                                0xffed8548, // blue
                                0xff3632db, // red
                                0xff0dc2f4, // yellow
                                0xff54ba3c, // green
                            };
                            return static_cast<uint8_t>((colors[x % colors.size()] >> (c * 8)) &
                                                        0xff);
                        });
                ASSERT_NE(img.get(), nullptr);

                const D3D12_TEXTURE_COPY_LOCATION srcLoc{
                    img.get(),                                 // pResource
                    D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, // Type
                    {{0}},
                };

                const D3D12_TEXTURE_COPY_LOCATION dstLoc{
                    texture.get(),                             // pResource
                    D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, // Type
                    {{0}},
                };
                list->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

                ID3D12DescriptorHeap *const descHeaps[] = {
                    texDescHeap.get(),
                    samplerDescHeap.get(),
                };
                list->SetGraphicsRootSignature(rootSignature.get());
                list->SetDescriptorHeaps(2, descHeaps);
                list->SetGraphicsRootDescriptorTable(
                    0, texDescHeap->GetGPUDescriptorHandleForHeapStart());
                list->SetGraphicsRootDescriptorTable(
                    1, samplerDescHeap->GetGPUDescriptorHandleForHeapStart());
                list->RSSetViewports(1, &defaultViewport);
                list->RSSetScissorRects(1, &defaultScissorRect);

                list->OMSetRenderTargets(1, &render_target_view, FALSE, nullptr);

                const float clearColor[] = {0.0f, 0.0f, 0.0f, 1.0f};
                list->ClearRenderTargetView(render_target_view, clearColor, 0, nullptr);
                list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                list->IASetVertexBuffers(0, 1, &vertexBufferView);
                list->DrawInstanced(3, 1, 0, 0);

                list->Close();
                ID3D12CommandList *cl = list.get();
                queue->ExecuteCommandLists(1, &cl);
                wait_for_completion(queue);
            });
}
