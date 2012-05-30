// Ceres Solver - A fast non-linear least squares minimizer
// Copyright 2012 Google Inc. All rights reserved.
// http://code.google.com/p/ceres-solver/
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

#ifndef CERES_INTERNAL_TRUST_REGION_STRATEGY_H_
#define CERES_INTERNAL_TRUST_REGION_STRATEGY_H_

#include "ceres/linear_solver.h"

namespace ceres {
namespace internal {

// Interface for classes implementing various trust region strategies
// for nonlinear least squares problems.
//
// The object is expected to maintain and update a trust region
// radius, which it then uses to solve for the trust region step using
// the jacobian matrix and residual vector.
//
// Here the term trust region radius is used loosely, as the strategy
// is free to treat it as guidance and violate it as need be. e.g.,
// the LevenbergMarquardtStrategy uses the inverse of the trust region
// radius to scale the damping term, which controls the step size, but
// does not set a hard limit on its size.
class TrustRegionStrategy {
public:
  struct Options {
    Options()
        : trust_region_strategy_type(LEVENBERG_MARQUARDT),
          initial_radius(1e4),
          max_radius(1e32),
          lm_min_diagonal(1e-6),
          lm_max_diagonal(1e32) {
    }

    TrustRegionStrategyType trust_region_strategy_type;
    // Linear solver used for actually solving the trust region step.
    LinearSolver* linear_solver;
    double initial_radius;
    double max_radius;

    // Minimum and maximum values of the diagonal damping matrix used
    // by LevenbergMarquardtStrategy.
    double lm_min_diagonal;
    double lm_max_diagonal;
  };

  // Per solve options.
  struct PerSolveOptions {
    // Forcing sequence for inexact solves.
    double eta;
  };

  ~TrustRegionStrategy();

  // Use the current radius to solve for the trust region step.
  virtual LinearSolver::Summary ComputeStep(
      const PerSolveOptions& per_solve_options,
      SparseMatrix* jacobian,
      const double* residuals,
      double* step) = 0;

  // Inform the strategy that the current step has been accepted, and
  // that the ratio of the decrease in the non-linear objective to the
  // decrease in the trust region model is step_quality.
  virtual void StepAccepted(double step_quality) = 0;

  // Inform the strategy that the current step has been rejected, and
  // that the ratio of the decrease in the non-linear objective to the
  // decrease in the trust region model is step_quality.
  virtual void StepRejected(double step_quality) = 0;

  // Current trust region radius.
  virtual double Radius() const = 0;

  // Factory.
  static TrustRegionStrategy* Create(const Options& options);
};

}  // namespace internal
}  // namespace ceres

#endif  // CERES_INTERNAL_TRUST_REGION_STRATEGY_H_
