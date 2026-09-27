# Project conventions

These are the maintainer's preferences for how Panda code is written and organized.
They apply to owned code, including examples and tests. Formatting is defined by
the checked-in `.clang-format`; this page covers choices that formatting alone
cannot enforce. Some may also be checked by static analysis.

The [quality policy](quality.md) owns correctness and verification requirements.
An API requirement or a concrete correctness constraint takes precedence over a
style preference. Keep exceptions local and explain non-obvious ones. Extend the
relevant section here as conventions are agreed; do not duplicate rules elsewhere.
Apply agreed conventions to the code being changed. Schedule broader consistency
refactors separately rather than mixing them into unrelated work.

## Names and declarations

- Use `_window`-style names for private data members. Access members without
  `this->`, except when C++ requires it, such as dependent-base member lookup.
- Boolean query names must read as questions, such as `isValid()`, `hasValue()`
  or `shouldClose()`.
- Use `get` for property reads and `set` for property writes, such as `getSize()`
  and `setSize(...)`. Boolean reads keep their question prefixes, such as
  `isEnabled()`, paired with `setEnabled(...)`. Operations that create, calculate,
  load or wait use verbs describing that action rather than a generic `get`.
- Use full words in names rather than shortened spellings. Keep established API
  and domain acronyms, such as `GLFW`, where expanding them would obscure meaning.
- Use PascalCase enumerators, such as `InvalidArgument`.
- Use trailing return types for functions returning a value: `auto getSize() -> Size`.
  Declare void functions as `void flush()`. Constructors, destructors and conversion
  operators follow their required C++ syntax.
- Use `typename`, rather than `class`, for template type parameters.

## Initialization and local variables

- Use `=` to initialize local variables declared with `auto`. Prefer braces (`{}`)
  when constructing values on the right-hand side and initializing data members.
  Assignment, default arguments and designated initializers also use `=`.
  Avoid braces when they would select an unintended `initializer_list` overload.
- Always use `auto` for local variables, including loop variables and declarations
  in conditions. Use `const` unless mutation is required. When a specific type is
  needed for representation or an API, express it through the initializer.
  Select `auto&` or `const auto&` deliberately when borrowing to avoid unintended
  copies. This rule does not apply to function parameters or class data members.

  ```cpp
  const auto windowTitle = std::string{"Panda - Simple scene"};
  auto width = std::uint32_t{1280};
  const auto& entry = entries[index];
  ```
- Prefer `static constexpr` for constant variables where permitted, except where
  `static` is redundant, such as in an anonymous namespace. This does not change
  the use of `constexpr` functions or `if constexpr`.

## Integer types

- Default to unsigned integers with explicit widths from `<cstdint>`, such as
  `std::uint32_t`. Choose the width for the value's range and representation.
- Prefer `std::size_t` for sizes, indices and size counters across platforms.
  Use pointer-sized types for addresses or identities derived from pointers.
- Use signed integers when negative values are meaningful. Use native types such
  as `int` when required by an API, for example GLFW; do not replace its output
  variables with incompatible pointer types.
- With `auto`, choose an initializer that deduces the intended type; an ordinary
  integer literal does not acquire an unsigned fixed-width type automatically.

## Class and file organization

- Structures must have only public members. Prefer `struct` for plain data bundles.
  Use `class` when a type encapsulates state, and prefer it for types that provide
  operations or manage behavior and lifetime.
- Put a class's public section before its private section unless a declaration
  dependency requires another order.
- Within each access section, order function declarations as static functions,
  constructors/destructor, then other functions. Within those categories, group
  related responsibilities and keep overloads together. Required declaration
  dependencies take precedence.
- Separate functions with blank lines. Also separate groups of static declarations
  and data members from functions and from each other. Related data members may
  remain together without a blank line between every field.
- Keep standalone implementation classes in their own matching files when this
  improves readability; do not spread a nested class definition across files
  merely to simulate a standalone class.
- Use `#pragma once` for owned headers and PascalCase names for C++ files that
  represent types, such as `Window.hpp` and `Window.cpp`. Conventional entry points
  such as `main.cpp` retain their names.
- Put function definitions in `.cpp` files, including trivial accessors. Keep
  template definitions and definitions required for constant evaluation in headers
  when callers need them. Keep private templates in `.cpp` files when their uses
  and required instantiations can remain there; access control alone does not remove
  the language requirement for a visible template definition.

## Functions and control flow

- Prefer default arguments for straightforward defaults. When callers must
  deliberately choose absence, require an explicit argument such as
  `std::optional<T>` with no default. Prefer a separately named operation when
  absence represents a distinct intent that a name can express more clearly.
- Use lambdas to pass callable behavior to another function, for example a
  predicate or callback. Do not use named local lambdas or immediately invoked
  lambdas as ordinary helper functions; extract a regular function instead.
- Use modern C++ when it simplifies the result. Prefer direct branching for
  recoverable errors; use `expected`'s monadic operations when the chain is clearer.
  A straightforward loop is preferable to a complicated ranges pipeline. Choose
  clarity for the actual operation rather than novelty or minimum line count.

## Comments and intentionally unused results

- Reserve `///` comments for public API documentation. Use ordinary `//` comments
  for internal classes, helpers and implementation explanations. Being in a class's
  `public` section does not make an internal helper part of the public Panda API.
- Document every public API function. Explain its useful contract rather than
  restating its name: unusual assumptions, ownership and borrowing lifetimes,
  units, errors, thread requirements and side effects where applicable. Prefer
  an API that prevents misuse through its types and operations over one that
  relies on callers remembering comments.
- Do not add closing namespace comments.
- When ignoring a result is appropriate, use `std::ignore = expression` with
  `<tuple>`. Use `[[maybe_unused]]` for a named local that is intentionally unused.
  Do not use a cast to void. The quality policy still requires recoverable errors
  to be handled unless ignoring them is justified.
