# C++ conventions

Rules for writing libarude. `CLAUDE.md` imports this file, so it is in context
when code is generated; the review skill checks against it rather than keeping
a second copy.

libarude is a header-mostly utility library, so its interfaces are the
expensive part — they propagate to every consumer and are hard to change once
released. Where a rule below trades convenience for a cleaner interface, take
the interface.

## Headers

- `#pragma once` at the top of every header, below the licence block.
- Never `using namespace` at namespace scope in a header.
- Include what you use; do not lean on transitive includes.
- Implementation details belong in `namespace detail`, not the public
  namespace.

## Ownership and lifetime

- RAII rather than manual acquire/release pairs.
- Prefer the rule of zero. If you declare any of destructor, copy, or move,
  account for all five.
- No owning raw pointers in interfaces — use value types, references, or smart
  pointers.
- Do not retain a reference or `string_view` parameter beyond the call unless
  the interface says so explicitly.

## Correctness

- const-correct member functions and parameters.
- Top-level `const` on a by-value parameter is not part of the interface — the
  compiler discards it from the signature. Leave it off declarations, and put
  it on definitions, where it documents and enforces that the local copy is
  never modified:

  ```cpp
  void store(int value);  // Declaration

  void store(const int value)  // Definition
  {
  }
  ```

- `explicit` on single-argument constructors.
- `[[nodiscard]]` wherever discarding the result is a bug.
- Be deliberate about integer types; avoid signed/unsigned mixing.
- Handle self-assignment, and leave moved-from objects in a valid state.

## Templates

- Prefer an abbreviated function template — an `auto` parameter — over an
  explicit `template<...>` clause wherever the type is not needed by name.
  Constraints still apply, so this costs nothing in rigour:

  ```cpp
  void take(std::integral auto value);  // Preferred

  template<std::integral T>  // Only when T must be named
  void take(T value);
  ```

  Reach for the explicit clause when the type must be named in the body, in a
  return type, or in a second parameter that has to match.

- Constrain with concepts or `requires`, so misuse fails at the interface
  rather than deep inside instantiation.
- Watch for unintended ADL.
- Aim for errors a caller can act on.

## Exceptions

- `noexcept` on move operations and destructors.
- Basic guarantee at minimum; state where a stronger one is intended.
- Nothing escapes a destructor.

## Comments

These apply to every file in the repository, not only C++ — build scripts, CI
workflows, and tool configuration such as `.clang-format` follow the same rule.

- Start a comment with a capital letter, as you would a sentence — including
  short trailing ones:

  ```cpp
  // Cache the result; the lookup is not cheap.
  int total = 0;  // Running sum, reset per row.
  ```

- This applies to the first line of a comment, not to every line. A wrapped
  comment is one sentence and continues in lower case:

  ```cpp
  // Consecutive alignment is off by design: it makes an unrelated line reflow
  // whenever the longest name in a group changes, which pollutes diffs.
  ```

- Do not capitalise something that is genuinely spelled lowercase: an
  identifier, a filename, or a Doxygen command keeps its own casing. All three
  of these are correct as written:

  ```cpp
  /// \copyright Copyright 2026 Adrian Rudin (arude).
  /// libarude is dual licensed. See LICENSE for the AGPLv3 terms.
  ```

  ```yaml
  # libarude code formatting.
  ```

  Reword rather than mangle a name. If a sentence would start with `size_t`,
  put something ahead of it instead of writing `Size_t`.

## Formatting

Not a judgement call — `.clang-format` decides. Run `clang-format -i` rather
than hand-formatting.
