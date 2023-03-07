// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once
#include <dxgi.h>
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include "commandline_args.h"
#include "data_locations.h"
#include "dxgi_formats.h"
#include "image.h"

union compute_header {
  struct {
    uint32_t format;
    uint32_t size;
  } header;
  char data[sizeof(header)];
};
static_assert(sizeof(compute_header) == 2 * sizeof(uint32_t),
              "The compute header size is wrong");

template <DXGI_FORMAT FORMAT, size_t LENGTH>
std::vector<typename format_to_type<FORMAT>::type> load_data(std::string* error,
                                                             const char* test_type) {
  constexpr size_t data_size = sizeof(typename format_to_type<FORMAT>::type) *
                               format_to_type<FORMAT>::num_elements * LENGTH;
  static const size_t total_element_count = format_to_type<FORMAT>::num_elements *
                                            LENGTH;
  std::filesystem::path path = get_data_path(".dat", test_type);
  std::ifstream myfile(path, std::ios::in | std::ios::binary);
  if (!myfile) {
    return std::vector<typename format_to_type<FORMAT>::type>();
  }
  myfile.unsetf(std::ios::skipws);

  std::vector<char> data;
  std::istream_iterator<char> in_iter(myfile);
  std::istream_iterator<char> end_iter;
  data.insert(data.begin(), in_iter, end_iter);
  if (data.size() < sizeof(compute_header)) {
    *error = "Missing header in file";
    return std::vector<typename format_to_type<FORMAT>::type>();
  }
  compute_header h;
  memcpy(&h, data.data(), sizeof(compute_header));

  if (h.header.format != FORMAT) {
    *error = "File format does not match image format";
    return std::vector<typename format_to_type<FORMAT>::type>();
  }
  if (h.header.size != LENGTH) {
    *error = "File width does not match image width";
    return std::vector<typename format_to_type<FORMAT>::type>();
  }
  if (data.size() != data_size + sizeof(compute_header)) {
    *error = "File truncated or overflowed";
    return std::vector<typename format_to_type<FORMAT>::type>();
  }
  std::vector<typename format_to_type<FORMAT>::type> out_data;
  out_data.resize(total_element_count);

  memcpy(out_data.data(), data.data() + sizeof(compute_header),
         data.size() - sizeof(compute_header));

  return out_data;
}

template <DXGI_FORMAT FORMAT, size_t LENGTH>
bool store_data(void* src, const char* test_type) {
  constexpr size_t data_size = sizeof(typename format_to_type<FORMAT>::type) *
                               format_to_type<FORMAT>::num_elements * LENGTH;
  std::filesystem::path path = get_data_write_path(".dat", test_type);
  std::filesystem::create_directories(path.parent_path());
  std::ofstream fout(path, std::ios::out | std::ios::binary);
  compute_header h;
  h.header.format = FORMAT;
  h.header.size = LENGTH;

  fout.write(h.data, sizeof(compute_header));
  fout.write(static_cast<const char*>(src), data_size);
  fout.close();
  return true;
}
