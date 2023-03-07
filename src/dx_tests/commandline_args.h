// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once
#include <string>

namespace dx_tests {
namespace cmdline_args {
extern bool update_images;
extern const char* image_base_dir;
extern const char* image_base_write_dir;

extern bool regenerate_prebuilt_shader_binaries;
extern bool use_prebuilt_shader_binaries;
extern std::string shader_binary_read_dir;
extern std::string shader_binary_write_dir;
extern std::string fxc_exe;
extern bool display;
}  // namespace cmdline_args
}  // namespace dx_tests
