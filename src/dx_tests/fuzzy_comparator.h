// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once
#include <cmath>
#include "comparator.h"
#include "dxgi_formats.h"

template <DXGI_FORMAT FORMAT, size_t WIDTH, size_t HEIGHT,
          size_t max_num_fuzzy_texels, size_t max_per_channel_texel_difference_percent>
struct fuzzy_image_comparator : image_comparator_base<FORMAT, WIDTH, HEIGHT> {
 public:
  using channel_type = typename format_to_type<FORMAT>::type;
  bool compare(const void* data, uint32_t row_pitch,
               image<FORMAT, WIDTH, HEIGHT>* img) override {
    for (size_t i = 0; i < HEIGHT; ++i) {
      for (size_t j = 0; j < WIDTH; ++j) {
        const channel_type* golden_texel = img->get_texel(i, j);
        const channel_type* test_texel =
            image_comparator_base<FORMAT, WIDTH, HEIGHT>::get_texel(
                data, row_pitch, i, j);
        bool incorrect = false;
        bool fail = false;
        for (size_t k = 0; k < format_to_type<FORMAT>::num_elements; ++k) {
          if (golden_texel[k] == test_texel[k]) {
            continue;
          }
          const channel_type channel_difference = std::max(golden_texel[k],
                                                           test_texel[k]) -
                                                  std::min(golden_texel[k],
                                                           test_texel[k]);
          double difference = static_cast<double>(channel_difference) /
                              static_cast<double>(
                                  std::numeric_limits<channel_type>::max());
          // in case we are dealing with float textures or negative diffs
          difference = std::fabs(difference);
          incorrect = true;

          if (difference > double(max_per_channel_texel_difference_percent) / 100.0) {
            fail = true;
          }
          maximum_error = std::max(maximum_error, difference);
        }
        total_texels_with_errors += incorrect;
        total_texels_exceeding_max_error += fail;
      }
    }
    return (total_texels_with_errors <= max_num_fuzzy_texels) &&
           total_texels_exceeding_max_error == 0;
  }

  std::string error_string() override {
    if ((total_texels_with_errors <= max_num_fuzzy_texels) &&
        total_texels_exceeding_max_error == 0) {
      std::string err = std::string("SUCCESS: ");
      err += " ";
      err += std::to_string(total_texels_with_errors);
      err += " texels are non-identical (";
      err += std::to_string(max_num_fuzzy_texels);
      err += " allowed)";
      return err;
    }

    std::string err = std::string("ERROR:");
    if (total_texels_exceeding_max_error) {
      err += " ";
      err += std::to_string(total_texels_exceeding_max_error);
      err += " texels exceeded maximum allowable error (";
      err += std::to_string(max_per_channel_texel_difference_percent);
      err += "%)";
    }

    err += " ";
    err += std::to_string(total_texels_with_errors);
    err += " texels are non-identical (";
    err += std::to_string(max_num_fuzzy_texels);
    err += " allowed)";
    err += " ";
    err += " (maximum total error )" + std::to_string(maximum_error * 100.0);
    err += "%)";
    return err;
  }

 private:
  size_t total_texels_with_errors = 0;
  size_t total_texels_exceeding_max_error = 0;
  double maximum_error = 0.0;
};
