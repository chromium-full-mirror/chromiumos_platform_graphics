// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <d3d11.h>
#include <dxgiformat.h>
#include <gtest/gtest.h>
#include <windows.h>
#include <cstdint>
#include <functional>
#include <istream>
#include <string>
#include <vector>

#include "compute_test_fixture.h"
#include "dx_pointer.h"
#include "test_utils.h"

using basicCompute = compute_test<DXGI_FORMAT_R32_UINT, 30>;

const std::string g_cs_add =
    R"(
RWBuffer<uint> Out : register(u0);
RWBuffer<uint> In1: register(u1);
RWBuffer<uint> In2: register(u2);

[numthreads(1, 1, 1)]
void main(uint3 id: SV_DispatchThreadID) {
    Out[id.x] = In1[id.x] + In2[id.x];
})";

TEST_F(basicCompute, addNumbers) {
  RunTest([](ID3D11Device* device, ID3D11DeviceContext* context,
             ID3D11Buffer* buffer) {
    std::vector<uint32_t> initial_numbers;
    for (uint32_t i = 0; i < 30; ++i) {
      initial_numbers.push_back(i);
    }
    DXPointer<ID3D11Buffer> input1;
    DXPointer<ID3D11Buffer> input2;
    ShaderOutput<ID3D11ComputeShader> cs;
    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Compute Shader", "cs_5_0",
                                         g_cs_add.c_str(), &cs))
        << cs.error_messages;

    D3D11_BUFFER_DESC bufferDesc = {
        kDataSize, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, 0, 0};

    D3D11_SUBRESOURCE_DATA dat;
    dat.pSysMem = initial_numbers.data();
    dat.SysMemPitch = sizeof(uint32_t) * 30;
    dat.SysMemSlicePitch = sizeof(uint32_t) * 30;

    ASSERT_EQ(S_OK, device->CreateBuffer(&bufferDesc, &dat, &input1.get()));
    ASSERT_EQ(S_OK, device->CreateBuffer(&bufferDesc, &dat, &input2.get()));

    DXPointer<ID3D11UnorderedAccessView> uavs[3];
    D3D11_UNORDERED_ACCESS_VIEW_DESC desc;
    desc.Format = kFormat;
    desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    desc.Buffer.FirstElement = 0;
    desc.Buffer.Flags = 0;
    desc.Buffer.NumElements = kLength;
    device->CreateUnorderedAccessView(buffer, &desc, &uavs[0].get());
    device->CreateUnorderedAccessView(input1.get(), &desc, &uavs[1].get());
    device->CreateUnorderedAccessView(input2.get(), &desc, &uavs[2].get());

    context->CSSetShader(cs.shader.get(), nullptr, 0);

    ID3D11UnorderedAccessView* views[3] = {uavs[0].get(), uavs[1].get(),
                                           uavs[2].get()};
    context->CSSetUnorderedAccessViews(0, 3, views, nullptr);

    context->Dispatch(30, 1, 1);
  });
}
