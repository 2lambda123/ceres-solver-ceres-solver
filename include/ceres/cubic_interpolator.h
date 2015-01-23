// Ceres Solver - A fast non-linear least squares minimizer
// Copyright 2014 Google Inc. All rights reserved.
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

#ifndef CERES_PUBLIC_CUBIC_INTERPOLATOR_H_
#define CERES_PUBLIC_CUBIC_INTERPOLATOR_H_

#include "glog/logging.h"
#include "ceres/internal/port.h"
#include "ceres/internal/scoped_ptr.h"
#include "ceres/internal/disable_warnings.h"

namespace ceres {

// This class takes as input an array of values that is assumed to be
// integer valued samples from a function f(x), evaluated at x = 0,
// ... , n - 1 and uses Catmull-Rom splines to produce a smooth
// approximation to it that can be used to evaluate the f(x) and f'(x)
// at any point in the interval [0, n-1].
//
// Besides this, the reason this class is included with Ceres is that
// the Evaluate method is overloaded so that the user can use it as
// part of their automatically differentiated CostFunction objects
// without worrying about the fact that they are working with a
// numerically interpolated object.
//
// For more details on cubic interpolation see
//
// http://en.wikipedia.org/wiki/Cubic_Hermite_spline
// http://www.paulinternet.nl/?page=bicubic
//
// TODO(sameeragarwal): Example usage.
class CERES_EXPORT CubicInterpolator1 {
 public:
  CubicInterpolator1(int num_values, const double* values);

  // Evaluate the interpolated function value and/or its
  // derivative. Returns false if x is out of bounds.
  bool Evaluate(double x, double* value, double* value_x) const;

  // Overload for Jets, which automatically accounts for the chain rule.
  template<typename JetT> bool Evaluate(const JetT& x, JetT* value) const {
    double f, fx;
    if (!Evaluate(x.a, &f, &fx)) { return false; }
    value->a = f;
    value->v = fx * x.v;
    return true;
  }

 private:
  const int num_values_;
  const double* values_;
};

}  // namespace ceres

#include "ceres/internal/reenable_warnings.h"

#endif  // CERES_PUBLIC_CUBIC_INTERPOLATOR_H_
