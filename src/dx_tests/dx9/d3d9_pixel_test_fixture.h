// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <d3d9.h>
#include <dxgi.h>
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include "comparator.h"
#include "dx_pointer.h"
#include "dxgi_formats.h"
#include "fuzzy_comparator.h"
#include "window.h"

template <DXGI_FORMAT FORMAT, size_t WIDTH, size_t HEIGHT>
using default_image_comparator = strict_image_comparator<FORMAT, WIDTH, HEIGHT>;

template <D3DFORMAT FORMAT, size_t WIDTH, size_t HEIGHT,
          typename comparator =
              default_image_comparator<TO_PSEUDO_DXGI_FORMAT(FORMAT), WIDTH, HEIGHT>>
class d3d9_texel_test : public ::testing::Test {
 public:
  static const uint32_t width = WIDTH;
  static const uint32_t height = HEIGHT;
  static const D3DFORMAT format = FORMAT;

 protected:
  void RunTest(
      const std::function<void(IDirect3DDevice9Ex*,
                               IDirect3DSurface9* non_rendertarget_surface)>& function) {
    DXPointer<IDirect3D9Ex> d3d9;
    DXPointer<IDirect3DDevice9Ex> deviceEx;

    ASSERT_EQ(S_OK, Direct3DCreate9Ex(D3D_SDK_VERSION, &d3d9.get()));
    D3DPRESENT_PARAMETERS d3dpp;
    ZeroMemory(&d3dpp, sizeof(d3dpp));
    d3dpp.Windowed = TRUE;
    d3dpp.BackBufferCount = 1;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.BackBufferWidth = 200;
    d3dpp.BackBufferHeight = 200;
    d3dpp.BackBufferFormat = D3DFMT_A8R8G8B8;
    d3dpp.hDeviceWindow = NULL;
    d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    ASSERT_EQ(S_OK, d3d9->CreateDeviceEx(0, D3DDEVTYPE_HAL, getTestWindow(),
                                         D3DCREATE_MULTITHREADED |
                                             D3DCREATE_HARDWARE_VERTEXPROCESSING |
                                             D3DCREATE_PUREDEVICE,
                                         &d3dpp, nullptr, &deviceEx.get()));

    DXPointer<IDirect3DSurface9> non_render_surface;
    deviceEx->CreateOffscreenPlainSurface(width, height, FORMAT, D3DPOOL_SYSTEMMEM,
                                          &non_render_surface.get(), NULL);
    function(deviceEx.get(), non_render_surface.get());
    D3DLOCKED_RECT lockrect;
    ASSERT_EQ(S_OK,
              non_render_surface->LockRect(&lockrect, NULL, D3DLOCK_READONLY));

    if (dx_tests::cmdline_args::update_images) {
      auto ret = store_image<TO_PSEUDO_DXGI_FORMAT(FORMAT), WIDTH, HEIGHT>(
          lockrect.pBits, lockrect.Pitch, "dx9");
      ASSERT_TRUE(ret);
    } else {
      std::string err;
      auto img = load_image<TO_PSEUDO_DXGI_FORMAT(FORMAT), WIDTH, HEIGHT>(
          &err, "dx9");
      ASSERT_NE(nullptr, img.get());
      comparator c;
      EXPECT_TRUE(c.compare(lockrect.pBits, lockrect.Pitch, img.get()))
          << c.error_string() << std::endl;
    }
  }
};

template <D3DFORMAT FORMAT, size_t WIDTH, size_t HEIGHT,
          typename comparator =
              default_image_comparator<TO_PSEUDO_DXGI_FORMAT(FORMAT), WIDTH, HEIGHT>>
class d3d9_render_test
    : public d3d9_texel_test<FORMAT, WIDTH, HEIGHT, comparator> {
 public:
 protected:
  void RunTest(const std::function<void(IDirect3DDevice9Ex*,
                                        IDirect3DSurface9* rendertarget)>& function) {
    d3d9_texel_test<FORMAT, WIDTH, HEIGHT, comparator>::RunTest(
        [&function](IDirect3DDevice9Ex* device,
                    IDirect3DSurface9* non_rendertarget_surface) {
          DXPointer<IDirect3DSurface9> render_surface;
          ASSERT_EQ(S_OK, device->CreateRenderTarget(
                              WIDTH, HEIGHT, FORMAT, D3DMULTISAMPLE_NONE, 0,
                              FALSE, &render_surface.get(), nullptr));

          function(device, render_surface.get());
          ASSERT_EQ(S_OK, device->GetRenderTargetData(
                              render_surface.get(), non_rendertarget_surface));
        });
  }
};
