// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <d3d11.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#include <stdlib.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>

#include "commandline_args.h"
#include "dx_pointer.h"
#include "sha1_util.h"

template <typename T>
struct ShaderOutput
{
  DXPointer<T> shader;
  std::vector<char> blob_output;
  std::string error_messages;
};

template <typename T, typename Device>
struct compile_shader_helper
{
};

#define MAKE_D3D11_HELPER(typename)                                   \
  template <>                                                         \
  struct compile_shader_helper<ID3D11##typename, ID3D11Device>        \
  {                                                                   \
    static void compiler_fn(ID3D11Device *_device, void *data,        \
                            size_t data_size, ID3D11##typename **out) \
    {                                                                 \
      _device->Create##typename(data, data_size, nullptr, out);       \
    }                                                                 \
  };
MAKE_D3D11_HELPER(PixelShader);
MAKE_D3D11_HELPER(HullShader);
MAKE_D3D11_HELPER(ComputeShader);
MAKE_D3D11_HELPER(DomainShader);
MAKE_D3D11_HELPER(GeometryShader);
MAKE_D3D11_HELPER(VertexShader);
#undef MAKE_D3D11_HELPER

#define MAKE_D3D9_HELPER(typename)                                           \
  template <>                                                                \
  struct compile_shader_helper<IDirect3D##typename##9, IDirect3DDevice9>     \
  {                                                                          \
    static void compiler_fn(IDirect3DDevice9 *_device, void *data, size_t,   \
                            IDirect3D##typename##9 * *out)                   \
    {                                                                        \
      _device->Create##typename(static_cast<const DWORD *>(data), out);      \
    }                                                                        \
  };                                                                         \
  template <>                                                                \
  struct compile_shader_helper<IDirect3D##typename##9, IDirect3DDevice9Ex>   \
  {                                                                          \
    static void compiler_fn(IDirect3DDevice9Ex *_device, void *data, size_t, \
                            IDirect3D##typename##9 * *out)                   \
    {                                                                        \
      _device->Create##typename(static_cast<const DWORD *>(data), out);      \
    }                                                                        \
  };
MAKE_D3D9_HELPER(PixelShader);
MAKE_D3D9_HELPER(VertexShader);
#undef MAKE_D3D9_HELPER

template <typename T, typename Device>
HRESULT MakeShaderFromSource(Device *device, const char *name, const char *model,
                             const char *source, ShaderOutput<T> *out)
{
  if (dx_tests::cmdline_args::regenerate_prebuilt_shader_binaries)
  {
#ifdef _WIN32
    // Intentionally using fxc.exe instead of D3DCompile, because D3DCompile
    // might redirect to SPIR-V
    size_t source_size = strlen(source);
    dxvk::Sha1Hash hash;
    hash = hash.compute(source, source_size);
    std::string filename = hash.toString() + "." + model;
    std::string dir = dx_tests::cmdline_args::shader_binary_write_dir;
    std::replace(std::begin(dir), std::end(dir), '/', '\\');
    std::filesystem::path path = dir;
    std::filesystem::create_directories(path);
    path /= filename;

    std::error_code c;
    std::filesystem::remove(path, c);

    std::filesystem::path source_path = path;
    source_path += ".srctmp";
    std::ofstream fout(source_path, std::ios::out | std::ios::binary);
    fout.write(source, source_size);
    fout.close();
    Sleep(100);
    SECURITY_ATTRIBUTES saAttr;
    ZeroMemory(&saAttr, sizeof(saAttr));
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);

    std::string fxc = dx_tests::cmdline_args::fxc_exe;
    if (fxc.empty())
    {
      LPSTR filePart;
      char fxc_path[MAX_PATH];
      if (!SearchPathA(NULL, "fxc", ".exe", MAX_PATH, fxc_path, &filePart))
      {
        EXPECT_TRUE(false) << "Could not find fxc.exe in path";
        std::filesystem::remove(source_path);
        return E_FAIL;
      }
      fxc = fxc_path;
    }

    PROCESS_INFORMATION pinf;
    ZeroMemory(&pinf, sizeof(pinf));
    std::string command_line = "/C \"\"" + fxc + std::string("\" /T ") + model +
                               " /Fo \"" + path.string() + "\" \"" +
                               source_path.string() + "\"\"";
    char cmd[2048];
    memcpy(cmd, command_line.c_str(), command_line.size() + 1);
    DWORD ret = CreateProcessA("C:\\Windows\\system32\\cmd.exe", cmd, nullptr,
                               nullptr, false, 0, 0, nullptr, &si, &pinf);
    DWORD last_error = GetLastError();
    EXPECT_NE(0u, ret) << "Return error code: " << last_error;
    if (ret == 0)
    {
      std::filesystem::remove(source_path);
      return E_FAIL;
    }

    WaitForSingleObject(pinf.hProcess, INFINITE);
    std::filesystem::remove(source_path);
    DWORD exit_code;
    if (FALSE == GetExitCodeProcess(pinf.hProcess, &exit_code))
    {
      return E_FAIL;
    }
    if (exit_code != 0)
    {
      return E_FAIL;
    }

#else
    EXPECT_FALSE(true) << "Could not regenerate shader binaries, not on "
                          "windows";
#endif
  }

  if (dx_tests::cmdline_args::regenerate_prebuilt_shader_binaries ||
      dx_tests::cmdline_args::use_prebuilt_shader_binaries)
  {
    std::filesystem::path path =
        dx_tests::cmdline_args::regenerate_prebuilt_shader_binaries
            ? dx_tests::cmdline_args::shader_binary_write_dir
            : dx_tests::cmdline_args::shader_binary_read_dir;
    size_t source_size = strlen(source);
    dxvk::Sha1Hash hash;
    hash = hash.compute(source, source_size);
    std::string filename = hash.toString() + "." + model;

    std::ifstream myfile(path / filename, std::ios::in | std::ios::binary);
    if (!myfile)
    {
      EXPECT_TRUE(false) << "Could not open file " << (path / filename).string()
                         << "you may need to regenerate shaders";
      out->error_messages = "Could not open file";
      return E_FAIL;
    }
    myfile.unsetf(std::ios::skipws);
    std::istream_iterator<char> in_iter(myfile);
    std::istream_iterator<char> end_iter;
    out->blob_output.insert(out->blob_output.begin(), in_iter, end_iter);
  }
  else
  {
    DXPointer<ID3DBlob> output;
    DXPointer<ID3DBlob> errors;
    EXPECT_EQ(S_OK, D3DCompile(source, strlen(source), name, nullptr, nullptr,
                               "main", model, 0, 0, &output.get(), &errors.get()))
        << static_cast<const char *>(errors->GetBufferPointer());
    if (!output)
    {
      return E_FAIL;
    }
    if (errors)
    {
      out->error_messages = static_cast<const char *>(errors->GetBufferPointer());
    }
    out->blob_output.resize(output->GetBufferSize());
    memcpy(out->blob_output.data(), output->GetBufferPointer(),
           output->GetBufferSize());
  }

  if (!out->blob_output.empty())
  {
    compile_shader_helper<T, Device>::compiler_fn(
        device, out->blob_output.data(), out->blob_output.size(),
        &out->shader.get());
  }
  if (!out->shader)
  {
    return E_FAIL;
  }
  return S_OK;
}

// TODO(awoloszyn): Fix this once SPIR-V is supported in D3D9 contexts.
template <typename T>
HRESULT MakeShaderFromSource(IDirect3DDevice9Ex *device, const char *name,
                             const char *model, const char *source,
                             ShaderOutput<T> *out)
{
  (void)name;
  if (dx_tests::cmdline_args::regenerate_prebuilt_shader_binaries)
  {
#ifdef _WIN32
    // Intentionally using fxc.exe instead of D3DCompile, because D3DCompile
    // might redirect to SPIR-V
    size_t source_size = strlen(source);
    dxvk::Sha1Hash hash;
    hash = hash.compute(source, source_size);
    std::string filename = hash.toString() + "." + model;
    std::string dir = dx_tests::cmdline_args::shader_binary_write_dir;
    std::replace(std::begin(dir), std::end(dir), '/', '\\');
    std::filesystem::path path = dir;
    std::filesystem::create_directories(path);
    path /= filename;

    std::error_code c;
    std::filesystem::remove(path, c);

    std::filesystem::path source_path = path;
    source_path += ".srctmp";
    std::ofstream fout(source_path, std::ios::out | std::ios::binary);
    fout.write(source, source_size);
    fout.close();
    Sleep(100);
    SECURITY_ATTRIBUTES saAttr;
    ZeroMemory(&saAttr, sizeof(saAttr));
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);

    std::string fxc = dx_tests::cmdline_args::fxc_exe;
    if (fxc.empty())
    {
      LPSTR filePart;
      char fxc_path[MAX_PATH];
      if (!SearchPathA(NULL, "fxc", ".exe", MAX_PATH, fxc_path, &filePart))
      {
        EXPECT_TRUE(false) << "Could not find fxc.exe in path";
        std::filesystem::remove(source_path);
        return E_FAIL;
      }
      fxc = fxc_path;
    }

    PROCESS_INFORMATION pinf;
    ZeroMemory(&pinf, sizeof(pinf));
    std::string command_line = "/C \"\"" + fxc + std::string("\" /T ") + model +
                               " /Fo \"" + path.string() + "\" \"" +
                               source_path.string() + "\"\"";
    char cmd[2048];
    memcpy(cmd, command_line.c_str(), command_line.size() + 1);
    DWORD ret = CreateProcessA("C:\\Windows\\system32\\cmd.exe", cmd, nullptr,
                               nullptr, false, 0, 0, nullptr, &si, &pinf);
    DWORD last_error = GetLastError();
    EXPECT_NE(0u, ret) << "Return error code: " << last_error;
    if (ret == 0)
    {
      std::filesystem::remove(source_path);
      return E_FAIL;
    }

    WaitForSingleObject(pinf.hProcess, INFINITE);
    std::filesystem::remove(source_path);
    DWORD exit_code;
    if (FALSE == GetExitCodeProcess(pinf.hProcess, &exit_code))
    {
      return E_FAIL;
    }
    if (exit_code != 0)
    {
      return E_FAIL;
    }

#else
    EXPECT_FALSE(true) << "Could not regenerate shader binaries, not on "
                          "windows";
#endif
  }

  std::filesystem::path path = dx_tests::cmdline_args::regenerate_prebuilt_shader_binaries
                                   ? dx_tests::cmdline_args::shader_binary_write_dir
                                   : dx_tests::cmdline_args::shader_binary_read_dir;
  size_t source_size = strlen(source);
  dxvk::Sha1Hash hash;
  hash = hash.compute(source, source_size);
  std::string filename = hash.toString() + "." + model;

  std::ifstream myfile(path / filename, std::ios::in | std::ios::binary);
  if (!myfile)
  {
    EXPECT_TRUE(false) << "Could not open file " << (path / filename).string()
                       << "you may need to regenerate shaders";
    out->error_messages = "Could not open file";
    return E_FAIL;
  }
  myfile.unsetf(std::ios::skipws);
  std::istream_iterator<char> in_iter(myfile);
  std::istream_iterator<char> end_iter;
  out->blob_output.insert(out->blob_output.begin(), in_iter, end_iter);

  if (!out->blob_output.empty())
  {
    compile_shader_helper<T, IDirect3DDevice9Ex>::compiler_fn(
        device, out->blob_output.data(), out->blob_output.size(),
        &out->shader.get());
  }
  if (!out->shader)
  {
    return E_FAIL;
  }
  return S_OK;
}

// D3D12 specialization; no shader object expected, just blob. out->blob_output
// isn't filled in since out->shader is a blob.
inline HRESULT MakeShaderFromSource(const char *name, const char *model,
                                    const char *source,
                                    ShaderOutput<ID3DBlob> *out)
{
  if (dx_tests::cmdline_args::regenerate_prebuilt_shader_binaries ||
      dx_tests::cmdline_args::use_prebuilt_shader_binaries)
  {
    EXPECT_FALSE(true) << "D3D12 does not support prebuilt shader binaries yet";
  }

  DXPointer<ID3DBlob> errors;
  EXPECT_EQ(S_OK,
            D3DCompile(source, strlen(source), name, nullptr, nullptr, "main",
                       model, 0, 0, &out->shader.get(), &errors.get()))
      << static_cast<const char *>(errors->GetBufferPointer());
  if (errors)
  {
    out->error_messages = static_cast<const char *>(errors->GetBufferPointer());
  }
  if (!out->shader)
  {
    return E_FAIL;
  }

  return S_OK;
}

const uint32_t kMesaLavapipeVendor = 0x10005;
const uint32_t kMesaLavapipeDevice = 0x0;
