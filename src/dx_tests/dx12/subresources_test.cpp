// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <d3d12.h>
#include <d3dcommon.h>
#include <dxgicommon.h>
#include <dxgiformat.h>
#include <gtest/gtest.h>
#include <windows.h>

#include <array>
#include <cstdint>
#include <vector>

#include "dx_pointer.h"

////////////////////////////////////////////////////////////////////////////////

// Fixture to create and hold a device.
class SubresourcesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0,
                                         __uuidof(ID3D12Device2),
                                         reinterpret_cast<void**>(&pDevice));
    ASSERT_EQ(hr, S_OK);
  }

  void TearDown() override {
    if (!pDevice) {
      return;
    }
    //EXPECT_EQ(pDevice->AddRef(), 2U);
    // If this does not equal 1 the test will leak the device.
    //EXPECT_EQ(pDevice->Release(), 1u);
  }

  DXPointer<ID3D12Device2> pDevice;
};

// Template class used in parameterized test suites.
template <typename ParamType>
class SubresourcesTestWithParam : public SubresourcesTest,
                                  public testing::WithParamInterface<ParamType> {
 protected:
  virtual void SetUp() override {
    SubresourcesTest::SetUp();
    param = this->GetParam();
  }

  ParamType param;
};

////////////////////////////////////////////////////////////////////////////////

struct TestResults {
  std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts;
  std::vector<uint32_t> num_rows;
  std::vector<uint64_t> row_sizes_in_bytes;
  uint64_t total_bytes;
};

struct TestParams {
  DXGI_FORMAT format;
  uint32_t flag_index;
  uint32_t width;
  uint32_t height;
  uint16_t depth;
  D3D12_RESOURCE_DIMENSION dim;
};

static void ValidateResults(const TestResults& obtained,
                            const TestResults& expected) {
  EXPECT_EQ(obtained.layouts.size(), expected.layouts.size());
  EXPECT_EQ(obtained.num_rows.size(), expected.num_rows.size());
  EXPECT_EQ(obtained.row_sizes_in_bytes.size(), expected.row_sizes_in_bytes.size());

  for (uint32_t i = 0; i < obtained.layouts.size(); ++i) {
    EXPECT_EQ(obtained.layouts[i].Offset, expected.layouts[i].Offset);

    EXPECT_EQ(obtained.layouts[i].Footprint.Format,
              expected.layouts[i].Footprint.Format);

    EXPECT_EQ(obtained.layouts[i].Footprint.Width,
              expected.layouts[i].Footprint.Width);

    EXPECT_EQ(obtained.layouts[i].Footprint.Height,
              expected.layouts[i].Footprint.Height);

    EXPECT_EQ(obtained.layouts[i].Footprint.Depth,
              expected.layouts[i].Footprint.Depth);

    EXPECT_EQ(obtained.layouts[i].Footprint.RowPitch,
              expected.layouts[i].Footprint.RowPitch);

    EXPECT_EQ(obtained.num_rows[i], expected.num_rows[i]);

    EXPECT_EQ(obtained.row_sizes_in_bytes[i], expected.row_sizes_in_bytes[i]);
  }
  EXPECT_EQ(obtained.total_bytes, expected.total_bytes);
}

namespace copyableFootprintsTests {

const std::vector<TestParams> testParams = {
    {DXGI_FORMAT_R32G32B32A32_FLOAT, 3, 2878, 2568, 125,
     D3D12_RESOURCE_DIMENSION_TEXTURE2D},
    {DXGI_FORMAT_R8G8B8A8_UNORM, 1, 948, 1051, 2041,
     D3D12_RESOURCE_DIMENSION_TEXTURE3D},
    {DXGI_FORMAT_R16_FLOAT, 2, 966, 1660, 1, D3D12_RESOURCE_DIMENSION_TEXTURE2D},
    {DXGI_FORMAT_R16G16_TYPELESS, 0, 6668, 8821, 1,
     D3D12_RESOURCE_DIMENSION_TEXTURE2D},
    {DXGI_FORMAT_R32G32_TYPELESS, 6, 3186, 13720, 1,
     D3D12_RESOURCE_DIMENSION_TEXTURE2D},
    {DXGI_FORMAT_R32_UINT, 2, 793, 45, 405, D3D12_RESOURCE_DIMENSION_TEXTURE3D},
    {DXGI_FORMAT_BC1_TYPELESS, 6, 404, 1160, 802, D3D12_RESOURCE_DIMENSION_TEXTURE3D},
    {DXGI_FORMAT_BC1_UNORM, 0, 1144, 1596, 1134, D3D12_RESOURCE_DIMENSION_TEXTURE3D},
    {DXGI_FORMAT_BC2_UNORM, 6, 6976, 1032, 1, D3D12_RESOURCE_DIMENSION_TEXTURE2D},
    {DXGI_FORMAT_BC3_UNORM_SRGB, 5, 1372, 2020, 1433,
     D3D12_RESOURCE_DIMENSION_TEXTURE3D},
};

const std::vector<TestResults> results = {
    {
        {
            // layouts
            {0, {DXGI_FORMAT_R32G32B32A32_FLOAT, 2878, 2568, 1, 46080}},
            {118333440, {DXGI_FORMAT_R32G32B32A32_FLOAT, 1439, 1284, 1, 23040}},
            {147916800, {DXGI_FORMAT_R32G32B32A32_FLOAT, 719, 642, 1, 11520}},
            {155312640, {DXGI_FORMAT_R32G32B32A32_FLOAT, 359, 321, 1, 5888}},
            {157202944, {DXGI_FORMAT_R32G32B32A32_FLOAT, 179, 160, 1, 3072}},
        },
        {
            2568,
            1284,
            642,
            321,
            160,
        },  // numrows
        {
            46048,
            23024,
            11504,
            5744,
            2864,
        },         // rowsizes
        157694256  // total bytes
    },
    {
        {
            // layouts
            {0, {DXGI_FORMAT_R8G8B8A8_UNORM, 948, 1051, 2041, 3840}},
            {3942182400, {DXGI_FORMAT_R8G8B8A8_UNORM, 474, 525, 1020, 2048}},
            {5038886400, {DXGI_FORMAT_R8G8B8A8_UNORM, 237, 262, 510, 1024}},
            {5175713280, {DXGI_FORMAT_R8G8B8A8_UNORM, 118, 131, 255, 512}},
            {5192816640, {DXGI_FORMAT_R8G8B8A8_UNORM, 59, 65, 127, 256}},
        },
        {
            1051,
            525,
            262,
            131,
            65,
        },  // numrows
        {
            3792,
            1896,
            948,
            472,
            236,
        },          // rowsizes
        5194929900  // total bytes
    },
    {
        {
            // layouts
            {0, {DXGI_FORMAT_R16_FLOAT, 966, 1660, 1, 2048}},
            {3399680, {DXGI_FORMAT_R16_FLOAT, 483, 830, 1, 1024}},
            {4249600, {DXGI_FORMAT_R16_FLOAT, 241, 415, 1, 512}},
            {4462080, {DXGI_FORMAT_R16_FLOAT, 120, 207, 1, 256}},
            {4515328, {DXGI_FORMAT_R16_FLOAT, 60, 103, 1, 256}},
        },
        {
            1660,
            830,
            415,
            207,
            103,
        },  // numrows
        {
            1932,
            966,
            482,
            240,
            120,
        },       // rowsizes
        4541560  // total bytes
    },
    {
        {
            // layouts
            {0, {DXGI_FORMAT_R16G16_TYPELESS, 6668, 8821, 1, 26880}},
            {237108736, {DXGI_FORMAT_R16G16_TYPELESS, 3334, 4410, 1, 13568}},
            {296943616, {DXGI_FORMAT_R16G16_TYPELESS, 1667, 2205, 1, 6912}},
            {312184832, {DXGI_FORMAT_R16G16_TYPELESS, 833, 1102, 1, 3584}},
            {316134400, {DXGI_FORMAT_R16G16_TYPELESS, 416, 551, 1, 1792}},
        },
        {
            8821,
            4410,
            2205,
            1102,
            551,
        },  // numrows
        {
            26672,
            13336,
            6668,
            3332,
            1664,
        },         // rowsizes
        317121664  // total bytes
    },
    {
        {
            // layouts
            {0, {DXGI_FORMAT_R32G32_TYPELESS, 3186, 13720, 1, 25600}},
            {351232000, {DXGI_FORMAT_R32G32_TYPELESS, 1593, 6860, 1, 12800}},
            {439040000, {DXGI_FORMAT_R32G32_TYPELESS, 796, 3430, 1, 6400}},
            {460992000, {DXGI_FORMAT_R32G32_TYPELESS, 398, 1715, 1, 3328}},
            {466699776, {DXGI_FORMAT_R32G32_TYPELESS, 199, 857, 1, 1792}},
        },
        {
            13720,
            6860,
            3430,
            1715,
            857,
        },  // numrows
        {
            25488,
            12744,
            6368,
            3184,
            1592,
        },         // rowsizes
        468235320  // total bytes
    },
    {
        {
            // layouts
            {0, {DXGI_FORMAT_R32_UINT, 793, 45, 405, 3328}},
            {60653056, {DXGI_FORMAT_R32_UINT, 396, 22, 202, 1792}},
            {68616704, {DXGI_FORMAT_R32_UINT, 198, 11, 101, 1024}},
            {69754368, {DXGI_FORMAT_R32_UINT, 99, 5, 50, 512}},
            {69882368, {DXGI_FORMAT_R32_UINT, 49, 2, 25, 256}},
        },
        {
            45,
            22,
            11,
            5,
            2,
        },  // numrows
        {
            3172,
            1584,
            792,
            396,
            196,
        },        // rowsizes
        69895108  // total bytes
    },
    {
        {
            // layouts
            {0, {DXGI_FORMAT_BC1_TYPELESS, 404, 1160, 802, 1024}},
            {238161920, {DXGI_FORMAT_BC1_TYPELESS, 204, 580, 401, 512}},
            {267932160, {DXGI_FORMAT_BC1_TYPELESS, 104, 292, 200, 256}},
            {271669760, {DXGI_FORMAT_BC1_TYPELESS, 52, 148, 100, 256}},
            {272616960, {DXGI_FORMAT_BC1_TYPELESS, 28, 72, 50, 256}},
        },
        {
            290,
            145,
            73,
            37,
            18,
        },  // numrows
        {
            808,
            408,
            208,
            104,
            56,
        },         // rowsizes
        272847160  // total bytes
    },
    {
        {
            // layouts
            {0, {DXGI_FORMAT_BC1_UNORM, 1144, 1596, 1134, 2304}},
            {1042481664, {DXGI_FORMAT_BC1_UNORM, 572, 800, 567, 1280}},
            {1187633664, {DXGI_FORMAT_BC1_UNORM, 288, 400, 283, 768}},
            {1209368064, {DXGI_FORMAT_BC1_UNORM, 144, 200, 141, 512}},
            {1212977664, {DXGI_FORMAT_BC1_UNORM, 72, 100, 70, 256}},
        },
        {
            399,
            200,
            100,
            50,
            25,
        },  // numrows
        {
            2288,
            1144,
            576,
            288,
            144,
        },          // rowsizes
        1213425552  // total bytes
    },
    {
        {
            // layouts
            {0, {DXGI_FORMAT_BC2_UNORM, 6976, 1032, 1, 27904}},
            {7199232, {DXGI_FORMAT_BC2_UNORM, 3488, 516, 1, 14080}},
            {9015808, {DXGI_FORMAT_BC2_UNORM, 1744, 260, 1, 7168}},
            {9481728, {DXGI_FORMAT_BC2_UNORM, 872, 132, 1, 3584}},
            {9600000, {DXGI_FORMAT_BC2_UNORM, 436, 64, 1, 1792}},
        },
        {
            258,
            129,
            65,
            33,
            16,
        },  // numrows
        {
            27904,
            13952,
            6976,
            3488,
            1744,
        },       // rowsizes
        9628624  // total bytes
    },
    {
        {
            // layouts
            {0, {DXGI_FORMAT_BC3_UNORM_SRGB, 1372, 2020, 1433, 5632}},
            {4075681280, {DXGI_FORMAT_BC3_UNORM_SRGB, 688, 1012, 716, 2816}},
            {4585794048, {DXGI_FORMAT_BC3_UNORM_SRGB, 344, 508, 358, 1536}},
            {4655629824, {DXGI_FORMAT_BC3_UNORM_SRGB, 172, 252, 179, 768}},
            {4664290816, {DXGI_FORMAT_BC3_UNORM_SRGB, 88, 128, 89, 512}},
        },
        {
            505,
            253,
            127,
            63,
            32,
        },  // numrows
        {
            5488,
            2752,
            1376,
            688,
            352,
        },          // rowsizes
        4665748832  // total bytes
    },
};

}  // namespace copyableFootprintsTests

////////////////////////////////////////////////////////////////////////////////

struct GetCopyableFootprintsTestParam {
  TestParams params;
  TestResults expected;
};

class GetCopyableFootprintsTest
    : public SubresourcesTestWithParam<GetCopyableFootprintsTestParam> {};

TEST_P(GetCopyableFootprintsTest, positiveTests) {
  std::array<D3D12_RESOURCE_FLAGS, 8> flags = {
      D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE,
      D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER,
      D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS,
      D3D12_RESOURCE_FLAG_VIDEO_DECODE_REFERENCE_ONLY};

  TestParams p = param.params;

  // Fill a descritor using the test parameters
  constexpr uint32_t sub_resource_count = 5;
  D3D12_RESOURCE_DESC desc;
  desc.Dimension = p.dim;
  desc.Alignment = 0;
  desc.Width = p.width;
  desc.Height = p.height;
  desc.DepthOrArraySize = p.depth;
  desc.MipLevels = sub_resource_count;
  desc.Format = p.format;
  desc.SampleDesc = {1, 0};
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  desc.Flags = flags[p.flag_index];

  // Allocate struct to hold the results
  TestResults results;
  results.layouts.resize(sub_resource_count);
  results.num_rows.resize(sub_resource_count);
  results.row_sizes_in_bytes.resize(sub_resource_count);

  // Make the test
  pDevice->GetCopyableFootprints(&desc, 0, sub_resource_count, 0,
                                 results.layouts.data(), results.num_rows.data(),
                                 results.row_sizes_in_bytes.data(),
                                 &results.total_bytes);

  // Verify the results
  ValidateResults(results, param.expected);
}

INSTANTIATE_TEST_SUITE_P(
    GetCopyableFootprintsTestSuite, GetCopyableFootprintsTest,
    ::testing::Values(
        GetCopyableFootprintsTestParam{copyableFootprintsTests::testParams[0],
                                       copyableFootprintsTests::results[0]},
        GetCopyableFootprintsTestParam{copyableFootprintsTests::testParams[1],
                                       copyableFootprintsTests::results[1]},
        GetCopyableFootprintsTestParam{copyableFootprintsTests::testParams[2],
                                       copyableFootprintsTests::results[2]},
        GetCopyableFootprintsTestParam{copyableFootprintsTests::testParams[3],
                                       copyableFootprintsTests::results[3]},
        GetCopyableFootprintsTestParam{copyableFootprintsTests::testParams[4],
                                       copyableFootprintsTests::results[4]},
        GetCopyableFootprintsTestParam{copyableFootprintsTests::testParams[5],
                                       copyableFootprintsTests::results[5]},
        GetCopyableFootprintsTestParam{copyableFootprintsTests::testParams[6],
                                       copyableFootprintsTests::results[6]},
        GetCopyableFootprintsTestParam{copyableFootprintsTests::testParams[7],
                                       copyableFootprintsTests::results[7]},
        GetCopyableFootprintsTestParam{copyableFootprintsTests::testParams[8],
                                       copyableFootprintsTests::results[8]},
        GetCopyableFootprintsTestParam{copyableFootprintsTests::testParams[9],
                                       copyableFootprintsTests::results[9]}));

////////////////////////////////////////////////////////////////////////////////
