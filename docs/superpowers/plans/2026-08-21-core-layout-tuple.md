# Core Layout Tuple Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the Core tuple alias with a Core-owned, struct-layout-preserving tuple supporting arities zero through sixteen.

**Architecture:** `tuple.h` uses generated arity specializations with direct ordered members and free tuple utility functions. `stl.h` only re-exports the Core type and its helpers. Layout is verified against mirror structs through actual member addresses.

**Tech Stack:** C++20, CMake, Ninja Multi-Config, MSVC x64.

**Spec:** `docs/superpowers/specs/2026-08-21-core-layout-tuple-design.md`

## Global Constraints

- New implementation lives in `src/core/tuple.h` and uses namespace `horizon::core`.
- Support exactly zero through sixteen tuple elements.
- Do not use inheritance, EBO, `[[no_unique_address]]`, or nested suffix storage for value elements.
- Preserve the dependency direction: Core must not depend on Math, AST, or DSL.
- Verify using the CLion CMake/Ninja configuration in `cmake-build-debug-clion`.

---

### Task 1: Add the layout and interface regression test

**Files:**
- Create: `tests/core/CMakeLists.txt`
- Create: `tests/core/test_tuple.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: the planned `core/tuple.h` public API.
- Produces: `horizon-test-core-tuple`, registered as `horizon.core.tuple`.

- [ ] **Step 1: Write the failing test**

```cpp
#include "core/tuple.h"

struct Mirror { char a; int b; double c; };
using Value = horizon::core::tuple<char, int, double>;
static_assert(sizeof(Value) == sizeof(Mirror));
static_assert(alignof(Value) == alignof(Mirror));
```

- [ ] **Step 2: Run test to verify it fails**

Run the CLion Ninja target `horizon-test-core-tuple`.
Expected: compilation fails because `core/tuple.h` does not exist.

- [ ] **Step 3: Add behavioral assertions**

Assert `get`, structured binding, `make_tuple`, `tie`, `apply`, `tuple_cat`,
and `get<15>` on a sixteen-element tuple.

### Task 2: Implement the Core tuple header

**Files:**
- Create: `src/core/tuple.h`

**Interfaces:**
- Produces: `tuple<T...>`, `get`, `tuple_size`, `tuple_element`, `make_tuple`,
  `tie`, `forward_as_tuple`, `apply`, `swap`, and `tuple_cat`.

- [ ] **Step 1: Implement direct-member arity specializations**

Declare specializations for zero through sixteen type arguments. Each value
specialization declares its members directly in parameter order and has no base
classes.

- [ ] **Step 2: Implement tuple protocol and utilities**

Provide indexed and type-based access, Core and standard tuple traits for
structured bindings, and utility functions that compose through `get`.

- [ ] **Step 3: Run the Core tuple target**

Run the CLion Ninja target `horizon-test-core-tuple`.
Expected: it compiles and exits successfully.

### Task 3: Replace the Core alias and verify integration

**Files:**
- Modify: `src/core/stl.h`

**Interfaces:**
- Consumes: `src/core/tuple.h`.
- Produces: the existing `horizon::core::tuple` API backed by Core storage.

- [ ] **Step 1: Include the new header and remove the std tuple alias/shims**

Replace the tuple section with the new Core implementation while retaining
existing `traverse_tuple` behavior through Core `get` and traits.

- [ ] **Step 2: Build affected module and test targets**

Build `horizon-core`, `horizon-math`, `horizon-ast`, `horizon-dsl`,
`horizon-test-core-tuple`, `horizon-test-math-swizzle`, and `horizon-test-ast`.

- [ ] **Step 3: Run the registered test suite and patch check**

Run CTest in `cmake-build-debug-clion` and `git diff --check`.
