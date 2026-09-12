# nova::inplace_function

[![CI](https://github.com/timblechmann/nova_inplace_function/actions/workflows/ci.yml/badge.svg)](https://github.com/timblechmann/nova_inplace_function/actions/workflows/ci.yml)

Fixed-capacity, non-allocating function wrappers for C++20. Based on the
[SG14 `stdext::inplace_function`](https://github.com/WG21-SG14/SG14/blob/master/SG14/inplace_function.h)
proposal.

The closure is stored inline in a buffer of `Capacity` bytes (default: 3 pointers),
so it never allocates — suitable for embedded, real-time, and hot-path use.

## Components

| Type | Closure | Notes |
|------|---------|-------|
| `inplace_function<Sig>` | copy-constructible | Copyable and movable |
| `move_only_inplace_function<Sig>` | move-constructible | Move-only; holds e.g. lambdas capturing `unique_ptr` |

Both share one implementation (`inplace_function_detail::base`) and support qualified signatures
like `int(int) const`, `void() noexcept`, `int() &`, `int() &&`.

## Usage

```cpp
#include <nova/inplace_function.hpp>

// copyable wrapper, default capacity 3 * sizeof(void*)
nova::inplace_function< int( int ) > f = []( int x ) {
    return x * 2;
};
int result = f( 21 ); // 42 — no heap allocation

// move-only callable (lambda capturing a unique_ptr)
auto ptr = std::make_unique< int >( 40 );
nova::move_only_inplace_function< int( int ) > g( [p = std::move( ptr )]( int x ) {
    return x + *p;
} );
int result2 = g( 2 ); // 42

// custom capacity for larger closures
nova::inplace_function< void(), 64 > h( [] {
    /* ... */
} );

// qualified signatures constrain the call operator
nova::inplace_function< int() const& > c( [] {
    return 1;
} );
nova::inplace_function< void() noexcept > n( []() noexcept {
    /* ... */
} );

// opt-in RTTI introspection (off by default to keep the vtable minimal)
nova::inplace_function< int( int ), 24, alignof( std::max_align_t ), nova::EnableTargetType::enabled > t(
    []( int x ) {
        return x * 2;
    } );
t.target_type() == typeid( /* lambda */ ); // true
```

## API

| Member | Notes |
|--------|-------|
| `inplace_function()` / `(nullptr)` | Empty; calling throws `std::bad_function_call` |
| `inplace_function(closure)` | Requires invocable + fits in `Capacity`/`Alignment`; copyable closure for `inplace_function` |
| converting ctor from smaller `Capacity` | Same signature; `static_assert` on size/alignment mismatch |
| `operator()(args...)` | Qualifiers mirror the signature (`const`, `noexcept`, `&`/`&&`) |
| `operator bool()` / `== nullptr` | Empty check |
| `operator=(other)` / `= nullptr` | Copy-and-relocate assignment; move empties the source |
| `swap(other)` / ADL `swap` | Exchange; handles empty/non-empty mixes |
| `target_type()` / `target<T>()` | Only with `EnableTargetType::enabled`; `typeid(void)` when empty |
| `signature`, `capacity`, `alignment` | Member typedefs; `is_copyable` bool constant |


## Requirements

- C++20 (GCC 12+, Clang 17+, MSVC 2022+)
- Header-only; no dependencies

## Portability

- **RTTI**: the header compiles with RTTI disabled (`-fno-rtti` / `/GR-`,
  auto-detected per compiler; `-DNOVA_HAS_RTTI=0` forces it for toolchains
  the detection doesn't know). `EnableTargetType::enabled` without RTTI is a
  compile-time error; everything else keeps working.
- **Exceptions**: compiling with `-fno-exceptions` requires defining
  `NOVA_INPLACE_FUNCTION_THROW`, e.g. `-DNOVA_INPLACE_FUNCTION_THROW(x)=std::abort()`
  (direct compiler invocation only — CMake drops function-style `-D` defines,
  so CMake users should define the macro in a configured header; any
  dependency of the macro such as `<cstdlib>` must be included before this
  header). MSVC keeps the throwing default: its `_CPPUNWIND` is absent even
  in default builds where `throw` still compiles.

## Build & test

```sh
cmake -B build
cmake --build build
ctest --test-dir build
```

## License

MIT — see [LICENSE](LICENSE). As a non-binding request, please use this code responsibly and ethically.
