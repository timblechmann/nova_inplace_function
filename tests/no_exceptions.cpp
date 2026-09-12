// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

// Freestanding smoke test: must compile and run with C++ exceptions disabled.
// NOVA_INPLACE_FUNCTION_THROW is provided by the build (abort-based).
// Standalone (no Catch2: it needs exceptions), non-throwing paths only,
// works in any build type.

// <cstdlib> first: the THROW macro below expands to std::abort().
#include <cstdlib>

// Configured by CMake (see CMakeLists.txt): defines NOVA_INPLACE_FUNCTION_THROW.
#include "no_exceptions_config.h"

#include <nova/inplace_function.hpp>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace {

int failures = 0;

#define CHECK_EQ( a, b )      \
    do {                      \
        if ( ( a ) != ( b ) ) \
            ++failures;       \
    } while ( 0 )

int add_one( int x )
{
    return x + 1;
}

} // namespace

int main()
{
    nova::inplace_function< int( int ) > f( add_one );
    CHECK_EQ( f( 41 ), 42 );
    CHECK_EQ( static_cast< bool >( f ), true );

    nova::inplace_function< int( int ) > g( f );
    CHECK_EQ( g( 41 ), 42 );

    nova::inplace_function< int( int ) > h( std::move( f ) );
    CHECK_EQ( static_cast< bool >( f ), false );
    CHECK_EQ( h( 41 ), 42 );

    g = h;
    CHECK_EQ( g( 41 ), 42 );

    // empty state is observable without throwing (calling it would abort)
    nova::inplace_function< int( int ) > e;
    CHECK_EQ( static_cast< bool >( e ), false );
    CHECK_EQ( e == nullptr, true );

    h = nullptr;
    CHECK_EQ( static_cast< bool >( h ), false );

    f.swap( g );
    CHECK_EQ( f( 41 ), 42 );

    nova::move_only_inplace_function< int( int ) > m( []( int x ) {
        return x * 2;
    } );
    CHECK_EQ( m( 21 ), 42 );

    nova::move_only_inplace_function< int( int ) > n( std::move( m ) );
    CHECK_EQ( static_cast< bool >( m ), false );
    CHECK_EQ( n( 21 ), 42 );

    return failures;
}
