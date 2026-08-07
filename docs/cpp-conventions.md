# C++ conventions

Rules for writing libarude, imported by `CLAUDE.md` so they apply while code is
written; the review skill checks against them. Target C++23.

`.clang-format` owns everything about formatting and is not repeated here —
never hand-format, run `clang-format -i`.

libarude is header-mostly, so interfaces are the expensive part: they reach
every consumer and are hard to withdraw. Where a rule trades convenience for a
cleaner interface, take the interface.

## Naming

- Files `snake_case.hpp` / `.cpp`, named after the type they contain.
- Types, functions, variables, namespaces: `snake_case`. No Hungarian notation.
- Functions read as commands: `create_instance()`, `do_start()`.
- Private and protected data members take a trailing underscore: `counter_`.
- Type aliases end in `_t`, as in the STL: `value_vec_t`.
- Template parameters are `CamelCase` — `T`, `Allocator`, `TFirstParameter`.
  The one exception to `snake_case`, so a parameter is distinguishable from a
  concrete type where it is used.
- Accessors are named for the property — `size()`, `size(std::size_t)`, never
  `get_size()` / `set_size()`.
- Namespaces are rooted at `arude`; implementation details in `arude::detail`.
- Locals in the narrowest scope that works, initialised at declaration.
- Constants are `constexpr`: `inline` at namespace scope, `static` in a
  function.
- Macros are a last resort; `ALL_CAPS_WITH_UNDERSCORES` if unavoidable.

## Headers

- `#pragma once` on the first line after the licence block.
- Never `using namespace` at namespace scope in a header.
- Include what you use; do not lean on transitive includes.
- Include from the include root — `"libarude/detail/buffer.hpp"`, never
  `"../../buffer.hpp"`. This holds in a `.cpp` including its own header.
- `""` within libarude, `<>` for third-party and standard headers.
- Forward-declare to cut header dependencies.
- Order: licence block, `#pragma once`, project includes, third-party includes,
  standard includes, forward declarations, then the `arude` namespace.
  `.clang-format` sorts within each group; keeping the groups apart is yours.
- Every `#include` comes before every `import`. GCC rejects the other order
  outright, so this is a portability rule rather than a matter of taste:

  ```cpp
  #include <print> // All includes first.

  import arude; // Then imports.
  ```

## Classes

- `class` by default. `struct` only for passive aggregates, functors, and
  traits — anything with no invariant to defend.
- `final` unless designed to be derived from.
- Data members `private`, exposed through accessors as needed.
- Prefer composition. Inheritance only for a genuine "is-a", and only public.
- To let a base vary one step of an algorithm, use the template method pattern.
- Inject collaborators through the constructor, not globals or singletons.
- One responsibility per class.
- Constructors establish invariants and do no complex or fallible work; expose
  an explicit init method where that is needed.
- Never call a virtual function from a constructor or destructor.
- No empty default constructor — use default member initialisers and let the
  compiler generate it.
- `explicit` on single-argument constructors, except copy/move and transparent
  wrappers.
- Destructor `virtual` if the class has any virtual function, `protected` if
  the type must not be deleted through a base pointer.
- Copy and move only where they mean something; otherwise `= delete` or
  inherit `arude::noncopyable`.

Declaration order, omitting empty sections: friend declarations before any
access specifier, then `public:`, `protected:`, `private:` — repeated if
grouping helps — and within each: constants, aliases and enums, constructors
and destructor, methods, data members.

Definitions go after the class, never inside it, so the body reads as an
interface. The exception is a body that is empty or nothing but `return {};`,
which carries no logic and would cost three lines to move out:

```cpp
class counter final
{
public:
  auto value() const -> std::size_t;
  auto reset() -> void {}
  auto clone() const -> counter { return {}; }

private:
  std::size_t value_ = 0;
};

inline auto counter::value() const -> std::size_t
{
  return value_;
}
```

- A definition kept in the header needs `inline`; a member of a class template
  does not.
- For non-template types the definitions belong in the matching `.cpp`.
- Function bodies only — default member initialisers stay in the class.
- Definition order follows declaration order, wherever they live.

## Functions

- Small and single-purpose; around 80 lines is the point to reconsider.
- Trailing return type throughout: `auto parse(std::string_view t) -> bool`.
- Parameter names match between declaration and definition.
- Unused parameters keep their name and gain `[[maybe_unused]]`.
- `[[nodiscard]]` wherever discarding the result is a bug.
- `constexpr` where the function could be evaluated at compile time.
- `return value;`, not `return (value);`.
- `noexcept` only where it buys something: destructors, move construction and
  assignment, `swap`.
- Top-level `const` on a by-value parameter is discarded from the signature, so
  leave it off declarations and put it on definitions, where it enforces that
  the local copy is never modified:

  ```cpp
  auto store(int value) -> void; // Declaration.

  auto store(const int value) -> void // Definition.
  {
  }
  ```

## Templates

- Prefer an abbreviated function template — an `auto` parameter — to an
  explicit `template<...>` clause. Constraints still apply, so it costs nothing
  in rigour. Use the explicit clause only when the type must be named in the
  body, in a return type, or across two parameters that must match:

  ```cpp
  auto take(std::integral auto value) -> void; // Preferred.

  template<std::integral T> // Only when T must be named.
  auto take(T value) -> void;
  ```

- Constrain with concepts or `requires`, so misuse fails at the interface
  rather than deep inside instantiation.
- Prefer templates to conditional compilation.
- `static_assert` for compile-time expectations a concept cannot express.
- Watch for unintended ADL.

## Ownership and lifetime

- RAII rather than manual acquire/release pairs.
- Prefer the rule of zero; if you declare any of destructor, copy, or move,
  account for all five.
- No owning raw pointers in interfaces — value types, references, or smart
  pointers.
- `std::unique_ptr` for sole ownership, `std::shared_ptr` only where ownership
  is genuinely shared. No bare `new` or `delete`; use `std::make_unique` /
  `std::make_shared`.
- Do not retain a reference, `std::string_view`, or `std::span` parameter
  beyond the call unless the interface says so.
- Move where it saves real work. Return by value and let RVO work rather than
  hand-writing a move it would elide.
- Avoid static and global objects of class type; hand them out through an
  accessor function if unavoidable.
- Handle self-assignment; leave moved-from objects valid.

## Modern C++

- `auto` where the type is evident from the initialiser or simply noise;
  `decltype(auto)` for forwarding a return type. Spell the type out when it is
  the point of the line.
- Range-based `for` over index loops; `std::ranges` algorithms over hand-rolled
  loops and iterator pairs.
- Structured bindings: `auto [ok, value] = parse(text);`
- Lambdas rather than `std::bind`; capture explicitly and mind the lifetime of
  anything captured by reference.
- `std::format` and `std::print` rather than iostreams.
- `std::filesystem::path` for every path, never a string.
- `std::string_view` and `std::span` for non-owning parameters.
- Reach for the STL before writing a container or algorithm.
- `nullptr` for pointers, `0` for integers, `0.0` for floating point, `'\0'`
  for characters. Never `NULL`.
- `sizeof(variable)` rather than `sizeof(type)` — it cannot drift.
- `.at()` rather than `operator[]` where the index is not already known to be
  in range; `insert_or_assign` on maps, since `operator[]` default-constructs.
- Prefix `++i` over postfix; for non-trivial types the difference is real.
- Be deliberate about integer types; avoid signed/unsigned mixing, and mind
  pointer size, alignment, and narrowing across 64-bit platforms.
- Overload operators only where the meaning is obvious, and in matching sets:
  `+` with `-`, `++` with `--`, prefix with postfix.
- `friend` sparingly — it is coupling.

## Types and casts

- `const` wherever it holds.
- Never a C-style cast. `static_cast` for value conversions and upcasts;
  `dynamic_cast` rarely, as needing it suggests a design problem;
  `reinterpret_cast` only with a clear account of the aliasing rules.
- `(void)` only to silence a warning that cannot be removed otherwise.
- Avoid RTTI; prefer a virtual function or a visitor for type-dependent
  behaviour.

## Error handling

- Exceptions signal what should not happen in normal operation, not ordinary
  control flow.
- Throw `arude::exception` with a message saying what failed.
- General handlers are `catch(...)`, reporting through
  `arude::exception_report()` rather than `e.what()`.
- Destructors never throw, and nothing escapes one.
- Basic guarantee at minimum; say where a stronger one is intended.
- `std::expected` is permitted where failure is expected and local, but
  exceptions remain the default.

## Preprocessor

- `#if (defined XXX)` rather than `#ifdef XXX`; it composes with `&&` and `||`
  without rewriting.
- Close every conditional with the condition it opened:
  `#endif // #if (defined XXX)`.
- Platform and compiler checks use `_MSC_VER`, `__GNUC__`, `__linux__`,
  `_WIN32`.
- A template or a constant is almost always better than a conditional block.

## Comments and documentation

Applies to every file, not only C++ — build scripts, CI workflows, and tool
configuration follow the same rules.

- English, spelled and punctuated properly.
- `//` for implementation comments, one space before the text.
- Start a comment with a capital letter, as you would a sentence — including
  short trailing ones. This applies to the first line only; a wrapped comment
  is one sentence and continues in lower case.
- Do not capitalise what is genuinely spelled lowercase: an identifier, a
  filename, or a Doxygen command keeps its own casing. Reword rather than
  mangle a name — if a sentence would start with `size_t`, put something ahead
  of it instead of writing `Size_t`.

  ```cpp
  // Cache the result; the lookup is not cheap.
  int total = 0; // Running sum, reset per row.

  /// \copyright Copyright 2026 Adrian Rudin (arude).
  /// libarude is dual licensed. See LICENSE for the AGPLv3 terms.
  ```

- Explain the tricky and the non-obvious. Do not narrate what the code plainly
  says; needing a comment to explain *what* is happening usually means the code
  is the thing to fix.
- `// TODO: <initials>: What needs doing.`

Every definition written apart from its declaration is preceded by two `///`
lines — a visual break between definitions, not documentation. The Doxygen
belongs on the declaration. A trivial body left in the class is its own
declaration and already carries that Doxygen, so it takes neither:

```cpp
///
///
auto hello_world(const int value) -> int
{
  return value;
}
```

Every public class and function carries Doxygen: template parameters,
parameters, return values, and anything a caller cannot infer — units, ranges,
pre- and post-conditions, side effects, what may be thrown. A one-line
description stays on one line.

```cpp
///
/// Brief description.
/// Detailed description.
///
/// \tparam T Element type.
/// \param val Value to convert, in millimetres.
/// \return Number of elements written.
/// \throws arude::exception If val is out of range.
///
auto convert(std::size_t val) -> int;
```
