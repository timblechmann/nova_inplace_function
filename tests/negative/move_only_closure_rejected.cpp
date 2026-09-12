// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

// Negative test: move-only closures are rejected by the copyable wrapper
// (requires-clause, no viable constructor). Built EXCLUDE_FROM_ALL; ctest runs
// the build itself and expects failure (WILL_FAIL).

#include <nova/inplace_function.hpp>

#include <memory>
#include <utility>

using rejected = nova::inplace_function< int( int ) >;

int main()
{
    auto     ptr = std::make_unique< int >( 1 );
    rejected f( [ p = std::move( ptr ) ]( int x ) {
        return x + *p;
    } );
    return f( 1 );
}
