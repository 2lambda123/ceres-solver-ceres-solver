// Ceres Solver - A fast non-linear least squares minimizer
// Copyright 2022 Google Inc. All rights reserved.
// http://ceres-solver.org/
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// * Neither the name of Google Inc. nor the names of its contributors may be
//   used to endorse or promote products derived from this software without
//   specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Author: sameeragarwal@google.com (Sameer Agarwal)

#include "ceres/partitioned_matrix_view.h"

#include <memory>
#include <random>
#include <vector>

#include "ceres/block_structure.h"
#include "ceres/casts.h"
#include "ceres/internal/eigen.h"
#include "ceres/linear_least_squares_problems.h"
#include "ceres/sparse_matrix.h"
#include "glog/logging.h"
#include "gtest/gtest.h"

namespace ceres {
namespace internal {

const double kEpsilon = 1e-14;

class PartitionedMatrixViewTest : public ::testing::Test {
 protected:
  void SetUp() final {
    std::unique_ptr<LinearLeastSquaresProblem> problem =
        CreateLinearLeastSquaresProblemFromId(2);
    CHECK(problem != nullptr);
    A_ = std::move(problem->A);

    num_cols_ = A_->num_cols();
    num_rows_ = A_->num_rows();
    num_eliminate_blocks_ = problem->num_eliminate_blocks;
    LinearSolver::Options options;
    options.elimination_groups.push_back(num_eliminate_blocks_);
    pmv_ = PartitionedMatrixViewBase::Create(
        options, *down_cast<BlockSparseMatrix*>(A_.get()));
  }

  double RandDouble() { return distribution_(prng_); }

  int num_rows_;
  int num_cols_;
  int num_eliminate_blocks_;
  std::unique_ptr<SparseMatrix> A_;
  std::unique_ptr<PartitionedMatrixViewBase> pmv_;
  std::mt19937 prng_;
  std::uniform_real_distribution<double> distribution_ =
      std::uniform_real_distribution<double>(0.0, 1.0);
};

TEST_F(PartitionedMatrixViewTest, DimensionsTest) {
  EXPECT_EQ(pmv_->num_col_blocks_e(), num_eliminate_blocks_);
  EXPECT_EQ(pmv_->num_col_blocks_f(), num_cols_ - num_eliminate_blocks_);
  EXPECT_EQ(pmv_->num_cols_e(), num_eliminate_blocks_);
  EXPECT_EQ(pmv_->num_cols_f(), num_cols_ - num_eliminate_blocks_);
  EXPECT_EQ(pmv_->num_cols(), A_->num_cols());
  EXPECT_EQ(pmv_->num_rows(), A_->num_rows());
}

TEST_F(PartitionedMatrixViewTest, RightMultiplyAndAccumulateE) {
  Vector x1(pmv_->num_cols_e());
  Vector x2(pmv_->num_cols());
  x2.setZero();

  for (int i = 0; i < pmv_->num_cols_e(); ++i) {
    x1(i) = x2(i) = RandDouble();
  }

  Vector y1 = Vector::Zero(pmv_->num_rows());
  pmv_->RightMultiplyAndAccumulateE(x1.data(), y1.data());

  Vector y2 = Vector::Zero(pmv_->num_rows());
  A_->RightMultiplyAndAccumulate(x2.data(), y2.data());

  for (int i = 0; i < pmv_->num_rows(); ++i) {
    EXPECT_NEAR(y1(i), y2(i), kEpsilon);
  }
}

TEST_F(PartitionedMatrixViewTest, RightMultiplyAndAccumulateF) {
  Vector x1(pmv_->num_cols_f());
  Vector x2 = Vector::Zero(pmv_->num_cols());

  for (int i = 0; i < pmv_->num_cols_f(); ++i) {
    x1(i) = RandDouble();
    x2(i + pmv_->num_cols_e()) = x1(i);
  }

  Vector y1 = Vector::Zero(pmv_->num_rows());
  pmv_->RightMultiplyAndAccumulateF(x1.data(), y1.data());

  Vector y2 = Vector::Zero(pmv_->num_rows());
  A_->RightMultiplyAndAccumulate(x2.data(), y2.data());

  for (int i = 0; i < pmv_->num_rows(); ++i) {
    EXPECT_NEAR(y1(i), y2(i), kEpsilon);
  }
}

TEST_F(PartitionedMatrixViewTest, LeftMultiplyAndAccumulate) {
  Vector x = Vector::Zero(pmv_->num_rows());
  for (int i = 0; i < pmv_->num_rows(); ++i) {
    x(i) = RandDouble();
  }

  Vector y = Vector::Zero(pmv_->num_cols());
  Vector y1 = Vector::Zero(pmv_->num_cols_e());
  Vector y2 = Vector::Zero(pmv_->num_cols_f());

  A_->LeftMultiplyAndAccumulate(x.data(), y.data());
  pmv_->LeftMultiplyAndAccumulateE(x.data(), y1.data());
  pmv_->LeftMultiplyAndAccumulateF(x.data(), y2.data());

  for (int i = 0; i < pmv_->num_cols(); ++i) {
    EXPECT_NEAR(y(i),
                (i < pmv_->num_cols_e()) ? y1(i) : y2(i - pmv_->num_cols_e()),
                kEpsilon);
  }
}

TEST_F(PartitionedMatrixViewTest, BlockDiagonalEtE) {
  std::unique_ptr<BlockSparseMatrix> block_diagonal_ee(
      pmv_->CreateBlockDiagonalEtE());
  const CompressedRowBlockStructure* bs = block_diagonal_ee->block_structure();

  EXPECT_EQ(block_diagonal_ee->num_rows(), 2);
  EXPECT_EQ(block_diagonal_ee->num_cols(), 2);
  EXPECT_EQ(bs->cols.size(), 2);
  EXPECT_EQ(bs->rows.size(), 2);

  EXPECT_NEAR(block_diagonal_ee->values()[0], 10.0, kEpsilon);
  EXPECT_NEAR(block_diagonal_ee->values()[1], 155.0, kEpsilon);
}

TEST_F(PartitionedMatrixViewTest, BlockDiagonalFtF) {
  std::unique_ptr<BlockSparseMatrix> block_diagonal_ff(
      pmv_->CreateBlockDiagonalFtF());
  const CompressedRowBlockStructure* bs = block_diagonal_ff->block_structure();

  EXPECT_EQ(block_diagonal_ff->num_rows(), 3);
  EXPECT_EQ(block_diagonal_ff->num_cols(), 3);
  EXPECT_EQ(bs->cols.size(), 3);
  EXPECT_EQ(bs->rows.size(), 3);
  EXPECT_NEAR(block_diagonal_ff->values()[0], 70.0, kEpsilon);
  EXPECT_NEAR(block_diagonal_ff->values()[1], 17.0, kEpsilon);
  EXPECT_NEAR(block_diagonal_ff->values()[2], 37.0, kEpsilon);
}

const int kMaxNumThreads = 8;
class PartitionedMatrixViewParallelTest : public ::testing::TestWithParam<int> {
 protected:
  void SetUp() final {
    std::unique_ptr<LinearLeastSquaresProblem> problem =
        CreateLinearLeastSquaresProblemFromId(2);
    CHECK(problem != nullptr);
    A_ = std::move(problem->A);

    num_cols_ = A_->num_cols();
    num_rows_ = A_->num_rows();
    num_eliminate_blocks_ = problem->num_eliminate_blocks;
    LinearSolver::Options options;
    options.elimination_groups.push_back(num_eliminate_blocks_);
    pmv_ = PartitionedMatrixViewBase::Create(
        options, *down_cast<BlockSparseMatrix*>(A_.get()));
    context_.EnsureMinimumThreads(kMaxNumThreads);
  }

  double RandDouble() { return distribution_(prng_); }

  ContextImpl context_;
  int num_rows_;
  int num_cols_;
  int num_eliminate_blocks_;
  std::unique_ptr<SparseMatrix> A_;
  std::unique_ptr<PartitionedMatrixViewBase> pmv_;
  std::mt19937 prng_;
  std::uniform_real_distribution<double> distribution_ =
      std::uniform_real_distribution<double>(0.0, 1.0);
};

TEST_P(PartitionedMatrixViewParallelTest, RightMultiplyAndAccumulateEParallel) {
  const int kNumThreads = GetParam();
  Vector x1(pmv_->num_cols_e());
  Vector x2(pmv_->num_cols());
  x2.setZero();

  for (int i = 0; i < pmv_->num_cols_e(); ++i) {
    x1(i) = x2(i) = RandDouble();
  }

  Vector y1 = Vector::Zero(pmv_->num_rows());
  pmv_->RightMultiplyAndAccumulateE(
      x1.data(), y1.data(), &context_, kNumThreads);

  Vector y2 = Vector::Zero(pmv_->num_rows());
  A_->RightMultiplyAndAccumulate(x2.data(), y2.data());

  for (int i = 0; i < pmv_->num_rows(); ++i) {
    EXPECT_NEAR(y1(i), y2(i), kEpsilon);
  }
}

TEST_P(PartitionedMatrixViewParallelTest, RightMultiplyAndAccumulateFParallel) {
  const int kNumThreads = GetParam();
  Vector x1(pmv_->num_cols_f());
  Vector x2(pmv_->num_cols());
  x2.setZero();

  for (int i = 0; i < pmv_->num_cols_f(); ++i) {
    x1(i) = x2(i + pmv_->num_cols_e()) = RandDouble();
  }

  Vector y1 = Vector::Zero(pmv_->num_rows());
  pmv_->RightMultiplyAndAccumulateF(
      x1.data(), y1.data(), &context_, kNumThreads);

  Vector y2 = Vector::Zero(pmv_->num_rows());
  A_->RightMultiplyAndAccumulate(x2.data(), y2.data());

  for (int i = 0; i < pmv_->num_rows(); ++i) {
    EXPECT_NEAR(y1(i), y2(i), kEpsilon);
  }
}

TEST_P(PartitionedMatrixViewParallelTest, LeftMultiplyAndAccumulateParallel) {
  const int kNumThreads = GetParam();
  Vector x = Vector::Zero(pmv_->num_rows());
  for (int i = 0; i < pmv_->num_rows(); ++i) {
    x(i) = RandDouble();
  }

  Vector y = Vector::Zero(pmv_->num_cols());
  Vector y1 = Vector::Zero(pmv_->num_cols_e());
  Vector y2 = Vector::Zero(pmv_->num_cols_f());

  A_->LeftMultiplyAndAccumulate(x.data(), y.data());
  pmv_->LeftMultiplyAndAccumulateE(x.data(), y1.data(), &context_, kNumThreads);
  pmv_->LeftMultiplyAndAccumulateF(x.data(), y2.data(), &context_, kNumThreads);

  for (int i = 0; i < pmv_->num_cols(); ++i) {
    EXPECT_NEAR(y(i),
                (i < pmv_->num_cols_e()) ? y1(i) : y2(i - pmv_->num_cols_e()),
                kEpsilon);
  }
}

INSTANTIATE_TEST_SUITE_P(ParallelProducts,
                         PartitionedMatrixViewParallelTest,
                         ::testing::Values(1, 2, 4, 8),
                         ::testing::PrintToStringParamName());

}  // namespace internal
}  // namespace ceres
