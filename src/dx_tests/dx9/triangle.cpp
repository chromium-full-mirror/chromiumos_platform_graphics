// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <d3d9.h>
#include <dxgiformat.h>
#include <gtest/gtest.h>
#include <string.h>
#include <windows.h>
#include <functional>
#include <istream>
#include <string>

#include "d3d9_pixel_test_fixture.h"
#include "dx_pointer.h"
#include "dxgi_formats.h"
#include "test_utils.h"

template <DXGI_FORMAT FORMAT, size_t WIDTH, size_t HEIGHT,
          size_t max_num_fuzzy_texels, size_t max_per_channel_texel_difference_percent>
struct fuzzy_image_comparator;

const std::string g_vsCode =
    R"(
struct VS_OUT {
    float4 position: SV_POSITION;
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
float4 main() : SV_TARGET {
  return float4(1.0, 0.0, 0.0, 1.0f);
})";

using simpled3d9triangle = d3d9_render_test<
    D3DFMT_A8R8G8B8, 32, 32,
    fuzzy_image_comparator<TO_PSEUDO_DXGI_FORMAT(D3DFMT_A8R8G8B8), 32, 32, 95, 2>>;

TEST_F(simpled3d9triangle, blit) {
  RunTest([](IDirect3DDevice9Ex* device, IDirect3DSurface9* render_target) {
    ShaderOutput<IDirect3DVertexShader9> vs;
    ShaderOutput<IDirect3DPixelShader9> ps;

    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Vertex Shader", "vs_3_0",
                                         g_vsCode.c_str(), &vs));
    ASSERT_EQ(S_OK, MakeShaderFromSource(device, "Pixel Shader", "ps_3_0",
                                         g_psCode.c_str(), &ps));

    D3DVERTEXELEMENT9 elements[] = {
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        D3DDECL_END()};
    DXPointer<IDirect3DVertexDeclaration9> decl;
    device->CreateVertexDeclaration(elements, &decl.get());
    DXPointer<IDirect3DVertexBuffer9> vb;
    const float triangle[] = {
        0.0f,  0.5f,  0.5f,  // p0
        0.5f,  -0.5f, 0.5f,  // p1
        -0.5f, -0.5f, 0.5f,  // p2
    };
    device->CreateVertexBuffer(sizeof(triangle), 0, 0, D3DPOOL_DEFAULT,
                               &vb.get(), NULL);
    void* dat;
    vb->Lock(0, sizeof(triangle), &dat, 0);
    memcpy(dat, triangle, sizeof(triangle));
    vb->Unlock();

    // Render at 2x the size, then blit back down
    const D3DVIEWPORT9 viewData = {0, 0, width * 2, height * 2, 0.0f, 1.0f};
    DXPointer<IDirect3DSurface9> render_surface;
    device->CreateRenderTarget(width * 2, height * 2, format, D3DMULTISAMPLE_NONE,
                               0, FALSE, &render_surface.get(), nullptr);

    device->SetRenderTarget(0, render_surface.get());
    device->BeginScene();
    device->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_RGBA(0, 0, 0, 255), 1.0f, 0);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
    device->SetViewport(&viewData);
    device->SetVertexShader(vs.shader.get());
    device->SetPixelShader(ps.shader.get());
    device->SetVertexDeclaration(decl.get());
    device->SetStreamSource(0, vb.get(), 0, 12);
    device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1);
    device->EndScene();

    device->StretchRect(render_surface.get(), nullptr, render_target, nullptr,
                        D3DTEXF_LINEAR);
  });
}
