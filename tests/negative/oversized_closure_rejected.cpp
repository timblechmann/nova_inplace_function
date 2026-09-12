// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

// Negative test: oversized closures must fail to compile (static_assert on
// sizeof). Built EXCLUDE_FROM_ALL; ctest runs the build itself and expects
// failure (WILL_FAIL).

#include <nova/inplace_function.hpp>

namespace {

struct large_closure
{
    char padding[ 64 ];

    int operator()( int x ) const
    {
        return x + padding[ 0 ];
    }
};

} // namespace

using rejected = nova::inplace_function< int( int ), 16 >;

int main()
{
    rejected f( large_closure {} );
    return f( 1 );
}
