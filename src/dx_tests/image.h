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
#include "compute_data.h"

union header {
  struct {
    uint32_t format;
    uint32_t width;
    uint32_t height;
  } header;
  char data[sizeof(uint32_t) * 3];
};

template <DXGI_FORMAT FORMAT, size_t WIDTH, size_t HEIGHT>
struct image {
  const size_t data_size = sizeof(typename format_to_type<FORMAT>::type) *
                           format_to_type<FORMAT>::num_elements * WIDTH * HEIGHT;
  image() {
    data.resize(WIDTH * HEIGHT * format_to_type<FORMAT>::num_elements);
  }
  typename format_to_type<FORMAT>::type* get_texel(size_t x, size_t y) {
    return &data[WIDTH * format_to_type<FORMAT>::num_elements * y +
                 x * format_to_type<FORMAT>::num_elements];
  }
  std::vector<typename format_to_type<FORMAT>::type> data;
};

template <DXGI_FORMAT FORMAT, size_t WIDTH, size_t HEIGHT>
std::unique_ptr<image<FORMAT, WIDTH, HEIGHT>> load_image(std::string* error,
                                                         const char* test_type) {
  std::filesystem::path path = get_data_path(".img", test_type);
  std::ifstream myfile(path, std::ios::in | std::ios::binary);
  if (!myfile) {
    return nullptr;
  }
  myfile.unsetf(std::ios::skipws);

  std::vector<char> data;
  std::istream_iterator<char> in_iter(myfile);
  std::istream_iterator<char> end_iter;
  data.insert(data.begin(), in_iter, end_iter);
  auto img = std::make_unique<image<FORMAT, WIDTH, HEIGHT>>();
  if (data.size() < sizeof(header)) {
    *error = "Missing header in file";
    return nullptr;
  }
  header h;
  memcpy(&h, data.data(), sizeof(header));

  if (h.header.format != FORMAT) {
    *error = "File format does not match image format";
    return nullptr;
  }
  if (h.header.width != WIDTH) {
    *error = "File width does not match image width";
    return nullptr;
  }
  if (h.header.height != HEIGHT) {
    *error = "File height does not match image width";
    return nullptr;
  }
  if (data.size() != img->data_size + sizeof(uint32_t) * 3) {
    *error = "File truncated or overflowed";
    return nullptr;
  }

  memcpy(img->data.data(), data.data() + sizeof(uint32_t) * 3,
         data.size() - sizeof(uint32_t) * 3);

  return img;
}

template <DXGI_FORMAT FORMAT, size_t WIDTH, size_t HEIGHT>
bool store_image(void* data, uint32_t row_pitch, const char* test_type) {
  std::filesystem::path path = get_data_write_path(".img", test_type);
  std::filesystem::create_directories(path.parent_path());
  std::ofstream fout(path, std::ios::out | std::ios::binary);
  header h;
  h.header.format = static_cast<uint32_t>(FORMAT);
  h.header.width = WIDTH;
  h.header.height = HEIGHT;

  fout.write(h.data, sizeof(header));
  const size_t row_data_size = format_to_type<FORMAT>::num_elements * WIDTH *
                               sizeof(typename format_to_type<FORMAT>::type);
  for (size_t i = 0; i < HEIGHT; ++i) {
    char* first_src_byte = static_cast<char*>(data) + (row_pitch * i);
    fout.write(first_src_byte, row_data_size);
  }
  fout.close();
  return true;
}
