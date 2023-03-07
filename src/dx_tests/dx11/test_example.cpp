// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <d3d11.h>
#include <dxgiformat.h>
#include <gtest/gtest.h>
#include <functional>

#include "pixel_test_fixture.h"

using basicdx11 = render_test<DXGI_FORMAT_R8G8B8A8_UNORM, 10, 10>;

TEST_F(basicdx11, clear) {
  RunTest([](ID3D11Device*, ID3D11DeviceContext* context,
             ID3D11RenderTargetView* view) {
    // Render stuff using device, and context to get it into texture2d.
    const float clear_color[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    context->ClearRenderTargetView(view, clear_color);
  });
}
