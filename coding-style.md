## Coding style

- Capitalization
  - Types, functions, and member functions use PascalCase.
  - Variables use camelCase, treating the prefix as the first word.
  - Private/protected member functions use camelCase.

- Use project naming conventions consistently:
  - Classes/types use `C` (class), `S` (struct), `I` (abstract bases), or `E` (enum) prefixes (for example `CGame`, `SInputEvent`, `IEntity`, `EButton`).
  - Private/protected class member fields use `m_` prefixes. Struct and public member fields use no extra prefix.
  - Global variables use a `g_` prefix.
  - `static` variables use an `s_` prefix;
  - Pointers and smart pointers use `p` prefixes. (combined with other prefixes like m_pfValue)
  - References use `r` prefixes. (combined with other prefixes like m_rfValue)
  - std::optional use `o` prefixes.
  - Booleans use `b` prefixes.
  - Floats use `f` prefixes.
  - Signed integers and integer-like use `i` prefixes.
  - Unsigned integers and integer-like use `u` prefixes.
  - std::vectors use `v` prefixes.
  - std::string, std::string_view, and other object based string-likes use `s` prefixes.
  - Characters use `c` prefixes.
  - Enum variables use `e` prefixes.
  - C-Style strings use `pc` prefixes.

- Pointers and references
  - East const for both pointers and references.
  - `*` and `&` to the left.

- class/struct ordering
  1. public types
  2. protected types
  3. private types
  4. public variables
  5. protected variables
  6. private variables
  7. public member functions
  8. protected member functions
  9. private member functions

- Preserve existing formatting:
  - Use tabs for indentation.
  - Keep opening braces on the next line for functions, control flow, and lambdas.
  - Prefer compact single-line `if` bodies when the body is trivial.
  - Keep blank lines between logical blocks.

- Headers
  - Use `#pragma once` in all headers.
  - Implement inline functions in a separate `.inlines.h` file named the same as the primary header. Include at the bottom of the header.
  - Keep every header and source file self-contained: include all directly used dependencies (standard library, third-party, and project), even if they are already included by other headers or by the precompiled header.

- Include style:
  - Do not `#include "pch.h"` in any file. The precompiled header is purely optional and is force-included by the build (`target_precompile_headers`); no file may rely on what it pulls in.
  - Group includes by project area and keep them ordered and readable.
  - Use quoted includes, not angle brackets, for project headers.
  - Project headers should be included before system/third-party headers.

- Prefer explicit, readable local naming:
  - Use descriptive variable names with engine-style prefixes, such as `fScreenWidth`, `pWindow`, `bCentered`, and `uWindowFlags`.
  - Keep temporary variables short but type-signaling.

- Control-flow style:
  - Use early returns for simple guard conditions.
  - Use `switch` statements for event dispatch.
  - Keep nested logic structured with clear `if`/`else if` chains.

- Preserve the existing compile-time toggling pattern:
  - Use `#if 0` / `#if 1` blocks for temporary feature gating, experiments, or debug-only sections when consistent with surrounding code.
  - Do not remove inactive blocks unless explicitly asked.

- Match the project’s API usage style:
  - Use brace initialization for vectors, colors, rects, and similar value types.

- Comment style:
  - Use `//` comments.
  - Keep commented-out experimental code if it matches surrounding exploratory/debug patterns.
