// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <d3d11.h>
#include <dxgiformat.h>
#include <gtest/gtest.h>
#include <functional>

#include "pixel_test_fixture.h"

using simplecontext = render_test<DXGI_FORMAT_R8G8B8A8_UNORM, 10, 10>;

TEST_F(simplecontext, type) {
  RunTest([](ID3D11Device*, ID3D11DeviceContext* context,
             ID3D11RenderTargetView* view) {
    // two clear colors: OK (green) FAIL (red)
    const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    const float green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    D3D11_DEVICE_CONTEXT_TYPE ctx_type = context->GetType();
    context->ClearRenderTargetView(
        view, ctx_type == D3D11_DEVICE_CONTEXT_IMMEDIATE ? green : red);
  });
}
