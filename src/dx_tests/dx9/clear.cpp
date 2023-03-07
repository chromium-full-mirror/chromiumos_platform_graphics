// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <d3d9.h>
#include <gtest/gtest.h>
#include <stddef.h>
#include <functional>

#include "d3d9_pixel_test_fixture.h"

using simpled3d9context = d3d9_render_test<D3DFMT_A8R8G8B8, 32, 32>;

TEST_F(simpled3d9context, clear) {
  RunTest([](IDirect3DDevice9Ex* device, IDirect3DSurface9* render_target) {
    device->SetRenderTarget(0, render_target);
    device->BeginScene();
    // Note: Somehow this is getting flipped.
    device->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_RGBA(128, 255, 64, 255),
                  1.0f, 0);
    device->EndScene();
  });
}
