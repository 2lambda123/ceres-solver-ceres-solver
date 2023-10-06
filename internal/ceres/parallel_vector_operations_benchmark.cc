// Ceres Solver - A fast non-linear least squares minimizer
// Copyright 2023 Google Inc. All rights reserved.
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

#include <algorithm>

#include "benchmark/benchmark.h"
#include "ceres/eigen_vector_ops.h"
#include "ceres/parallel_for.h"

namespace ceres::internal {
// Older versions of benchmark library (for example, one shipped with
// ubuntu 20.04) do not support range generation and range products
#define BENCHMARK_SIZE \
  Arg(128)             \
      ->Arg(256)       \
      ->Arg(1024)      \
      ->Arg(4096)      \
      ->Arg(16384)     \
      ->Arg(65536)     \
      ->Arg(262144)    \
      ->Arg(1048576)   \
      ->Arg(4194304)   \
      ->Arg(8388608)

#define BENCHMARK_SIZE_THREADS \
  Args({128, 1})               \
      ->Args({128, 2})         \
      ->Args({128, 4})         \
      ->Args({128, 8})         \
      ->Args({128, 16})        \
      ->Args({256, 1})         \
      ->Args({256, 2})         \
      ->Args({256, 4})         \
      ->Args({256, 8})         \
      ->Args({256, 16})        \
      ->Args({1024, 1})        \
      ->Args({1024, 2})        \
      ->Args({1024, 4})        \
      ->Args({1024, 8})        \
      ->Args({1024, 16})       \
      ->Args({4096, 1})        \
      ->Args({4096, 2})        \
      ->Args({4096, 4})        \
      ->Args({4096, 8})        \
      ->Args({4096, 16})       \
      ->Args({16384, 1})       \
      ->Args({16384, 2})       \
      ->Args({16384, 4})       \
      ->Args({16384, 8})       \
      ->Args({16384, 16})      \
      ->Args({65536, 1})       \
      ->Args({65536, 2})       \
      ->Args({65536, 4})       \
      ->Args({65536, 8})       \
      ->Args({65536, 16})      \
      ->Args({262144, 1})      \
      ->Args({262144, 2})      \
      ->Args({262144, 4})      \
      ->Args({262144, 8})      \
      ->Args({262144, 16})     \
      ->Args({1048576, 1})     \
      ->Args({1048576, 2})     \
      ->Args({1048576, 4})     \
      ->Args({1048576, 8})     \
      ->Args({1048576, 16})    \
      ->Args({4194304, 1})     \
      ->Args({4194304, 2})     \
      ->Args({4194304, 4})     \
      ->Args({4194304, 8})     \
      ->Args({4194304, 16})    \
      ->Args({8388608, 1})     \
      ->Args({8388608, 2})     \
      ->Args({8388608, 4})     \
      ->Args({8388608, 8})     \
      ->Args({8388608, 16});

static void SetZero(benchmark::State& state) {
  const int kVectorSize = static_cast<int>(state.range(0));
  Vector x = Vector::Random(kVectorSize);
  for (auto _ : state) {
    x.setZero();
  }
  CHECK_EQ(x.squaredNorm(), 0.);
}
BENCHMARK(SetZero)->BENCHMARK_SIZE;

static void SetZeroParallel(benchmark::State& state) {
  const int kVectorSize = static_cast<int>(state.range(0));
  const int num_threads = static_cast<int>(state.range(1));
  ContextImpl context;
  context.EnsureMinimumThreads(num_threads);

  Vector x = Vector::Random(kVectorSize);
  for (auto _ : state) {
    ParallelSetZero(&context, num_threads, x);
  }
  CHECK_EQ(x.squaredNorm(), 0.);
}
BENCHMARK(SetZeroParallel)->BENCHMARK_SIZE_THREADS;

static void Negate(benchmark::State& state) {
  const int kVectorSize = static_cast<int>(state.range(0));
  Vector x = Vector::Random(kVectorSize).normalized();
  const Vector x_init = x;

  for (auto _ : state) {
    x = -x;
  }
  CHECK((x - x_init).squaredNorm() == 0. || (x + x_init).squaredNorm() == 0);
}
BENCHMARK(Negate)->BENCHMARK_SIZE;

static void NegateParallel(benchmark::State& state) {
  const int kVectorSize = static_cast<int>(state.range(0));
  const int num_threads = static_cast<int>(state.range(1));
  ContextImpl context;
  context.EnsureMinimumThreads(num_threads);

  Vector x = Vector::Random(kVectorSize).normalized();
  const Vector x_init = x;

  for (auto _ : state) {
    ParallelAssign(&context, num_threads, x, -x);
  }
  CHECK((x - x_init).squaredNorm() == 0. || (x + x_init).squaredNorm() == 0);
}
BENCHMARK(NegateParallel)->BENCHMARK_SIZE_THREADS;

static void Assign(benchmark::State& state) {
  const int kVectorSize = static_cast<int>(state.range(0));
  Vector x = Vector::Random(kVectorSize);
  Vector y = Vector(kVectorSize);
  for (auto _ : state) {
    y.block(0, 0, kVectorSize, 1) = x.block(0, 0, kVectorSize, 1);
  }
  CHECK_EQ((y - x).squaredNorm(), 0.);
}
BENCHMARK(Assign)->BENCHMARK_SIZE;

static void AssignParallel(benchmark::State& state) {
  const int kVectorSize = static_cast<int>(state.range(0));
  const int num_threads = static_cast<int>(state.range(1));
  ContextImpl context;
  context.EnsureMinimumThreads(num_threads);

  Vector x = Vector::Random(kVectorSize);
  Vector y = Vector(kVectorSize);

  for (auto _ : state) {
    ParallelAssign(&context, num_threads, y, x);
  }
  CHECK_EQ((y - x).squaredNorm(), 0.);
}
BENCHMARK(AssignParallel)->BENCHMARK_SIZE_THREADS;

static void D2X(benchmark::State& state) {
  const int kVectorSize = static_cast<int>(state.range(0));
  const Vector x = Vector::Random(kVectorSize);
  const Vector D = Vector::Random(kVectorSize);
  Vector y = Vector::Zero(kVectorSize);
  for (auto _ : state) {
    y = D.array().square() * x.array();
  }
  CHECK_GT(y.squaredNorm(), 0.);
}
BENCHMARK(D2X)->BENCHMARK_SIZE;

static void D2XParallel(benchmark::State& state) {
  const int kVectorSize = static_cast<int>(state.range(0));
  const int num_threads = static_cast<int>(state.range(1));
  ContextImpl context;
  context.EnsureMinimumThreads(num_threads);

  const Vector x = Vector::Random(kVectorSize);
  const Vector D = Vector::Random(kVectorSize);
  Vector y = Vector(kVectorSize);

  for (auto _ : state) {
    ParallelAssign(&context, num_threads, y, D.array().square() * x.array());
  }
  CHECK_GT(y.squaredNorm(), 0.);
}
BENCHMARK(D2XParallel)->BENCHMARK_SIZE_THREADS;

static void DivideSqrt(benchmark::State& state) {
  const int kVectorSize = static_cast<int>(state.range(0));
  Vector diagonal = Vector::Random(kVectorSize).array().abs();
  const double radius = 0.5;
  for (auto _ : state) {
    diagonal = (diagonal / radius).array().sqrt();
  }
  CHECK_GT(diagonal.squaredNorm(), 0.);
}
BENCHMARK(DivideSqrt)->BENCHMARK_SIZE;

static void DivideSqrtParallel(benchmark::State& state) {
  const int kVectorSize = static_cast<int>(state.range(0));
  const int num_threads = static_cast<int>(state.range(1));
  ContextImpl context;
  context.EnsureMinimumThreads(num_threads);

  Vector diagonal = Vector::Random(kVectorSize).array().abs();
  const double radius = 0.5;
  for (auto _ : state) {
    ParallelAssign(
        &context, num_threads, diagonal, (diagonal / radius).cwiseSqrt());
  }
  CHECK_GT(diagonal.squaredNorm(), 0.);
}
BENCHMARK(DivideSqrtParallel)->BENCHMARK_SIZE_THREADS;

static void Clamp(benchmark::State& state) {
  const int kVectorSize = static_cast<int>(state.range(0));
  Vector diagonal = Vector::Random(kVectorSize);
  const double min = -0.5;
  const double max = 0.5;
  for (auto _ : state) {
    for (int i = 0; i < kVectorSize; ++i) {
      diagonal[i] = std::min(std::max(diagonal[i], min), max);
    }
  }
  CHECK_LE(diagonal.maxCoeff(), 0.5);
  CHECK_GE(diagonal.minCoeff(), -0.5);
}
BENCHMARK(Clamp)->BENCHMARK_SIZE;

static void ClampParallel(benchmark::State& state) {
  const int kVectorSize = static_cast<int>(state.range(0));
  const int num_threads = static_cast<int>(state.range(1));
  ContextImpl context;
  context.EnsureMinimumThreads(num_threads);

  Vector diagonal = Vector::Random(kVectorSize);
  const double min = -0.5;
  const double max = 0.5;
  for (auto _ : state) {
    ParallelAssign(
        &context, num_threads, diagonal, diagonal.array().max(min).min(max));
  }
  CHECK_LE(diagonal.maxCoeff(), 0.5);
  CHECK_GE(diagonal.minCoeff(), -0.5);
}
BENCHMARK(ClampParallel)->BENCHMARK_SIZE_THREADS;

static void Norm(benchmark::State& state) {
  const int kVectorSize = static_cast<int>(state.range(0));
  const Vector x = Vector::Random(kVectorSize);

  double total = 0.;
  for (auto _ : state) {
    total += x.norm();
  }
  CHECK_GT(total, 0.);
}
BENCHMARK(Norm)->BENCHMARK_SIZE;

static void NormParallel(benchmark::State& state) {
  const int kVectorSize = static_cast<int>(state.range(0));
  const int num_threads = static_cast<int>(state.range(1));
  ContextImpl context;
  context.EnsureMinimumThreads(num_threads);

  const Vector x = Vector::Random(kVectorSize);

  double total = 0.;
  for (auto _ : state) {
    total += Norm(x, &context, num_threads);
  }
  CHECK_GT(total, 0.);
}
BENCHMARK(NormParallel)->BENCHMARK_SIZE_THREADS;

static void Dot(benchmark::State& state) {
  const int kVectorSize = static_cast<int>(state.range(0));
  const Vector x = Vector::Random(kVectorSize);
  const Vector y = Vector::Random(kVectorSize);

  double total = 0.;
  for (auto _ : state) {
    total += x.dot(y);
  }
  CHECK_NE(total, 0.);
}
BENCHMARK(Dot)->BENCHMARK_SIZE;

static void DotParallel(benchmark::State& state) {
  const int kVectorSize = static_cast<int>(state.range(0));
  const int num_threads = static_cast<int>(state.range(1));
  ContextImpl context;
  context.EnsureMinimumThreads(num_threads);

  const Vector x = Vector::Random(kVectorSize);
  const Vector y = Vector::Random(kVectorSize);

  double total = 0.;
  for (auto _ : state) {
    total += Dot(x, y, &context, num_threads);
  }
  CHECK_NE(total, 0.);
}
BENCHMARK(DotParallel)->BENCHMARK_SIZE_THREADS;

static void Axpby(benchmark::State& state) {
  const int kVectorSize = static_cast<int>(state.range(0));
  const Vector x = Vector::Random(kVectorSize);
  const Vector y = Vector::Random(kVectorSize);
  Vector z = Vector::Zero(kVectorSize);
  const double a = 3.1415;
  const double b = 1.2345;

  for (auto _ : state) {
    z = a * x + b * y;
  }
  CHECK_GT(z.squaredNorm(), 0.);
}
BENCHMARK(Axpby)->BENCHMARK_SIZE;

static void AxpbyParallel(benchmark::State& state) {
  const int kVectorSize = static_cast<int>(state.range(0));
  const int num_threads = static_cast<int>(state.range(1));
  ContextImpl context;
  context.EnsureMinimumThreads(num_threads);

  const Vector x = Vector::Random(kVectorSize);
  const Vector y = Vector::Random(kVectorSize);
  Vector z = Vector::Zero(kVectorSize);
  const double a = 3.1415;
  const double b = 1.2345;

  for (auto _ : state) {
    Axpby(a, x, b, y, z, &context, num_threads);
  }
  CHECK_GT(z.squaredNorm(), 0.);
}
BENCHMARK(AxpbyParallel)->BENCHMARK_SIZE_THREADS;

}  // namespace ceres::internal

BENCHMARK_MAIN();
