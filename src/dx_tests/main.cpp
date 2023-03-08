// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <stdio.h>
#include <string.h>
#include <chrono>
#include <string>

#include "test_fixture_config.h"

namespace dx_tests
{
  namespace cmdline_args
  {
    bool update_images = false;
    bool regenerate_prebuilt_shader_binaries = false;
    bool use_prebuilt_shader_binaries = false;
    std::string image_base_str = dx_test_config::kTestImagePath;
    std::string image_base_write_str = dx_test_config::kTestImageWritePath;
    std::string shader_binary_read_dir = dx_test_config::kPrebuiltShaderPath;
    std::string shader_binary_write_dir = dx_test_config::kPrebuiltShaderWritePath;
    std::string fxc_exe = "";
    const char *image_base_dir;
    const char *image_base_write_dir;
    bool display = false;
  } // namespace cmdline_args
} // namespace dx_tests

int main(int argc, char **argv)
{

  // loop through argv, and find arguments -regenerate-images.
  for (int i = 0; i < argc; ++i)
  {
    if (!strcmp(argv[i], "-regenerate-images"))
    {
      dx_tests::cmdline_args::update_images = true;
    }
    if (!strcmp(argv[i], "-regenerate-prebuilt-shader-binaries"))
    {
      dx_tests::cmdline_args::regenerate_prebuilt_shader_binaries = true;
    }
    if (!strcmp(argv[i], "-use-prebuilt-shader-binaries"))
    {
      dx_tests::cmdline_args::use_prebuilt_shader_binaries = true;
    }
    if (!strncmp(argv[i], "-image-dir=", 11))
    {
      dx_tests::cmdline_args::image_base_str = argv[i] + 11;
    }
    if (!strncmp(argv[i], "-image-write-dir=", 17))
    {
      dx_tests::cmdline_args::image_base_write_str = argv[i] + 17;
    }
    if (!strncmp(argv[i], "-shader-dir=", 12))
    {
      dx_tests::cmdline_args::shader_binary_read_dir = argv[i] + 12;
    }
    if (!strncmp(argv[i], "-shader-write-dir=", 18))
    {
      dx_tests::cmdline_args::shader_binary_write_dir = argv[i] + 18;
    }
    if (!strncmp(argv[i], "-fxc=", 5))
    {
      dx_tests::cmdline_args::fxc_exe = argv[i] + 5;
    }
    if (!strncmp(argv[i], "-display", 8))
    {
      dx_tests::cmdline_args::display = true;
    }
  }
  dx_tests::cmdline_args::image_base_dir =
      dx_tests::cmdline_args::image_base_str.c_str();
  dx_tests::cmdline_args::image_base_write_dir =
      dx_tests::cmdline_args::image_base_write_str.c_str();

  testing::InitGoogleTest(&argc, argv);
  int result = RUN_ALL_TESTS();

  return result;
}
