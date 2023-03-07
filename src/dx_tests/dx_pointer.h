// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

template <typename T>
struct DXPointer {
  DXPointer() : t(nullptr) {}
  DXPointer(DXPointer&& _other) noexcept {
    t = _other.t;
    _other.t = nullptr;
  }
  ~DXPointer() {
    // This is a problem with clang-tidy. If you have
    // enough code that uses DXPointer in a file this will get hit.
    // The SAME code will pass or fail based on the amount of
    // other code in the file :)
    // NOLINTNEXTLINE(clang-analyzer-core.uninitialized.Branch)
    if (t) {
      t->Release();
    }
  }

  DXPointer& operator=(DXPointer&& _other) noexcept {
    // NOLINTNEXTLINE(clang-analyzer-core.uninitialized.Branch)
    if (t) {
      t->Release();
    }
    t = _other.t;
    _other.t = nullptr;
    return *this;
  }

  T* release() {
    T* temp = t;
    t = nullptr;
    return temp;
  }

  T*& get() { return t; }

  T* operator->() { return t; }

  T* operator*() { return t; }

  explicit operator bool() { return t != nullptr; }

 private:
  T* t = nullptr;
};
