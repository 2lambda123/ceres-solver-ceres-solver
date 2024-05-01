// Ceres Solver - A fast non-linear least squares minimizer
// Copyright 2024 Google Inc. All rights reserved.
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
// Author: markshachkov@gmail.com (Mark Shachkov)

#include "ceres/cuda_sparse_cholesky.h"

#ifdef CERES_USE_CUDSS

#include <string>

#include "Eigen/Core"
#include "ceres/compressed_row_sparse_matrix.h"
#include "ceres/cuda_buffer.h"
#include "ceres/linear_solver.h"
#include "cuDSS.h"

namespace ceres::internal {

class CERES_NO_EXPORT CuDSSMatrixBase {
 public:
  ~CuDSSMatrixBase() { CHECK_EQ(Free(), CUDSS_STATUS_SUCCESS); }

  cudssStatus_t Free() {
    if (matrix_) {
      return cudssMatrixDestroy(matrix_);
    }

    return CUDSS_STATUS_SUCCESS;
  }

  operator cudssMatrix_t() { return matrix_; }

 protected:
  cudssMatrix_t matrix_{nullptr};
};

class CERES_NO_EXPORT CuDSSMatrixCSR : public CuDSSMatrixBase {
 public:
  cudssStatus_t Reset(int64_t num_rows,
                      int64_t num_cols,
                      int64_t num_nonzeros,
                      void* rows_start,
                      void* rows_end,
                      void* cols,
                      void* values,
                      cudaDataType_t index_type,
                      cudaDataType_t value_type,
                      cudssMatrixType_t matrix_type,
                      cudssMatrixViewType_t matrix_storage_type,
                      cudssIndexBase_t index_base) {
    if (auto status = Free(); status != CUDSS_STATUS_SUCCESS) {
      return status;
    }

    return cudssMatrixCreateCsr(&matrix_,
                                num_rows,
                                num_cols,
                                num_nonzeros,
                                rows_start,
                                rows_end,
                                cols,
                                values,
                                index_type,
                                value_type,
                                matrix_type,
                                matrix_storage_type,
                                index_base);
  }
};

class CERES_NO_EXPORT CuDSSMatrixDense : public CuDSSMatrixBase {
 public:
  cudssStatus_t Reset(int64_t num_rows,
                      int64_t num_cols,
                      int64_t ld,
                      void* values,
                      cudaDataType_t value_type,
                      cudssLayout_t layout) {
    if (auto status = Free(); status != CUDSS_STATUS_SUCCESS) {
      return status;
    }

    return cudssMatrixCreateDn(
        &matrix_, num_rows, num_cols, ld, values, value_type, layout);
  }
};

template <typename Scalar>
class CERES_NO_EXPORT CudaSparseCholeskyImpl final : public SparseCholesky {
 public:
  static_assert(std::is_same_v<Scalar, float> || std::is_same_v<Scalar, double>,
                "Scalar type is unsuported by cuDSS");
  static constexpr cudaDataType_t kCuDSSScalar =
      std::is_same_v<Scalar, float> ? CUDA_R_32F : CUDA_R_64F;

  CudaSparseCholeskyImpl(ContextImpl* context)
      : context_(context),
        lhs_cols_d_(context_),
        lhs_rows_d_(context_),
        lhs_values_d_(context_),
        rhs_d_(context_),
        x_d_(context_) {
    CHECK_EQ(cudssConfigCreate(&solverConfig_), CUDSS_STATUS_SUCCESS);
    CHECK_EQ(cudssDataCreate(context_->cudss_handle_, &solverData_),
             CUDSS_STATUS_SUCCESS);
  }

  ~CudaSparseCholeskyImpl() {
    cudssDataDestroy(context_->cudss_handle_, solverData_);
    cudssConfigDestroy(solverConfig_);
  }

  CompressedRowSparseMatrix::StorageType StorageType() const {
    return CompressedRowSparseMatrix::StorageType::LOWER_TRIANGULAR;
  }

  LinearSolverTerminationType Factorize(CompressedRowSparseMatrix* lhs,
                                        std::string* message) {
    CHECK_EQ(lhs->num_rows(), lhs->num_cols());

    if (factorize_result_ == LinearSolverTerminationType::FATAL_ERROR) {
      if (!Analyze(lhs, message)) {
        return factorize_result_ = LinearSolverTerminationType::FATAL_ERROR;
      }
    }

    ConvertOrCopy(lhs->values(), lhs_values_h_.data(), lhs_values_h_.size());
    lhs_values_d_.CopyFromCpu(lhs_values_h_.data(), lhs_values_h_.size());
    if (auto status = cudssExecute(context_->cudss_handle_,
                                   CUDSS_PHASE_FACTORIZATION,
                                   solverConfig_,
                                   solverData_,
                                   cudss_lhs_,
                                   cudss_x_,
                                   cudss_rhs_);
        status != CUDSS_STATUS_SUCCESS) {
      *message = StringPrintf(
          "cudssExecute with CUDSS_PHASE_FACTORIZATION failed, cudssStatus_t = "
          "%d",
          status);
      return factorize_result_ = LinearSolverTerminationType::FATAL_ERROR;
    }

    return factorize_result_ = LinearSolverTerminationType::SUCCESS;
  }

  LinearSolverTerminationType Solve(const double* rhs,
                                    double* solution,
                                    std::string* message) {
    if (factorize_result_ != LinearSolverTerminationType::SUCCESS) {
      *message = "Factorize did not complete successfully previously.";
      return factorize_result_;
    }

    ConvertOrCopy(rhs, rhs_h_.data(), rhs_h_.size());
    rhs_d_.CopyFromCpu(rhs_h_.data(), rhs_h_.size());

    if (auto status = cudssExecute(context_->cudss_handle_,
                                   CUDSS_PHASE_SOLVE,
                                   solverConfig_,
                                   solverData_,
                                   cudss_lhs_,
                                   cudss_x_,
                                   cudss_rhs_);
        status != CUDSS_STATUS_SUCCESS) {
      *message = StringPrintf(
          "cudssExecute with CUDSS_PHASE_SOLVE failed, cudssStatus_t = %d",
          status);
      return LinearSolverTerminationType::FATAL_ERROR;
    }
    x_d_.CopyToCpu(x_h_.data(), x_h_.size());
    ConvertOrCopy(x_h_.data(), solution, x_h_.size());

    return LinearSolverTerminationType::SUCCESS;
  }

 private:
  bool Analyze(const CompressedRowSparseMatrix* lhs, std::string* message) {
    if (!ResizeBuffers(lhs, message)) {
      return false;
    }

    std::copy_n(lhs->rows(), lhs_rows_h_.size(), lhs_rows_h_.data());
    lhs_rows_d_.CopyFromCpu(lhs_rows_h_.data(), lhs_rows_h_.size());
    std::copy_n(lhs->cols(), lhs_cols_h_.size(), lhs_cols_h_.data());
    lhs_cols_d_.CopyFromCpu(lhs_cols_h_.data(), lhs_cols_h_.size());

    if (auto status = cudssExecute(context_->cudss_handle_,
                                   CUDSS_PHASE_ANALYSIS,
                                   solverConfig_,
                                   solverData_,
                                   cudss_lhs_,
                                   cudss_x_,
                                   cudss_rhs_);
        status != CUDSS_STATUS_SUCCESS) {
      *message = StringPrintf(
          "cudssExecute with CUDSS_PHASE_ANALYSIS failed, cudssStatus_t = %d",
          status);
      return false;
    }

    return true;
  }

  bool ResizeBuffers(const CompressedRowSparseMatrix* lhs,
                     std::string* message) {
    const auto num_rows = lhs->num_rows();
    const auto num_nonzeros = lhs->num_nonzeros();

    lhs_rows_h_.Reserve(num_rows + 1);
    lhs_cols_h_.Reserve(num_nonzeros);
    lhs_values_h_.Reserve(num_nonzeros);
    rhs_h_.Reserve(num_rows);
    x_h_.Reserve(num_rows);

    lhs_rows_d_.Reserve(num_rows + 1);
    lhs_cols_d_.Reserve(num_nonzeros);
    lhs_values_d_.Reserve(num_nonzeros);
    rhs_d_.Reserve(num_rows);
    x_d_.Reserve(num_rows);

    static constexpr auto kFailedToCreateCuDSSMatrix =
        "cudssMatrixCreate call failed, error code: %d";
    if (auto status = cudss_lhs_.Reset(num_rows,
                                       num_rows,
                                       num_nonzeros,
                                       lhs_rows_d_.data(),
                                       nullptr,
                                       lhs_cols_d_.data(),
                                       lhs_values_d_.data(),
                                       CUDA_R_32I,
                                       kCuDSSScalar,
                                       CUDSS_MTYPE_SPD,
                                       CUDSS_MVIEW_LOWER,
                                       CUDSS_BASE_ZERO);
        status != CUDSS_STATUS_SUCCESS) {
      *message = StringPrintf(kFailedToCreateCuDSSMatrix, status);
      return false;
    }

    if (auto status = cudss_rhs_.Reset(num_rows,
                                       1,
                                       num_rows,
                                       rhs_d_.data(),
                                       kCuDSSScalar,
                                       CUDSS_LAYOUT_COL_MAJOR);
        status != CUDSS_STATUS_SUCCESS) {
      *message = StringPrintf(kFailedToCreateCuDSSMatrix, status);
      return false;
    }

    if (auto status = cudss_x_.Reset(num_rows,
                                     1,
                                     num_rows,
                                     x_d_.data(),
                                     kCuDSSScalar,
                                     CUDSS_LAYOUT_COL_MAJOR);
        status != CUDSS_STATUS_SUCCESS) {
      *message = StringPrintf(kFailedToCreateCuDSSMatrix, status);
      return false;
    }

    return true;
  }

  template <typename ScalarSource, typename ScalarDestination>
  void ConvertOrCopy(const ScalarSource* source,
                     ScalarDestination* destination,
                     size_t count) {
    if constexpr (std::is_same_v<ScalarSource, ScalarDestination>) {
      std::copy_n(source, count, destination);
    } else {
      Eigen::Map<Eigen::Matrix<ScalarDestination, Eigen::Dynamic, 1>>(
          destination, count) =
          Eigen::Map<const Eigen::Matrix<ScalarSource, Eigen::Dynamic, 1>>(
              source, count)
              .template cast<ScalarDestination>();
    }
  }

  ContextImpl* context_;
  cudssConfig_t solverConfig_{nullptr};
  cudssData_t solverData_{nullptr};
  CuDSSMatrixCSR cudss_lhs_;
  CuDSSMatrixDense cudss_rhs_;
  CuDSSMatrixDense cudss_x_;

  CudaPinnedHostBuffer<int> lhs_rows_h_;
  CudaPinnedHostBuffer<int> lhs_cols_h_;
  CudaPinnedHostBuffer<Scalar> lhs_values_h_;
  CudaPinnedHostBuffer<Scalar> rhs_h_;
  CudaPinnedHostBuffer<Scalar> x_h_;
  CudaBuffer<int> lhs_rows_d_;
  CudaBuffer<int> lhs_cols_d_;
  CudaBuffer<Scalar> lhs_values_d_;
  CudaBuffer<Scalar> rhs_d_;
  CudaBuffer<Scalar> x_d_;

  LinearSolverTerminationType factorize_result_ =
      LinearSolverTerminationType::FATAL_ERROR;
};

template <typename Scalar>
std::unique_ptr<SparseCholesky> CudaSparseCholesky<Scalar>::Create(
    ContextImpl* context, const OrderingType ordering_type) {
  if (ordering_type != OrderingType::AMD) {
    LOG(FATAL)
        << "Congratulations you have found a bug in Ceres Solver. Please "
           "report it to the Ceres Solver developers.";
    return nullptr;
  }

  if (context == nullptr || !context->IsCudaInitialized()) {
    return nullptr;
  }

  return std::make_unique<CudaSparseCholeskyImpl<Scalar>>(context);
}

template class CudaSparseCholesky<float>;
template class CudaSparseCholesky<double>;

}  // namespace ceres::internal

#endif  // CERES_USE_CUDSS
