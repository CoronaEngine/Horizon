# Core Layout Tuple Design

## Goal

Provide `horizon::core::tuple` as a Core-owned tuple type whose value-element
specializations have the same MSVC x64 object layout as a struct declaring the
same non-reference members in the same order.

## Design

`src/core/tuple.h` will contain generated explicit specializations for arities
zero through sixteen. Each non-empty specialization contains only direct data
members, `value0_` through `valueN_`, in declaration order. It intentionally
does not use base classes, empty-base optimization, `[[no_unique_address]]`, or
nested suffix storage, because each can change member offsets.

The public interface provides construction, assignment, comparisons, indexed
and type-based `get`, `tuple_size`, `tuple_element`, structured-binding
support, `make_tuple`, `tie`, `forward_as_tuple`, `apply`, `swap`, and
`tuple_cat` for Core tuples. The physical-layout guarantee applies only to
non-reference object elements; reference elements remain supported for normal
tuple access but have no layout-equivalence guarantee.

`src/core/stl.h` will include the new header and stop aliasing
`horizon::core::tuple` to `std::tuple`. Existing Core reflection keeps using
the Core tuple as a type list. No module dependency changes are introduced.

## Verification

`tests/core/test_tuple.cpp` will compare Core tuple size, alignment, and real
member byte offsets against mirror structs with deliberately mixed alignments.
It will also exercise construction, `get`, type-based access, structured
bindings, utility functions, and the supported arity limit.
