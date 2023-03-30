/*
Copyright 2022 NVIDIA CORPORATION

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <gtest/gtest.h>

#include "nvblox/datasets/3dmatch.h"

using namespace nvblox;

constexpr float kFloatEpsilon = 1e-6;

TEST(HumanMapperTest, MaskOnAndOff) {
  // Load some 3DMatch data
  constexpr int kSeqID = 1;
  constexpr bool kMultithreadedLoading = false;
  datasets::threedmatch::DataLoader data_loader("./data/3dmatch", kSeqID,
                                                kMultithreadedLoading);

  DepthImage depth_frame;
  ColorImage color_frame;
  Transform T_L_C;
  Camera camera;
  Transform T_CM_CD = Transform::Identity();  // depth to mask camera transform
  data_loader.loadNext(&depth_frame, &T_L_C, &camera, &color_frame);

  // Two mappers - one with mask, one without
  constexpr float voxel_size_m = 0.05f;
  RgbdMapper mapper(voxel_size_m, MemoryType::kUnified);
  HumanMapper human_mapper(voxel_size_m, MemoryType::kUnified);
  // Add a human mapper using T_CM_CD = identity
  // to mimic the standard human depth mapper
  HumanMapper human_mapper_transform(voxel_size_m, MemoryType::kUnified);

  // Make a mask where everything is masked out
  MonoImage mask_one(depth_frame.rows(), depth_frame.cols(),
                     MemoryType::kUnified);
  for (int row_idx = 0; row_idx < mask_one.rows(); row_idx++) {
    for (int col_idx = 0; col_idx < mask_one.cols(); col_idx++) {
      mask_one(row_idx, col_idx) = 1;
    }
  }
  MonoImage mask_zero(mask_one);
  mask_zero.setZero();

  // Depth masked out - expect nothing integrated
  mapper.integrateDepth(depth_frame, T_L_C, camera);
  human_mapper.integrateDepth(depth_frame, mask_one, T_L_C, camera);
  human_mapper_transform.integrateDepth(depth_frame, mask_one, T_L_C, T_CM_CD,
                                        camera, camera);
  EXPECT_GT(mapper.tsdf_layer().numAllocatedBlocks(), 0);
  EXPECT_EQ(human_mapper.tsdf_layer().numAllocatedBlocks(), 0);

  // Depth NOT masked out - expect same results as normal mapper
  human_mapper.integrateDepth(depth_frame, mask_zero, T_L_C, camera);
  human_mapper_transform.integrateDepth(depth_frame, mask_zero, T_L_C, T_CM_CD,
                                        camera, camera);
  EXPECT_EQ(mapper.tsdf_layer().numAllocatedBlocks(),
            human_mapper.tsdf_layer().numAllocatedBlocks());
  EXPECT_EQ(mapper.tsdf_layer().numAllocatedBlocks(),
            human_mapper_transform.tsdf_layer().numAllocatedBlocks());

  // Color masked out - expect blocks allocated but zero weight
  mapper.integrateColor(color_frame, T_L_C, camera);
  human_mapper.integrateColor(color_frame, mask_one, T_L_C, camera);
  int num_non_zero_weight_voxels = 0;
  callFunctionOnAllVoxels<ColorVoxel>(
      human_mapper.color_layer(),
      [&](const Index3D& block_index, const Index3D& voxel_index,
          const ColorVoxel* voxel) -> void {
        EXPECT_NEAR(voxel->weight, 0.0f, kFloatEpsilon);
        if (voxel->weight) {
          ++num_non_zero_weight_voxels;
        }
      });
  EXPECT_EQ(num_non_zero_weight_voxels, 0);

  // Color NOT masked out - expect same results as normal mapper
  human_mapper.integrateColor(color_frame, mask_zero, T_L_C, camera);
  EXPECT_EQ(human_mapper.color_layer().numAllocatedBlocks(),
            mapper.color_layer().numAllocatedBlocks());
  for (const Index3D& block_idx : mapper.color_layer().getAllBlockIndices()) {
    const auto block = mapper.color_layer().getBlockAtIndex(block_idx);
    const auto human_block =
        human_mapper.color_layer().getBlockAtIndex(block_idx);
    CHECK(block);
    CHECK(human_block);
    for (int x_idx = 0; x_idx < ColorBlock::kVoxelsPerSide; x_idx++) {
      for (int y_idx = 0; y_idx < ColorBlock::kVoxelsPerSide; y_idx++) {
        for (int z_idx = 0; z_idx < ColorBlock::kVoxelsPerSide; z_idx++) {
          ColorVoxel voxel = block->voxels[x_idx][y_idx][z_idx];
          ColorVoxel human_voxel = human_block->voxels[x_idx][y_idx][z_idx];
          EXPECT_TRUE(voxel.color == human_voxel.color);
          EXPECT_NEAR(voxel.weight, human_voxel.weight, kFloatEpsilon);
          if (human_voxel.weight > 0.0f) {
            ++num_non_zero_weight_voxels;
          }
        }
      }
    }
  }
  EXPECT_GT(num_non_zero_weight_voxels, 0);
  std::cout << "num_non_zero_weight_voxels: " << num_non_zero_weight_voxels
            << std::endl;
}

int main(int argc, char** argv) {
  FLAGS_alsologtostderr = true;
  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
