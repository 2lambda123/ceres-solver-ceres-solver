// Ceres Solver - A fast non-linear least squares minimizer
// Copyright 2017 Google Inc. All rights reserved.
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
// Author: mierle@gmail.com (Keir Mierle)
//
// WARNING WARNING WARNING
// WARNING WARNING WARNING  Tiny solver is experimental and will change.
// WARNING WARNING WARNING
//
// A tiny least squares solver using Levenberg-Marquardt, intended for solving
// small dense problems with low latency and low overhead. The implementation
// takes care to do all allocation up front, so that no memory is allocated
// during solving. This is especially useful when solving many similar problems;
// for example, inverse pixel distortion for every pixel on a grid.
//
// Note: This code has no depedencies beyond Eigen, including on other parts of
// Ceres, so it is possible to take this file alone and put it in another
// project without the rest of Ceres.
//
// Algorithm based off of:
//
// [1] K. Madsen, H. Nielsen, O. Tingleoff.
//     Methods for Non-linear Least Squares Problems.
//     http://www2.imm.dtu.dk/pubdb/views/edoc_download.php/3215/pdf/imm3215.pdf

#ifndef CERES_PUBLIC_TINY_SOLVER_H_
#define CERES_PUBLIC_TINY_SOLVER_H_

#include <cassert>
#include <cmath>

#include "Eigen/Core"
#include "Eigen/LU"

#include "ceres/internal/autodiff.h"

namespace ceres {

// To use tiny solver, create a class or struct that allows computing the cost
// function (described below). This is similar to a ceres::CostFunction, but is
// different to enable statically allocating all memory for the solve
// (specifically, enum sizes). Key parts are the Scalar typedef, the enums to
// describe problem sizes (needed to remove all heap allocations), and the
// operator() overload to evaluate the cost and (optionally) jacobians.
//
//   struct TinySolverCostFunctionTraits {
//     typedef double Scalar;
//     enum {
//       NUM_RESIDUALS = <int> OR Eigen::Dynamic,
//       NUM_PARAMETERS = <int> OR Eigen::Dynamic,
//     };
//     bool operator()(const double* parameters,
//                     double* residuals,
//                     double* jacobian) const;
//
//     int NumResiduals();  -- Needed if NUM_RESIDUALS == Eigen::Dynamic.
//     int NumParameters(); -- Needed if NUM_PARAMETERS == Eigen::Dynamic.
//   }
//
// For operator(), the size of the objects is:
//
//   double* parameters -- NUM_PARAMETERS or NumParameters()
//   double* residuals  -- NUM_RESIDUALS or NumResiduals()
//   double* jacobian   -- NUM_RESIDUALS * NUM_PARAMETERS in column-major format
//                         (Eigen's default); or NULL if no jacobian requested.
//
// An example (fully statically sized):
//
//   struct MyCostFunctionExample {
//     typedef double Scalar;
//     enum {
//       NUM_RESIDUALS = 2,
//       NUM_PARAMETERS = 3,
//     };
//     bool operator()(const double* parameters,
//                     double* residuals,
//                     double* jacobian) const {
//       residuals[0] = x + 2*y + 4*z;
//       residuals[1] = y * z;
//       if (jacobian) {
//         jacobian[0 * 2 + 0] = 1;   // First column (x).
//         jacobian[0 * 2 + 1] = 0;
//
//         jacobian[1 * 2 + 0] = 2;   // Second column (y).
//         jacobian[1 * 2 + 1] = z;
//
//         jacobian[2 * 2 + 0] = 4;   // Third column (z).
//         jacobian[2 * 2 + 1] = y;
//       }
//       return true;
//     }
//   };
//
// The solver supports either statically or dynamically sized cost
// functions. If the number of residuals is dynamic then the Function
// must define:
//
//   int NumResiduals() const;
//
// If the number of parameters is dynamic then the Function must
// define:
//
//   int NumParameters() const;
//
template<typename Function,
         typename LinearSolver = Eigen::PartialPivLU<
           Eigen::Matrix<typename Function::Scalar,
                         Function::NUM_PARAMETERS,
                         Function::NUM_PARAMETERS> > >
class TinySolver {
 public:
  enum {
    NUM_RESIDUALS = Function::NUM_RESIDUALS,
    NUM_PARAMETERS = Function::NUM_PARAMETERS
  };
  typedef typename Function::Scalar Scalar;
  typedef typename Eigen::Matrix<Scalar, NUM_PARAMETERS, 1> Parameters;

  // TODO(keir): Some of these knobs can be derived from each other and
  // removed, instead of requiring the user to set them.
  enum Status {
    RUNNING,
    // Resulting solution may be OK to use.
    GRADIENT_TOO_SMALL,            // eps > max(J'*f(x))
    RELATIVE_STEP_SIZE_TOO_SMALL,  // eps > ||dx|| / ||x||
    ERROR_TOO_SMALL,               // eps > ||f(x)||
    HIT_MAX_ITERATIONS,

    // Numerical issues
    FAILED_TO_EVALUATE_COST_FUNCTION,
    FAILED_TO_SOLVER_LINEAR_SYSTEM,
  };

  struct SolverParameters {
    SolverParameters()
       : gradient_threshold(1e-16),
         relative_step_threshold(1e-16),
         error_threshold(1e-16),
         initial_scale_factor(1e-3),
         max_iterations(100) {}
    Scalar gradient_threshold;       // eps > max(J'*f(x))
    Scalar relative_step_threshold;  // eps > ||dx|| / ||x||
    Scalar error_threshold;          // eps > ||f(x)||
    Scalar initial_scale_factor;     // Initial u for solving normal equations.
    int    max_iterations;           // Maximum number of solver iterations.
  };

  struct Results {
    Scalar error_magnitude;     // ||f(x)||
    Scalar gradient_magnitude;  // ||J'f(x)||
    int    num_failed_linear_solves;
    int    iterations;
    Status status;
  };

  Status Update(const Function& function, const Parameters &x) {
    // TODO(keir): Handle false return from the cost function.
    function(&x(0), &error_(0), &jacobian_(0, 0));
    error_ = -error_;

    // This explicitly computes the normal equations, which is numerically
    // unstable. Nevertheless, it is often good enough and is fast.
    jtj_ = jacobian_.transpose() * jacobian_;
    g_ = jacobian_.transpose() * error_;
    if (g_.array().abs().maxCoeff() < params.gradient_threshold) {
      return GRADIENT_TOO_SMALL;
    } else if (error_.norm() < params.error_threshold) {
      return ERROR_TOO_SMALL;
    }
    return RUNNING;
  }

  Results Solve(const Function& function, Parameters* x_and_min) {
    Initialize<NUM_RESIDUALS, NUM_PARAMETERS>(function);

    assert(x_and_min);
    Parameters& x = *x_and_min;
    results.status = Update(function, x);

    Scalar u = Scalar(params.initial_scale_factor * jtj_.diagonal().maxCoeff());
    Scalar v = 2;

    int i;
    for (i = 0; results.status == RUNNING && i < params.max_iterations; ++i) {
      jtj_augmented_ = jtj_;
      jtj_augmented_.diagonal().array() += u;

      linear_solver_.compute(jtj_augmented_);
      dx_ = linear_solver_.solve(g_);
      bool solved = (jtj_augmented_ * dx_).isApprox(g_);
      if (solved) {
        if (dx_.norm() < params.relative_step_threshold * x.norm()) {
          results.status = RELATIVE_STEP_SIZE_TOO_SMALL;
          break;
        }
        x_new_ = x + dx_;
        // Rho is the ratio of the actual reduction in error to the reduction
        // in error that would be obtained if the problem was linear. See [1]
        // for details.
        // TODO(keir): Add proper handling of errors from user eval of cost functions.
        function(&x_new_[0], &f_x_new_[0], NULL);
        Scalar rho((error_.squaredNorm() - f_x_new_.squaredNorm())
                   / dx_.dot(u * dx_ + g_));
        if (rho > 0) {
          // Accept the Gauss-Newton step because the linear model fits well.
          x = x_new_;
          results.status = Update(function, x);
          Scalar tmp = Scalar(2 * rho - 1);
          u = u * std::max(1 / 3., 1 - tmp * tmp * tmp);
          v = 2;
          continue;
        }
      } else {
        results.num_failed_linear_solves++;
      }
      // Reject the update because either the normal equations failed to solve
      // or the local linear model was not good (rho < 0). Instead, increase u
      // to move closer to gradient descent.
      u *= v;
      v *= 2;
    }
    if (results.status == RUNNING) {
      results.status = HIT_MAX_ITERATIONS;
    }
    results.error_magnitude = error_.norm();
    results.gradient_magnitude = g_.norm();
    results.iterations = i;
    return results;
  }

  SolverParameters params;
  Results results;

 private:
  // Preallocate everything, including temporary storage needed for solving the
  // linear system. This allows reusing the intermediate storage across solves.
  LinearSolver linear_solver_;
  Parameters dx_, x_new_, g_;
  Eigen::Matrix<Scalar, NUM_RESIDUALS, 1> error_, f_x_new_;
  Eigen::Matrix<Scalar, NUM_RESIDUALS, NUM_PARAMETERS> jacobian_;
  Eigen::Matrix<Scalar, NUM_PARAMETERS, NUM_PARAMETERS> jtj_, jtj_augmented_;

  // The following definitions are needed for template metaprogramming.
  template<bool Condition, typename T> struct enable_if;

  template<typename T> struct enable_if<true, T> {
    typedef T type;
  };

  // The number of parameters and residuals are dynamically sized.
  template <int R, int P>
  typename enable_if<(R == Eigen::Dynamic && P == Eigen::Dynamic), void>::type
  Initialize(const Function& function) {
    Initialize(function.NumResiduals(), function.NumParameters());
  }

  // The number of parameters is dynamically sized and the number of
  // residuals is statically sized.
  template <int R, int P>
  typename enable_if<(R == Eigen::Dynamic && P != Eigen::Dynamic), void>::type
  Initialize(const Function& function) {
    Initialize(function.NumResiduals(), P);
  }

  // The number of parameters is statically sized and the number of
  // residuals is dynamically sized.
  template <int R, int P>
  typename enable_if<(R != Eigen::Dynamic && P == Eigen::Dynamic), void>::type
  Initialize(const Function& function) {
    Initialize(R, function.NumParameters());
  }

  // The number of parameters and residuals are statically sized.
  template <int R, int P>
  typename enable_if<(R != Eigen::Dynamic && P != Eigen::Dynamic), void>::type
  Initialize(const Function& /* function */) { }

  void Initialize(int num_residuals, int num_parameters) {
    error_.resize(num_residuals);
    f_x_new_.resize(num_residuals);
    jacobian_.resize(num_residuals, num_parameters);
    jtj_.resize(num_parameters, num_parameters);
    jtj_augmented_.resize(num_parameters, num_parameters);
  }
};

// An adapter around Ceres autodiff-style CostFunctors to enable easier use of
// TinySolver. See the example below showing how to use it:
//
//   // Same as a Ceres autodiff functor, but taking only 1 parameter.
//   struct MyFunctor {
//     template<typename T>
//     bool operator()(const T* const parameters, T* residuals) const {
//       const T& x = parameters[0];
//       const T& y = parameters[1];
//       const T& z = parameters[2];
//       residuals[0] = x + 2.*y + 4.*z;
//       residuals[1] = y * z;
//       return true;
//     }
//   };
//
//   typedef TinySolverFunctionAutoDiffAdapter<MyFunctor, 2, 3>
//       WrappedFunctor;
//
//   Vec3 x = ...;
//
//   MyFunctor my_functor;
//   WrappedFunction f(my_functor);
//   TinySolver<WrappedFunction> solver;
//   solver.Solve(f, &x);
//
// Key point of note is that a fair amount of stack space is needed, and there
// is some overhead to using this approach.
template<typename CostFunctor,
         int kNumResiduals,
         int kNumParameters,
         typename T = double>
class TinySolverFunctionAutoDiffAdapter {
 public:
   TinySolverFunctionAutoDiffAdapter(const CostFunctor& cost_functor)
     : cost_functor_(cost_functor) {}

  typedef T Scalar;
  enum {
    NUM_PARAMETERS = kNumParameters,
    NUM_RESIDUALS = kNumResiduals,
  };
  // Note: This may use quite some stack space, so be careful.
  bool operator()(const T* parameters,
                  T* residuals,
                  T* jacobian) const {
    if (!jacobian) {
      // No jacobian requested, so just directly call the cost function with
      // doubles, skipping jets and derivatives.
      return cost_functor_(parameters, residuals);
    }
    // TODO(keir): It may be worth using FixedArray here to avoid risks with
    // stack allocations. However, the entire purpose of TinySolver is to avoid
    // heap allocations. So perhaps there should be an alternate interface that
    // allows allocating the jet storage once and reusing it across calls. Such
    // an interface will require some thinking.

    // Initialize the input jets with passed parameters.
    Jet<T, kNumParameters> jet_parameters[kNumParameters];
    for (int i = 0; i < kNumParameters; ++i) {
      jet_parameters[i].a = parameters[i];  // Scalar part.
      jet_parameters[i].v.setZero();  // Derivative part.
      jet_parameters[i].v[i] = T(1.0);
    }

    // Initialize the output jets such that we can detect user errors.
    Jet<T, kNumParameters> jet_residuals[kNumResiduals];
    for (int i = 0; i < kNumResiduals; ++i) {
      jet_residuals[i].a = kImpossibleValue;
      jet_residuals[i].v.setConstant(kImpossibleValue);
    }

    // Execute the cost function, but with jets to find the derivative.
    if (!cost_functor_(jet_parameters, jet_residuals)) {
      return false;
    }

    // Copy the jacobian out of the derivative part of the residual jets.
    Eigen::Map<Eigen::Matrix<T,
                             kNumResiduals,
                             kNumParameters> > jacobian_matrix(jacobian);
    for (int r = 0; r < kNumResiduals; ++r) {
      residuals[r] = jet_residuals[r].a;
      // Note that while this looks like a fast vectorized write, in practice it
      // unfortunately thrashes the cache since the writes to the column-major
      // jacobian are strided (e.g. rows are non-contiguous).
      jacobian_matrix.row(r) = jet_residuals[r].v;
    }
    return true;
  }

 private:
  const CostFunctor& cost_functor_;
};

}  // namespace ceres

#endif  // CERES_PUBLIC_TINY_SOLVER_H_
