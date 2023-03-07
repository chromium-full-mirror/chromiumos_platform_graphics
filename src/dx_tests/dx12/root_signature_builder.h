// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once
#include <d3d12.h>
#include <vector>
#include "dx_pointer.h"

class RootSignatureBuilder {
 public:
  explicit RootSignatureBuilder()
      : desc_({0, nullptr, 0, nullptr,
               D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT}) {}

  RootSignatureBuilder& AddParameter(D3D12_ROOT_PARAMETER parameter) {
    parameters_.push_back(parameter);
    return *this;
  }

  DXPointer<ID3D12RootSignature> commit(ID3D12Device* device) {
    DXPointer<ID3D12RootSignature> rootSignature;
    DXPointer<ID3DBlob> signature;
    DXPointer<ID3DBlob> error;
    desc_.NumParameters = static_cast<UINT>(parameters_.size());
    desc_.pParameters = parameters_.data();
    if (S_OK != D3D12SerializeRootSignature(&desc_, D3D_ROOT_SIGNATURE_VERSION_1,
                                            &signature.get(), &error.get())) {
      return DXPointer<ID3D12RootSignature>();
    }
    if (S_OK != device->CreateRootSignature(0, signature->GetBufferPointer(),
                                            signature->GetBufferSize(),
                                            IID_PPV_ARGS(&rootSignature.get()))) {
      return DXPointer<ID3D12RootSignature>();
    }
    return rootSignature;
  }

 private:
  D3D12_ROOT_SIGNATURE_DESC desc_;
  std::vector<D3D12_ROOT_PARAMETER> parameters_;
};
