/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.

#include "arude/hello_world.hpp"

#include <benchmark/benchmark.h>

namespace
{

///
///
auto bench_hello_world(benchmark::State& state) -> void
{
  // DoNotOptimize is load-bearing. hello_world returns its argument, so
  // without it the compiler removes the call and the loop measures nothing.
  for(auto _ : state)
  {
    benchmark::DoNotOptimize(arude::hello_world(42));
  }
}

BENCHMARK(bench_hello_world);

///
///
auto bench_hello_world_range(benchmark::State& state) -> void
{
  const int value = static_cast<int>(state.range(0));

  for(auto _ : state)
  {
    benchmark::DoNotOptimize(arude::hello_world(value));
  }
}

BENCHMARK(bench_hello_world_range)->Arg(0)->Arg(1 << 16);

} // namespace
