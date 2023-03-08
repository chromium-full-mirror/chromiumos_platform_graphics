project(dx_tests)
cmake_minimum_required(VERSION 3.21)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
enable_testing()
find_package(GTest)

# Detect Linux
if(UNIX AND NOT APPLE)
  set(LINUX TRUE)
endif()

add_subdirectory(stb_impl)
set(gtest_force_shared_crt on)
add_subdirectory(third_party/googletest)
add_compile_options(
  "/MP" # enable multiprocessor compilation
  "/DNOMINMAX"
  "/wd4127" # conditional expression is constant
  "/WX"
  "-D_CRT_SECURE_NO_WARNINGS"
)

set(TEST_IMAGE_PATH dx_tests/golden_images)
set(TEST_IMAGE_WRITE_PATH dx_tests/golden_images)
set(TEST_SHADER_PATH dx_tests/assets/prebuilt_shaders)

configure_file(test_fixture_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/test_fixture_config.h @ONLY)

set(additional_sources)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(LIBPATH "C:/Program Files (x86)/Windows Kits/10/Lib/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/um/x64")
else(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(LIBPATH "C:/Program Files (x86)/Windows Kits/10/Lib/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/um/x86")
endif(CMAKE_SIZEOF_VOID_P EQUAL 8)

find_library(D3D9LIBRARY d3d9.lib NO_DEFAULT_PATHS NO_SYSTEM_ENVIRONMENT_PATH NO_CMAKE_SYSTEM_PATH PATHS ${LIBPATH})
find_library(D3D10LIBRARY d3d10.lib NO_DEFAULT_PATHS NO_SYSTEM_ENVIRONMENT_PATH NO_CMAKE_SYSTEM_PATH PATHS ${LIBPATH})
find_library(D3D11LIBRARY d3d11.lib NO_DEFAULT_PATHS NO_SYSTEM_ENVIRONMENT_PATH NO_CMAKE_SYSTEM_PATH PATHS ${LIBPATH})
find_library(D3DCOMPILER d3dcompiler.lib NO_DEFAULT_PATHS NO_SYSTEM_ENVIRONMENT_PATH NO_CMAKE_SYSTEM_PATH PATHS ${LIBPATH})
find_library(D3D12LIBRARY d3d12.lib NO_DEFAULT_PATHS NO_SYSTEM_ENVIRONMENT_PATH NO_CMAKE_SYSTEM_PATH PATHS ${LIBPATH})
find_library(DX11DXGILIBRARY dxgi.lib NO_DEFAULT_PATHS NO_SYSTEM_ENVIRONMENT_PATH NO_CMAKE_SYSTEM_PATH PATHS ${LIBPATH})
find_library(DX12DXGILIBRARY dxgi.lib NO_DEFAULT_PATHS NO_SYSTEM_ENVIRONMENT_PATH NO_CMAKE_SYSTEM_PATH PATHS ${LIBPATH})

set(additional_sources
  ${dx_tests_SOURCE_DIR}/third_party/dxvk/src/util/sha1/sha1_util.cpp
  ${dx_tests_SOURCE_DIR}/third_party/dxvk/src/util/sha1/sha1_util.h
  ${dx_tests_SOURCE_DIR}/third_party/dxvk/src/util/sha1/sha1.c
  ${dx_tests_SOURCE_DIR}/third_party/dxvk/src/util/sha1/sha1.h
)

add_executable(DxvkUnitTestApp
  comparator.h
  compute_data.h
  data_locations.h
  dx_pointer.h
  dx11/basic_context.cpp
  dx11/compute_test_fixture.h
  dx11/compute.cpp
  dx11/copy.cpp
  dx11/dual_source_blending.cpp
  dx11/geometry_shader.cpp
  dx11/geometry_shader_stream_output.cpp
  dx11/mipmap_gen.cpp
  dx11/pixel_test_fixture.h
  dx11/query.cpp
  dx11/resolve.cpp
  dx11/stream_output.cpp
  dx11/tess_shader.cpp
  dx11/test_example.cpp
  dx11/triangle.cpp
  dx9/clear.cpp
  dx9/d3d9_pixel_test_fixture.h
  dx9/triangle.cpp
  dxgi_formats.h
  fuzzy_comparator.h
  image.h
  main.cpp
  test_utils.h
  window.cpp
  window.h
  ${additional_sources}
  ${CMAKE_CURRENT_BINARY_DIR}/test_fixture_config.h
)

gtest_discover_tests(DxvkUnitTestApp)

set_target_properties(DxvkUnitTestApp PROPERTIES FOLDER "Unit Tests")
target_include_directories(DxvkUnitTestApp SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/dxvk/src/util/sha1
)
target_include_directories(DxvkUnitTestApp PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}/dx11
)

target_link_libraries(DxvkUnitTestApp PRIVATE gtest ${D3D11LIBRARY} ${D3D9LIBRARY} ${D3DCOMPILER} stb_impl)

add_library(
  dxvk_sha1_c
  STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/dxvk/src/util/sha1/sha1.c
)
add_library(
  dxvk_sha1
  STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/dxvk/src/util/sha1/sha1_util.cpp
)
set_target_properties(dxvk_sha1 dxvk_sha1_c PROPERTIES FOLDER "Unit Tests/Dependencies")

add_executable(DX12UnitTestApp
  comparator.h
  compute_data.h
  data_locations.h
  dx_pointer.h
  dxgi_formats.h
  fuzzy_comparator.h
  image.h
  main.cpp
  dx12/basic_test.cpp
  dx12/blend_factor.cpp
  dx12/buffer_test_fixture.h
  dx12/cbv.cpp
  dx12/command_list.cpp
  dx12/common_test_utils.h
  dx12/copy_test.cpp
  dx12/fence_test.cpp
  dx12/heap.cpp
  dx12/occlusion_query.cpp
  dx12/pixel_test_fixture.h
  dx12/root_constants.cpp
  dx12/root_signature_builder.h
  dx12/simple_render.cpp
  dx12/srv.cpp
  dx12/subresources_test.cpp
  dx12/tiled_resource.cpp
  dx12/triangle.cpp
  ${additional_sources}
  ${CMAKE_CURRENT_BINARY_DIR}/test_fixture_config.h
)
gtest_discover_tests(DX12UnitTestApp)

target_include_directories(
  DX12UnitTestApp PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(
  DX12UnitTestApp SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/dxvk/src/util/sha1
)

target_link_libraries(DX12UnitTestApp PRIVATE gtest ${D3D12LIBRARY} ${DX12DXGILIBRARY} ${D3DCOMPILER} stb_impl dxvk_sha1 dxvk_sha1_c)
set_target_properties(DX12UnitTestApp PROPERTIES FOLDER "Unit Tests")
