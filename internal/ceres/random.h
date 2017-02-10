// Ceres Solver - A fast non-linear least squares minimizer
// Copyright 2015 Google Inc. All rights reserved.
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
// Author: keir@google.com (Keir Mierle)
//         sameeragarwal@google.com (Sameer Agarwal)

#ifndef CERES_INTERNAL_RANDOM_H_
#define CERES_INTERNAL_RANDOM_H_

#include <cmath>
#include <cstdlib>
#include "ceres/internal/port.h"

namespace ceres {

inline void SetRandomState(int state) {
  srand(state);
}

inline int Uniform(int n) {
  return rand() % n;
}

inline double RandDouble() {
  double r = static_cast<double>(rand());
  return r / RAND_MAX;
}

// Uniform in range [-n n]
inline double RandDoubleUniform(double n) {
  double r = static_cast<double>(rand());
  return (2 * (r / RAND_MAX) - 1) * n;
}

// Box-Muller algorithm for normal random number generation.
// http://en.wikipedia.org/wiki/Box-Muller_transform
inline double RandNormal() {
  double x1, x2, w;
  do {
    x1 = 2.0 * RandDouble() - 1.0;
    x2 = 2.0 * RandDouble() - 1.0;
    w = x1 * x1 + x2 * x2;
  } while ( w >= 1.0 || w == 0.0 );

  w = sqrt((-2.0 * log(w)) / w);
  return x1 * w;
}

}  // namespace ceres

#endif  // CERES_INTERNAL_RANDOM_H_
