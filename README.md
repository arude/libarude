# libarude

[![build](https://github.com/arude/libarude/actions/workflows/build.yml/badge.svg)](https://github.com/arude/libarude/actions/workflows/build.yml)
[![tidy](https://github.com/arude/libarude/actions/workflows/tidy.yml/badge.svg)](https://github.com/arude/libarude/actions/workflows/tidy.yml)
[![checks](https://github.com/arude/libarude/actions/workflows/checks.yml/badge.svg)](https://github.com/arude/libarude/actions/workflows/checks.yml)
[![coverage](https://codecov.io/gh/arude/libarude/graph/badge.svg)](https://codecov.io/gh/arude/libarude)
[![license](https://img.shields.io/badge/license-AGPL--3.0--or--later-blue)](LICENSING.md)

A utility and helper library for C++ projects.

> **Status: early.** The repository is being rebuilt from scratch. There is not
> much here yet.

## Building

Requires CMake 4.2 or newer, Ninja, and a C++23-capable compiler — Clang 21 is
what the project is developed against. The first configure downloads
[magic_enum](https://github.com/Neargye/magic_enum),
[reflect-cpp](https://github.com/getml/reflect-cpp) and Catch2 through
[CPM](https://github.com/cpm-cmake/CPM.cmake), so it needs network access; later
ones do not. `-DARUDE_NO_CONFIG=ON` skips reflect-cpp.

Of those, only Catch2 is test-only. magic_enum is header-only and MIT licensed,
and reflect-cpp comes with the configuration support: both are part of the
library itself, so a consumer of the enum helpers or of `arude/config.hpp`
gets their headers on its include path.

```sh
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

In-source builds are rejected, so configure into a separate directory. `build/`
and `build-*/` are gitignored.

### Running the tests

```sh
ctest --test-dir build --output-on-failure
```

Each scenario is registered with CTest individually. The test binary can also
be run directly, which gives Catch2's own filtering and reporting:

```sh
./build/libarude_test                 # Everything.
./build/libarude_test "[hello_world]" # One tag.
./build/libarude_test --list-tests    # What is available.
```

### Running the benchmarks

Benchmarks use [Google Benchmark](https://github.com/google/benchmark) and build
into their own binary. Configure as `Release` — timings from a `Debug` build are
not worth reading:

```sh
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/libarude_benchmark
```

```sh
./build/libarude_benchmark --benchmark_filter=hello   # A subset.
./build/libarude_benchmark --benchmark_min_time=0.05s # Finish sooner.
```

A `CPU scaling is enabled` warning means the governor is free to change clock
speed mid-run; results will be noisy.

### Options

| Option | Default | Effect |
| --- | --- | --- |
| `ARUDE_CXX_STANDARD` | `26`, or `23` if the compiler cannot do 26 | Language standard |
| `BUILD_TESTING` | `ON` | `OFF` skips the Catch2 download and the test target |
| `ARUDE_BUILD_BENCHMARKS` | `ON` | `OFF` skips the Google Benchmark download and its target |
| `ARUDE_NO_CONFIG` | `OFF` | `ON` skips the reflect-cpp download, and with it `arude/config.hpp` |
| `ARUDE_HTTPS` | `OFF` | `ON` adds the HTTPS configuration transport: downloads cpp-httplib and needs a TLS backend on the machine |
| `ARUDE_ENABLE_COVERAGE` | `OFF` | Adds `--coverage` instrumentation, for the coverage job |
| `CMAKE_BUILD_TYPE` | — | `Debug` or `Release` |

### Documentation

Doxygen alone produces the full HTML site — no other tooling required:

```sh
doxygen
xdg-open docs/html/index.html
```

Graphviz is optional. With it you additionally get inheritance and dependency
diagrams; without it everything else is unchanged.

Undocumented public API is written to `docs/warnings.log` rather than stopping
the run, so the site is produced either way. The log is advisory locally, as
clang-tidy's warnings are; CI fails the docs job when it is not empty, which
holds the baseline at zero. The site is uploaded as an artifact on every push,
including when that check fails.

### Static analysis

`.clang-tidy` requires clang-tidy 21 and a compilation database, which the
build always generates:

```sh
clang-tidy -p build $(git ls-files 'src/*.cpp' 'test/*.cpp' 'benchmark/*.cpp')
```

Warnings are advisory locally; CI adds `--warnings-as-errors='*'`. Mechanical
findings can be applied with `--fix`, though the result is worth reading before
committing. Suppress a justified exception in place with `// NOLINT(check-name)`
rather than loosening the config.

### Formatting

`.clang-format` is authoritative and requires clang-format 21 or newer:

```sh
clang-format -i $(git ls-files --cached --others --exclude-standard '*.hpp' '*.cpp')
```

`--others` includes files that are not committed yet; plain `git ls-files`
would silently skip them.

## License

libarude is dual licensed.

**Open source use — GNU AGPLv3.** By default libarude is available under the
GNU Affero General Public License, version 3 or later. See [LICENSE](LICENSE)
for the full terms. The AGPL permits commercial use, but it is strongly
copyleft: if you distribute software built on libarude, **or** make it available
to users over a network, you must release the complete corresponding source of
that software under the AGPL as well.

**Proprietary use — MIT, by arrangement.** If the AGPL's copyleft does not suit
your project, I can grant you the same code under the MIT license instead. This
is not a standing offer to the public — it is negotiated per licensee, in
exchange for conditions or compensation. What I ask for varies: it might be a
payment, code sharing, an honourable mention, or nothing at all.

To discuss terms, contact **adrian@mav.ch**.

See [LICENSING.md](LICENSING.md) for how the two options relate.

## Contributing

Contributions are welcome. Because libarude is also offered under a second,
non-AGPL license, I need to remain able to license the whole work under those
alternative terms — which is only possible if I hold or control the rights to
all of it.

By submitting a contribution you therefore agree that:

1. Your contribution is licensed to the public under the AGPLv3, and
2. You grant me a perpetual, worldwide, irrevocable, royalty-free right to also
   license your contribution under other terms, including the MIT license.

You keep the copyright to your own work; this is a grant of permission, not a
transfer of ownership. If you are not able to make that grant — for example
because your employer owns the code — please say so before opening a pull
request so we can find another way.

## Copyright

Copyright © 2026 Adrian Rudin (arude).
