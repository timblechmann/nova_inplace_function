// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

// Negative test: narrowing cross-capacity conversions must fail to compile
// (requires-clause on the converting constructor). Built EXCLUDE_FROM_ALL;
// ctest runs the build itself and expects failure (WILL_FAIL).

#include <nova/inplace_function.hpp>

using small = nova::inplace_function< int( int ), 16 >;
using large = nova::inplace_function< int( int ), 64 >;

int main()
{
    large l( []( int x ) {
        return x + 1;
    } );
    small s( l );
    return s( 1 );
}
