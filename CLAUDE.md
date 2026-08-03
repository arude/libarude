# libarude

A utility and helper library for C++ projects.

## Source file headers

Every new source file (`.hpp`, `.cpp`) starts with the header in
[`support/template_file_header.hpp`](support/template_file_header.hpp), which is
the canonical copy. Reproduced here for convenience — if the two ever disagree,
the file wins, and this block should be corrected to match:

```cpp
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
```

Do not use the long FSF boilerplate. The SPDX identifier is the machine-readable
part and must stay exactly `AGPL-3.0-or-later`.

`support/` holds tooling and templates, not library code — keep it out of build
globs.

## Formatting

`.clang-format` is authoritative and documented inline. Requires clang-format 21
or newer. Do not hand-format around it — run `clang-format -i` instead.

Note that it enables `InsertBraces` and `RemoveSemicolon`, so clang-format will
modify code, not just whitespace.

## Licensing constraints

libarude is dual licensed: AGPLv3 by default, MIT to individual licensees by
private arrangement. Two rules follow from that:

- **Never add an MIT license file to this repository.** A committed MIT text
  reads as a standing grant to everyone who clones, which destroys the point of
  the arrangement. The MIT option is described in `LICENSING.md` and granted per
  licensee, in writing.
- **Do not weaken the contribution terms in `README.md`.** They require
  contributors to grant relicensing rights. Without that, the first merged pull
  request makes it legally impossible to offer the whole library under MIT.
