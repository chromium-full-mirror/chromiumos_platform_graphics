// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once
#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include "commandline_args.h"

inline std::filesystem::path get_data_path(const char* extension,
                                           const char* test_type) {
  // some work to find the right image directory
  const ::testing::TestInfo* const test_info =
      ::testing::UnitTest::GetInstance()->current_test_info();
  std::string image_path(dx_tests::cmdline_args::image_base_dir);
  image_path += "/";
  image_path += test_type;
  image_path += "/";
  image_path += std::string(test_info->test_suite_name()) + "/" +
                std::string(test_info->name()) + extension;
  return image_path;
}

inline std::filesystem::path get_data_write_path(const char* extension,
                                                 const char* test_type) {
  // some work to find the right image directory
  const ::testing::TestInfo* const test_info =
      ::testing::UnitTest::GetInstance()->current_test_info();
  std::string image_path(dx_tests::cmdline_args::image_base_write_dir);
  image_path += "/";
  image_path += test_type;
  image_path += "/";
  image_path += std::string(test_info->test_suite_name()) + "/" +
                std::string(test_info->name()) + extension;
  return image_path;
}
