// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

// Freestanding smoke test: must compile and run with RTTI disabled
// (-fno-rtti / /GR-). Standalone (no Catch2: its headers need RTTI),
// non-throwing paths only, works in any build type.

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
    using nova::EnableTargetType;
    using nova::inplace_function_detail::vtable_impl;

    static_assert( NOVA_HAS_RTTI == 0, "this TU must be built without RTTI" );

    // no typeid entry anywhere without the opt-in
    static_assert( sizeof( vtable_impl< void, false, false, true, EnableTargetType::disabled > ) == 4 * sizeof( void* ) );
    static_assert( sizeof( vtable_impl< void, false, false, false, EnableTargetType::disabled > )
                   == 3 * sizeof( void* ) );

    using tight = nova::inplace_function< void(), 24, 16 >;
    static_assert( sizeof( tight ) == 32 );

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
