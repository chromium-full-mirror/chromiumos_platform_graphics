// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once
#include <cmath>
#include "dxgi_formats.h"
#include "image.h"

template <DXGI_FORMAT FORMAT, size_t WIDTH, size_t HEIGHT>
class image_comparator_base {
 public:
  const typename format_to_type<FORMAT>::type* get_texel(const void* src,
                                                         uint32_t row_pitch,
                                                         size_t x, size_t y) {
    return reinterpret_cast<const typename format_to_type<FORMAT>::type*>(
        reinterpret_cast<const unsigned char*>(src) + (y * row_pitch) +
        (x * sizeof(typename format_to_type<FORMAT>::type) *
         format_to_type<FORMAT>::num_elements));
  }
  virtual bool compare(const void* src, uint32_t row_pitch,
                       image<FORMAT, WIDTH, HEIGHT>* img) = 0;
  virtual std::string error_string() = 0;
};

template <DXGI_FORMAT FORMAT, size_t WIDTH, size_t HEIGHT>
struct strict_image_comparator : image_comparator_base<FORMAT, WIDTH, HEIGHT> {
 public:
  static const size_t row_data_size = format_to_type<FORMAT>::num_elements * WIDTH *
                                      sizeof(typename format_to_type<FORMAT>::type);
  bool compare(const void* src, uint32_t row_pitch,
               image<FORMAT, WIDTH, HEIGHT>* img) override {
    for (size_t i = 0; i < HEIGHT; ++i) {
      const unsigned char* src_byte = static_cast<const unsigned char*>(src) +
                                      (row_pitch * i);
      const unsigned char* dst_byte = reinterpret_cast<const unsigned char*>(
                                          img->data.data()) +
                                      (i * row_data_size);

      for (size_t j = 0; j < row_data_size; ++j) {
        if (*(src_byte++) != *(dst_byte++)) {
          total_bytes_wrong += 1;
        }
      }
    }
    return total_bytes_wrong == 0;
  }

  std::string error_string() override {
    if (total_bytes_wrong == 0) {
      return "SUCCESS";
    }

    std::string err = std::string(
        "ERROR: The total number of incorrect bytes is: ");
    err += std::to_string(total_bytes_wrong);
    err += " / ";
    err += std::to_string(row_data_size * HEIGHT);
    err += "\n";
    return err;
  }

 private:
  size_t total_bytes_wrong = 0;
};

template <DXGI_FORMAT FORMAT, size_t LENGTH>
class compute_comparator_base {
 public:
  virtual bool compare(
      const void* src,
      const std::vector<typename format_to_type<FORMAT>::type>& dat) = 0;
  virtual std::string error_string() = 0;
};

template <DXGI_FORMAT FORMAT, size_t LENGTH>
struct strict_compute_comparator : compute_comparator_base<FORMAT, LENGTH> {
 public:
  bool compare(const void* src,
               const std::vector<typename format_to_type<FORMAT>::type>& dat) override {
    auto base_ptr = static_cast<const typename format_to_type<FORMAT>::type*>(src);
    for (size_t i = 0; i < LENGTH; ++i) {
      const auto element_idx = format_to_type<FORMAT>::num_elements * i;
      auto src_element = base_ptr + element_idx;
      auto dst_element = &dat[element_idx];
      for (size_t j = 0; j < format_to_type<FORMAT>::num_elements; ++j) {
        if (src_element[j] != dst_element[j]) {
          total_elements_wrong++;
        }
      }
    }
    return total_elements_wrong == 0;
  }

  std::string error_string() override {
    if (total_elements_wrong == 0) {
      return "SUCCESS";
    }

    std::string err = std::string(
        "ERROR: The total number of incorrect elements is: ");
    err += std::to_string(total_elements_wrong);
    err += " / ";
    err += std::to_string(LENGTH * format_to_type<FORMAT>::num_elements);
    err += "\n";
    return err;
  }

 private:
  size_t total_elements_wrong = 0;
};
