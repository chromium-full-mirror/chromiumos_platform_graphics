// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once
#include <cmath>
#include "comparator.h"
#include "dxgi_formats.h"

template <DXGI_FORMAT FORMAT, size_t WIDTH, size_t HEIGHT, size_t neigbourhood_size>
struct neighborhood_comparator : image_comparator_base<FORMAT, WIDTH, HEIGHT> {
 public:
  using channel_type = typename format_to_type<FORMAT>::type;
  bool compare(const void* data, uint32_t row_pitch,
               image<FORMAT, WIDTH, HEIGHT>* img) override {
    for (size_t i = 0; i < HEIGHT; ++i) {
      for (size_t j = 0; j < WIDTH; ++j) {
        const channel_type* test_texel =
            image_comparator_base<FORMAT, WIDTH, HEIGHT>::get_texel(
                data, row_pitch, i, j);
        // Compare to the corresponding texel
        if (compare_texels(test_texel, img->get_texel(i, j))) {
          continue;
        }
        // Calculate neigborhood limits
        const size_t n_min_j = std::max(static_cast<size_t>(0),
                                        j - neigbourhood_size);
        const size_t n_max_j = std::min(WIDTH - 1, j + neigbourhood_size);
        const size_t n_min_i = std::max(static_cast<size_t>(0),
                                        i - neigbourhood_size);
        const size_t n_max_i = std::min(HEIGHT - 1, i + neigbourhood_size);
        // Traverse neighborhood looking for a matching texel
        bool texel_match = false;
        for (size_t ni = n_min_i; ni <= n_max_i && (!texel_match); ++ni) {
          for (size_t nj = n_min_j; nj <= n_max_j && (!texel_match); ++nj) {
            const channel_type* golden_texel = img->get_texel(ni, nj);
            if (compare_texels(test_texel, golden_texel)) {
              texel_match = true;
            }
          }
        }
        if (texel_match) {
          total_neighborhood_matches++;
        } else {
          total_texels_without_match++;
        }
      }
    }
    return total_texels_without_match == 0;
  }

  std::string error_string() override {
    std::string err;
    if (total_texels_without_match == 0) {
      err = std::string("SUCCESS");
      if (total_neighborhood_matches != 0) {
        err += ": ";
        err += std::to_string(total_neighborhood_matches);
        err += " texels were matched inside a neighborhood";
      }
    } else {
      err = std::string("ERROR: ");
      err += std::to_string(total_texels_without_match);
      err += " texels did not found a match in a ";
      err += std::to_string(2 * neigbourhood_size + 1);
      err += "x";
      err += std::to_string(2 * neigbourhood_size + 1);
      err += " neighborhood";
    }
    return err;
  }

 private:
  size_t total_texels_without_match = 0;
  size_t total_neighborhood_matches = 0;

  bool compare_texels(const channel_type* lhs, const channel_type* rhs) {
    bool bytes_match = true;
    for (size_t k = 0; k < format_to_type<FORMAT>::num_elements; ++k) {
      if (lhs[k] != rhs[k]) {
        bytes_match = false;
        break;
      }
    }
    return bytes_match;
  }
};
