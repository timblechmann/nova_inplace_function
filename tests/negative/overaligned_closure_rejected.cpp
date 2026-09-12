// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

// Negative test: overaligned closures must fail to compile (static_assert on
// alignment). Built EXCLUDE_FROM_ALL; ctest runs the build itself and expects
// failure (WILL_FAIL).

#include <nova/inplace_function.hpp>

namespace {

struct alignas( 32 ) overaligned_closure
{
    int operator()( int x ) const
    {
        return x;
    }
};

} // namespace

using rejected = nova::inplace_function< int( int ), 32, 8 >;

int main()
{
    rejected f( overaligned_closure {} );
    return f( 1 );
}
