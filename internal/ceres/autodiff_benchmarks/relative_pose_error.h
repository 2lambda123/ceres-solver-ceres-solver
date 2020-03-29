// Ceres Solver - A fast non-linear least squares minimizer
// Copyright 2020 Google Inc. All rights reserved.
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
// Author: nikolaus@nikolaus-demmel.de (Nikolaus Demmel)
//
//
#ifndef CERES_INTERNAL_AUTODIFF_BENCHMARK_RELATIVE_POSE_ERROR_H_
#define CERES_INTERNAL_AUTODIFF_BENCHMARK_RELATIVE_POSE_ERROR_H_

#include <Eigen/Dense>

#include "ceres/codegen/codegen_cost_function.h"
#include "ceres/rotation.h"

namespace ceres {

// Relative pose error as one might use in SE(3) pose graph optimization.
// The measurement is a relative pose T_i_j, and the parameters are absolute
// poses T_w_i and T_w_j. For the residual we use the log of the the residual
// pose, in split representation SO(3) x R^3.
struct RelativePoseError : public ceres::CodegenCostFunction<6, 7, 7> {
  RelativePoseError(const Eigen::Quaternion<double>& q_i_j,
                    const Eigen::Vector3d& t_i_j)
      : meas_q_i_j_(q_i_j), meas_t_i_j_(t_i_j) {}

  template <typename T>
  bool operator()(const T* const pose_i_ptr,
                  const T* const pose_j_ptr,
                  T* residuals_ptr) const {
    Eigen::Map<const Eigen::Quaternion<T>> q_w_i(pose_i_ptr);
    Eigen::Map<const Eigen::Matrix<T, 3, 1>> t_w_i(pose_i_ptr + 4);
    Eigen::Map<const Eigen::Quaternion<T>> q_w_j(pose_j_ptr);
    Eigen::Map<const Eigen::Matrix<T, 3, 1>> t_w_j(pose_j_ptr + 4);
    Eigen::Map<Eigen::Matrix<T, 6, 1>> residuals(residuals_ptr);

    // estimate of relative pose from i to j
    const Eigen::Quaternion<T> est_q_j_i = q_w_j.conjugate() * q_w_i;
    const Eigen::Matrix<T, 3, 1> est_t_j_i =
        q_w_j.conjugate() * (t_w_i - t_w_j);

    // residual pose
    const Eigen::Quaternion<T> res_q = meas_q_i_j_.cast<T>() * est_q_j_i;
    const Eigen::Matrix<T, 3, 1> res_t =
        meas_q_i_j_.cast<T>() * est_t_j_i + meas_t_i_j_;

    // convert quaternion to ceres convention (eigen stores xyzw, ceres wxyz)
    Eigen::Matrix<T, 4, 1> res_q_ceres;
    res_q_ceres << res_q.w(), res_q.vec();

    // residual is log of pose; use split representation SO(3) x R^3
    QuaternionToAngleAxis(res_q_ceres.data(), residuals.data());
    residuals.template bottomRows<3>() = res_t;

    return true;
  }
#ifdef WITH_CODE_GENERATION
#include "benchmarks/relativeposeerror.h"
#else
  virtual bool Evaluate(double const* const* parameters,
                        double* residuals,
                        double** jacobians) const {
    return false;
  }
#endif

 private:
  // measurement relative pose from j to i
  Eigen::Quaternion<double> meas_q_i_j_;
  Eigen::Vector3d meas_t_i_j_;
};
}  // namespace ceres
#endif  // CERES_INTERNAL_AUTODIFF_BENCHMARK_RELATIVE_POSE_ERROR_H_
