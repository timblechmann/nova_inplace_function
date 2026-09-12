// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

// Negative test: opting in to target introspection without RTTI must fail to
// compile (static_assert in vtable_impl). Built EXCLUDE_FROM_ALL; ctest runs
// the build itself and expects failure (WILL_FAIL).

#include <nova/inplace_function.hpp>

using rejected = nova::inplace_function< void(), 24, alignof( std::max_align_t ), nova::EnableTargetType::enabled >;

int main()
{
    rejected f;
    return static_cast< bool >( f ) ? 1 : 0;
}
