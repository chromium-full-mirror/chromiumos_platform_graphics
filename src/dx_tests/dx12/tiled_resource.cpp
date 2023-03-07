// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <string.h>
#include <windows.h>
#include <functional>
#include <string>

#include <d3d12.h>
#include <d3dcommon.h>
#include <dxgiformat.h>
#include <gtest/gtest.h>
#include <windows.h>
#include <functional>
#include <string>
#include "common_test_utils.h"

#include "common_test_utils.h"
#include "dx_pointer.h"
#include "pixel_test_fixture.h"
#include "root_signature_builder.h"
#include "test_utils.h"

namespace
{

    // If the `value` is divisible by the `alignment`, then that value is returned.
    // Otherwise find the next larger multiple of alignment and return that instead.
    template <typename T, typename U>
    constexpr T nextAlignedOffset(const T value, const U alignment)
    {
        return alignment * ((value / alignment) + ((value % alignment) > 0));
    }

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

    const std::string g_psTexturedCode_mip_1 = R"(
struct VS_OUT {
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

Texture2D    myTexture : register(t0);
SamplerState mySampler : register(s0);

float4 main(VS_OUT input) : SV_TARGET {
    return myTexture.Sample(mySampler, input.texCoord, 0, 1.0f);
})";

    const std::string g_psTexturedCode_mip_2 = R"(
struct VS_OUT {
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

Texture2D    myTexture : register(t0);
SamplerState mySampler : register(s0);

float4 main(VS_OUT input) : SV_TARGET {
    return myTexture.Sample(mySampler, input.texCoord, 0, 2.0f);
})";

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

using tiledresources = render_test<DXGI_FORMAT_R8G8B8A8_UNORM, 30, 30>;

TEST_F(tiledresources, unboundtexture)
{

    RunTest([this](IDXGIAdapter *adapter, ID3D12Device *device, ID3D12CommandQueue *queue,
                   ID3D12CommandAllocator *allocator,
                   D3D12_CPU_DESCRIPTOR_HANDLE render_target_view)
            {
                DXGI_ADAPTER_DESC desc;
                adapter->GetDesc(&desc);
                if (desc.VendorId == kMesaLavapipeVendor &&
                    desc.DeviceId == kMesaLavapipeDevice)
                {
                    GTEST_SKIP() << "Skipping due to this not working on Mesa Lavapipe";
                }
                (void)render_target_view;
                (void)allocator;
                D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qualityLevel = {};
                qualityLevel.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                qualityLevel.SampleCount = 1;
                qualityLevel.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_TILED_RESOURCE;

                HRESULT hr = device->CheckFeatureSupport(
                    D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &qualityLevel,
                    sizeof(qualityLevel));
                ASSERT_EQ(hr, S_OK);

                D3D12_RESOURCE_DESC image_create_info = {
                    D3D12_RESOURCE_DIMENSION_TEXTURE2D, // Dimension
                    0,                                  // Alignment
                    64,                                 // Width
                    64,                                 // Height
                    1,                                  // DepthOrArraySlice
                    1,                                  // MipLevels
                    DXGI_FORMAT_R8G8B8A8_UNORM,         // Format
                    {
                        1,                                       // Count
                        0,                                       // Quality
                    },                                           // SampleDesc
                    D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE, // Layout
                    D3D12_RESOURCE_FLAG_NONE,                    // Flags
                };

                DXPointer<ID3D12Resource> texture;
                ASSERT_EQ(S_OK, device->CreateReservedResource(
                                    &image_create_info, D3D12_RESOURCE_STATE_COMMON,
                                    nullptr, IID_PPV_ARGS(&texture.get())));
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
                hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator,
                                               pso.get(), IID_PPV_ARGS(&list.get()));
                ASSERT_EQ(S_OK, hr);

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

// We have an 64KB page F (X is an unbound page).
// The image is mapped so that its backing looks like this:
// +-----+-----+
// |  3  |  4  |
// |  X  |  X  |
// +-----+-----+
// |  1  |  2  |
// |  F  |  F  |
// +-----+-----+
// We want to copy our image to page 1, and then make sure that
// when we render it shows up in page 1 AND page 2.

TEST_F(tiledresources, partialsharedmapping)
{
    RunTest([this](IDXGIAdapter *adapter, ID3D12Device *device, ID3D12CommandQueue *queue,
                   ID3D12CommandAllocator *allocator,
                   D3D12_CPU_DESCRIPTOR_HANDLE render_target_view)
            {
                DXGI_ADAPTER_DESC desc;
                adapter->GetDesc(&desc);
                if (desc.VendorId == kMesaLavapipeVendor &&
                    desc.DeviceId == kMesaLavapipeDevice)
                {
                    GTEST_SKIP() << "Skipping due to this not working on Mesa Lavapipe";
                }
                (void)render_target_view;
                (void)allocator;
                D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qualityLevel = {};
                qualityLevel.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                qualityLevel.SampleCount = 1;
                qualityLevel.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_TILED_RESOURCE;

                HRESULT hr = device->CheckFeatureSupport(
                    D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &qualityLevel,
                    sizeof(qualityLevel));
                ASSERT_EQ(hr, S_OK);

                D3D12_RESOURCE_DESC image_create_info = {
                    D3D12_RESOURCE_DIMENSION_TEXTURE2D, // Dimension
                    0,                                  // Alignment
                    256,                                // Width
                    256,                                // Height
                    1,                                  // DepthOrArraySlice
                    1,                                  // MipLevels
                    DXGI_FORMAT_R8G8B8A8_UNORM,         // Format
                    {
                        1,                                       // Count
                        0,                                       // Quality
                    },                                           // SampleDesc
                    D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE, // Layout
                    D3D12_RESOURCE_FLAG_NONE,                    // Flags
                };

                DXPointer<ID3D12Resource> texture;
                ASSERT_EQ(S_OK, device->CreateReservedResource(
                                    &image_create_info, D3D12_RESOURCE_STATE_COMMON,
                                    nullptr, IID_PPV_ARGS(&texture.get())));

                UINT num_tiles = 0;
                D3D12_PACKED_MIP_INFO packed_mip_info;
                D3D12_TILE_SHAPE ts;
                UINT num_subresource_tilings = 1;
                D3D12_SUBRESOURCE_TILING tiling;
                device->GetResourceTiling(texture.get(), &num_tiles, &packed_mip_info, &ts,
                                          &num_subresource_tilings, 0, &tiling);

                EXPECT_EQ(1u, packed_mip_info.NumStandardMips);
                EXPECT_EQ(0u, packed_mip_info.NumPackedMips);
                EXPECT_EQ(0u, packed_mip_info.NumTilesForPackedMips);
                EXPECT_EQ(0u, packed_mip_info.StartTileIndexInOverallResource);

                EXPECT_EQ(4u, num_tiles);
                EXPECT_EQ(1u, num_subresource_tilings);
                EXPECT_EQ(2u, tiling.WidthInTiles);
                EXPECT_EQ(2u, tiling.HeightInTiles);
                EXPECT_EQ(1u, tiling.DepthInTiles);
                EXPECT_EQ(0u, tiling.StartTileIndexInOverallResource);

                EXPECT_EQ(128u, ts.WidthInTexels);
                EXPECT_EQ(128u, ts.HeightInTexels);
                EXPECT_EQ(1u, ts.DepthInTexels);

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

                D3D12_TILED_RESOURCE_COORDINATE coord{0, 0, 0, 0};
                D3D12_TILE_REGION_SIZE region_size{1, TRUE, 1, 1, 1};
                D3D12_TILE_RANGE_FLAGS flags = D3D12_TILE_RANGE_FLAG_NONE;
                UINT heapRangeStartOffsets = 0;
                UINT rangeTileCounts = 1;

                queue->UpdateTileMappings(texture.get(), 1, &coord, &region_size,
                                          heap.get(), 1, &flags, &heapRangeStartOffsets,
                                          &rangeTileCounts, D3D12_TILE_MAPPING_FLAG_NONE);
                coord.X = 1;
                queue->UpdateTileMappings(texture.get(), 1, &coord, &region_size,
                                          heap.get(), 1, &flags, &heapRangeStartOffsets,
                                          &rangeTileCounts, D3D12_TILE_MAPPING_FLAG_NONE);

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
                hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator,
                                               pso.get(), IID_PPV_ARGS(&list.get()));
                ASSERT_EQ(S_OK, hr);

                auto [img, scratchImg] = CreateAndFillImage<DXGI_FORMAT_R8G8B8A8_UNORM, 256, 256>(
                    device, list.get(), [](size_t x, size_t, size_t c)
                    {
                        static const std::array<uint32_t, 4> colors{
                            0xffed8548, // blue
                            0xff3632db, // red
                            0xff0dc2f4, // yellow
                            0xff54ba3c, // green
                        };
                        return static_cast<uint8_t>((colors[x % colors.size()] >> (c * 8)) & 0xff);
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

                D3D12_BOX b{0, 0, 0, 128, 128, 1};
                list->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, &b);

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

TEST_F(tiledresources, partialunsharedmapping)
{
    RunTest([this](IDXGIAdapter *, ID3D12Device *device, ID3D12CommandQueue *queue,
                   ID3D12CommandAllocator *allocator,
                   D3D12_CPU_DESCRIPTOR_HANDLE render_target_view)
            {
                (void)render_target_view;
                (void)allocator;
                D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qualityLevel = {};
                qualityLevel.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                qualityLevel.SampleCount = 1;
                qualityLevel.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_TILED_RESOURCE;

                HRESULT hr = device->CheckFeatureSupport(
                    D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &qualityLevel,
                    sizeof(qualityLevel));
                ASSERT_EQ(hr, S_OK);

                D3D12_RESOURCE_DESC image_create_info = {
                    D3D12_RESOURCE_DIMENSION_TEXTURE2D, // Dimension
                    0,                                  // Alignment
                    256,                                // Width
                    256,                                // Height
                    1,                                  // DepthOrArraySlice
                    1,                                  // MipLevels
                    DXGI_FORMAT_R8G8B8A8_UNORM,         // Format
                    {
                        1,                                       // Count
                        0,                                       // Quality
                    },                                           // SampleDesc
                    D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE, // Layout
                    D3D12_RESOURCE_FLAG_NONE,                    // Flags
                };

                DXPointer<ID3D12Resource> texture;
                ASSERT_EQ(S_OK, device->CreateReservedResource(
                                    &image_create_info, D3D12_RESOURCE_STATE_COMMON,
                                    nullptr, IID_PPV_ARGS(&texture.get())));

                UINT num_tiles = 0;
                D3D12_PACKED_MIP_INFO packed_mip_info;
                D3D12_TILE_SHAPE ts;
                UINT num_subresource_tilings = 1;
                D3D12_SUBRESOURCE_TILING tiling;
                device->GetResourceTiling(texture.get(), &num_tiles, &packed_mip_info, &ts,
                                          &num_subresource_tilings, 0, &tiling);

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

                D3D12_TILED_RESOURCE_COORDINATE coord{0, 0, 0, 0};
                D3D12_TILE_REGION_SIZE region_size{2, TRUE, 1, 2, 1};
                D3D12_TILE_RANGE_FLAGS flags = D3D12_TILE_RANGE_FLAG_NONE;
                UINT heapRangeStartOffsets = 0;
                UINT rangeTileCounts = 2;

                queue->UpdateTileMappings(texture.get(), 1, &coord, &region_size,
                                          heap.get(), 1, &flags, &heapRangeStartOffsets,
                                          &rangeTileCounts, D3D12_TILE_MAPPING_FLAG_NONE);

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
                hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator,
                                               pso.get(), IID_PPV_ARGS(&list.get()));
                ASSERT_EQ(S_OK, hr);

                auto [img, scratchImg] = CreateAndFillImage<DXGI_FORMAT_R8G8B8A8_UNORM, 256, 256>(
                    device, list.get(), [](size_t x, size_t, size_t c)
                    {
                        static const std::array<uint32_t, 4> colors{
                            0xffed8548, // blue
                            0xff3632db, // red
                            0xff0dc2f4, // yellow
                            0xff54ba3c, // green
                        };
                        return static_cast<uint8_t>((colors[x % colors.size()] >> (c * 8)) & 0xff);
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

                D3D12_BOX b{0, 0, 0, 256, 256, 1};
                list->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, &b);

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

TEST_F(tiledresources, fullmapping)
{
    RunTest([this](IDXGIAdapter *, ID3D12Device *device, ID3D12CommandQueue *queue,
                   ID3D12CommandAllocator *allocator,
                   D3D12_CPU_DESCRIPTOR_HANDLE render_target_view)
            {
                (void)render_target_view;
                (void)allocator;
                D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qualityLevel = {};
                qualityLevel.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                qualityLevel.SampleCount = 1;
                qualityLevel.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_TILED_RESOURCE;

                HRESULT hr = device->CheckFeatureSupport(
                    D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &qualityLevel,
                    sizeof(qualityLevel));
                ASSERT_EQ(hr, S_OK);

                D3D12_RESOURCE_DESC image_create_info = {
                    D3D12_RESOURCE_DIMENSION_TEXTURE2D, // Dimension
                    0,                                  // Alignment
                    256,                                // Width
                    256,                                // Height
                    1,                                  // DepthOrArraySlice
                    1,                                  // MipLevels
                    DXGI_FORMAT_R8G8B8A8_UNORM,         // Format
                    {
                        1,                                       // Count
                        0,                                       // Quality
                    },                                           // SampleDesc
                    D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE, // Layout
                    D3D12_RESOURCE_FLAG_NONE,                    // Flags
                };

                DXPointer<ID3D12Resource> texture;
                ASSERT_EQ(S_OK, device->CreateReservedResource(
                                    &image_create_info, D3D12_RESOURCE_STATE_COMMON,
                                    nullptr, IID_PPV_ARGS(&texture.get())));

                UINT num_tiles = 0;
                D3D12_PACKED_MIP_INFO packed_mip_info;
                D3D12_TILE_SHAPE ts;
                UINT num_subresource_tilings = 1;
                D3D12_SUBRESOURCE_TILING tiling;
                device->GetResourceTiling(texture.get(), &num_tiles, &packed_mip_info, &ts,
                                          &num_subresource_tilings, 0, &tiling);

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

                D3D12_TILED_RESOURCE_COORDINATE coord{0, 0, 0, 0};
                D3D12_TILE_REGION_SIZE region_size{4, FALSE, 0, 0, 0};
                D3D12_TILE_RANGE_FLAGS flags = D3D12_TILE_RANGE_FLAG_NONE;
                UINT heapRangeStartOffsets = 0;
                UINT rangeTileCounts = 4;

                queue->UpdateTileMappings(texture.get(), 1, &coord, &region_size,
                                          heap.get(), 1, &flags, &heapRangeStartOffsets,
                                          &rangeTileCounts, D3D12_TILE_MAPPING_FLAG_NONE);

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
                hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator,
                                               pso.get(), IID_PPV_ARGS(&list.get()));
                ASSERT_EQ(S_OK, hr);

                auto [img, scratchImg] = CreateAndFillImage<DXGI_FORMAT_R8G8B8A8_UNORM, 256, 256>(
                    device, list.get(), [](size_t x, size_t, size_t c)
                    {
                        static const std::array<uint32_t, 4> colors{
                            0xffed8548, // blue
                            0xff3632db, // red
                            0xff0dc2f4, // yellow
                            0xff54ba3c, // green
                        };
                        return static_cast<uint8_t>((colors[x % colors.size()] >> (c * 8)) & 0xff);
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

                D3D12_BOX b{0, 0, 0, 256, 256, 1};
                list->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, &b);

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

TEST_F(tiledresources, tailmips)
{
    RunTest([this](IDXGIAdapter *, ID3D12Device *device, ID3D12CommandQueue *queue,
                   ID3D12CommandAllocator *allocator,
                   D3D12_CPU_DESCRIPTOR_HANDLE render_target_view)
            {
                (void)render_target_view;
                (void)allocator;
                D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qualityLevel = {};
                qualityLevel.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                qualityLevel.SampleCount = 1;
                qualityLevel.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_TILED_RESOURCE;

                HRESULT hr = device->CheckFeatureSupport(
                    D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &qualityLevel,
                    sizeof(qualityLevel));
                ASSERT_EQ(hr, S_OK);

                D3D12_RESOURCE_DESC image_create_info = {
                    D3D12_RESOURCE_DIMENSION_TEXTURE2D, // Dimension
                    0,                                  // Alignment
                    256,                                // Width
                    256,                                // Height
                    1,                                  // DepthOrArraySlice
                    9,                                  // MipLevels
                    DXGI_FORMAT_R8G8B8A8_UNORM,         // Format
                    {
                        1,                                       // Count
                        0,                                       // Quality
                    },                                           // SampleDesc
                    D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE, // Layout
                    D3D12_RESOURCE_FLAG_NONE,                    // Flags
                };

                DXPointer<ID3D12Resource> texture;
                ASSERT_EQ(S_OK, device->CreateReservedResource(
                                    &image_create_info, D3D12_RESOURCE_STATE_COMMON,
                                    nullptr, IID_PPV_ARGS(&texture.get())));

                UINT num_tiles = 0;
                D3D12_PACKED_MIP_INFO packed_mip_info;
                D3D12_TILE_SHAPE ts;
                UINT num_subresource_tilings = 2;
                D3D12_SUBRESOURCE_TILING tiling[2];
                device->GetResourceTiling(texture.get(), &num_tiles, &packed_mip_info, &ts,
                                          &num_subresource_tilings, 0, tiling);

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

                D3D12_TILED_RESOURCE_COORDINATE coord{0, 0, 0, 0};
                D3D12_TILE_REGION_SIZE region_size{4, FALSE, 0, 0, 0};
                D3D12_TILE_RANGE_FLAGS flags = D3D12_TILE_RANGE_FLAG_NONE;
                UINT heapRangeStartOffsets = 0;
                UINT rangeTileCounts = 4;

                queue->UpdateTileMappings(texture.get(), 1, &coord, &region_size,
                                          heap.get(), 1, &flags, &heapRangeStartOffsets,
                                          &rangeTileCounts, D3D12_TILE_MAPPING_FLAG_NONE);

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
                                               g_vsCode.c_str(),
                                               g_psTexturedCode_mip_1.c_str());

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
                srvDesc.Texture2D.MipLevels = 9;
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
                hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator,
                                               pso.get(), IID_PPV_ARGS(&list.get()));
                ASSERT_EQ(S_OK, hr);

                auto [img, scratchImg] = CreateAndFillImage<DXGI_FORMAT_R8G8B8A8_UNORM, 256, 256>(
                    device, list.get(), [](size_t x, size_t, size_t c)
                    {
                        static const std::array<uint32_t, 4> colors{
                            0xffed8548, // blue
                            0xff3632db, // red
                            0xff0dc2f4, // yellow
                            0xff54ba3c, // green
                        };
                        return static_cast<uint8_t>((colors[x % colors.size()] >> (c * 8)) & 0xff);
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

                D3D12_BOX b{0, 0, 0, 256, 256, 1};
                list->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, &b);

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

TEST_F(tiledresources, tailmips1)
{
    RunTest([this](IDXGIAdapter *, ID3D12Device *device, ID3D12CommandQueue *queue,
                   ID3D12CommandAllocator *allocator,
                   D3D12_CPU_DESCRIPTOR_HANDLE render_target_view)
            {
                (void)render_target_view;
                (void)allocator;
                D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qualityLevel = {};
                qualityLevel.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                qualityLevel.SampleCount = 1;
                qualityLevel.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_TILED_RESOURCE;

                HRESULT hr = device->CheckFeatureSupport(
                    D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &qualityLevel,
                    sizeof(qualityLevel));
                ASSERT_EQ(hr, S_OK);

                D3D12_RESOURCE_DESC image_create_info = {
                    D3D12_RESOURCE_DIMENSION_TEXTURE2D, // Dimension
                    0,                                  // Alignment
                    256,                                // Width
                    256,                                // Height
                    1,                                  // DepthOrArraySlice
                    9,                                  // MipLevels
                    DXGI_FORMAT_R8G8B8A8_UNORM,         // Format
                    {
                        1,                                       // Count
                        0,                                       // Quality
                    },                                           // SampleDesc
                    D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE, // Layout
                    D3D12_RESOURCE_FLAG_NONE,                    // Flags
                };

                DXPointer<ID3D12Resource> texture;
                ASSERT_EQ(S_OK, device->CreateReservedResource(
                                    &image_create_info, D3D12_RESOURCE_STATE_COMMON,
                                    nullptr, IID_PPV_ARGS(&texture.get())));

                UINT num_tiles = 0;
                D3D12_PACKED_MIP_INFO packed_mip_info;
                D3D12_TILE_SHAPE ts;
                UINT num_subresource_tilings = 2;
                D3D12_SUBRESOURCE_TILING tiling[2];
                device->GetResourceTiling(texture.get(), &num_tiles, &packed_mip_info, &ts,
                                          &num_subresource_tilings, 0, tiling);

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

                D3D12_TILED_RESOURCE_COORDINATE coord{0, 0, 0, 1};
                D3D12_TILE_REGION_SIZE region_size{1, FALSE, 0, 0, 0};
                D3D12_TILE_RANGE_FLAGS flags = D3D12_TILE_RANGE_FLAG_NONE;
                UINT heapRangeStartOffsets = 0;
                UINT rangeTileCounts = 1;

                queue->UpdateTileMappings(texture.get(), 1, &coord, &region_size,
                                          heap.get(), 1, &flags, &heapRangeStartOffsets,
                                          &rangeTileCounts, D3D12_TILE_MAPPING_FLAG_NONE);

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
                                               g_vsCode.c_str(),
                                               g_psTexturedCode_mip_1.c_str());

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
                srvDesc.Texture2D.MipLevels = 9;
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
                hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator,
                                               pso.get(), IID_PPV_ARGS(&list.get()));
                ASSERT_EQ(S_OK, hr);

                auto [img, scratchImg] = CreateAndFillImage<DXGI_FORMAT_R8G8B8A8_UNORM, 256, 256>(
                    device, list.get(), [](size_t x, size_t, size_t c)
                    {
                        static const std::array<uint32_t, 4> colors{
                            0xffed8548, // blue
                            0xff3632db, // red
                            0xff0dc2f4, // yellow
                            0xff54ba3c, // green
                        };
                        return static_cast<uint8_t>((colors[x % colors.size()] >> (c * 8)) & 0xff);
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
                    {{1}},
                };

                D3D12_BOX b{0, 0, 0, 128, 128, 1};
                list->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, &b);

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

TEST_F(tiledresources, tailmipstail)
{
    RunTest([this](IDXGIAdapter *, ID3D12Device *device, ID3D12CommandQueue *queue,
                   ID3D12CommandAllocator *allocator,
                   D3D12_CPU_DESCRIPTOR_HANDLE render_target_view)
            {
                (void)render_target_view;
                (void)allocator;
                D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qualityLevel = {};
                qualityLevel.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                qualityLevel.SampleCount = 1;
                qualityLevel.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_TILED_RESOURCE;

                HRESULT hr = device->CheckFeatureSupport(
                    D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &qualityLevel,
                    sizeof(qualityLevel));
                ASSERT_EQ(hr, S_OK);

                D3D12_RESOURCE_DESC image_create_info = {
                    D3D12_RESOURCE_DIMENSION_TEXTURE2D, // Dimension
                    0,                                  // Alignment
                    256,                                // Width
                    256,                                // Height
                    1,                                  // DepthOrArraySlice
                    9,                                  // MipLevels
                    DXGI_FORMAT_R8G8B8A8_UNORM,         // Format
                    {
                        1,                                       // Count
                        0,                                       // Quality
                    },                                           // SampleDesc
                    D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE, // Layout
                    D3D12_RESOURCE_FLAG_NONE,                    // Flags
                };

                DXPointer<ID3D12Resource> texture;
                ASSERT_EQ(S_OK, device->CreateReservedResource(
                                    &image_create_info, D3D12_RESOURCE_STATE_COMMON,
                                    nullptr, IID_PPV_ARGS(&texture.get())));

                UINT num_tiles = 0;
                D3D12_PACKED_MIP_INFO packed_mip_info;
                D3D12_TILE_SHAPE ts;
                UINT num_subresource_tilings = 2;
                D3D12_SUBRESOURCE_TILING tiling[2];
                device->GetResourceTiling(texture.get(), &num_tiles, &packed_mip_info, &ts,
                                          &num_subresource_tilings, 0, tiling);
                ASSERT_EQ(2, packed_mip_info.NumStandardMips);
                ASSERT_EQ(7, packed_mip_info.NumPackedMips);

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

                D3D12_TILED_RESOURCE_COORDINATE coord{
                    0, 0, 0, static_cast<UINT>(packed_mip_info.NumStandardMips)};
                D3D12_TILE_REGION_SIZE region_size{packed_mip_info.NumTilesForPackedMips,
                                                   FALSE, 0, 0, 0};

                D3D12_TILE_RANGE_FLAGS flags = D3D12_TILE_RANGE_FLAG_NONE;
                UINT heapRangeStartOffsets = 0;
                UINT rangeTileCounts = packed_mip_info.NumTilesForPackedMips;

                queue->UpdateTileMappings(texture.get(), 1, &coord, &region_size,
                                          heap.get(), 1, &flags, &heapRangeStartOffsets,
                                          &rangeTileCounts, D3D12_TILE_MAPPING_FLAG_NONE);

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
                                               g_vsCode.c_str(),
                                               g_psTexturedCode_mip_2.c_str());

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
                srvDesc.Texture2D.MipLevels = 9;
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
                hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator,
                                               pso.get(), IID_PPV_ARGS(&list.get()));
                ASSERT_EQ(S_OK, hr);

                auto [img, scratchImg] = CreateAndFillImage<DXGI_FORMAT_R8G8B8A8_UNORM, 256, 256>(
                    device, list.get(), [](size_t x, size_t, size_t c)
                    {
                        static const std::array<uint32_t, 4> colors{
                            0xffed8548, // blue
                            0xff3632db, // red
                            0xff0dc2f4, // yellow
                            0xff54ba3c, // green
                        };
                        return static_cast<uint8_t>((colors[x % colors.size()] >> (c * 8)) & 0xff);
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
                    {{2}},
                };

                D3D12_BOX b{0, 0, 0, 64, 64, 1};
                list->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, &b);

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
