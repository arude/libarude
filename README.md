# libarude

A utility and helper library for C++ projects.

> **Status: early.** The repository is being rebuilt from scratch. There is not
> much here yet.

## Building

Requires CMake 4.2 or newer, Ninja, and a C++23-capable compiler — Clang 21 is
what the project is developed against. The first configure downloads Catch2
through [CPM](https://github.com/cpm-cmake/CPM.cmake), so it needs network
access; later ones do not.

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

### Options

| Option | Default | Effect |
| --- | --- | --- |
| `ARUDE_CXX_STANDARD` | `26` | Language standard; set to `23` for older toolchains |
| `BUILD_TESTING` | `ON` | `OFF` skips the Catch2 download and the test target entirely |
| `CMAKE_BUILD_TYPE` | — | `Debug` or `Release` |

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
