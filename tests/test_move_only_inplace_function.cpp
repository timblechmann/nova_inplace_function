// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/inplace_function.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace {

int free_value()
{
    return 42;
}

template < class F >
concept has_target_type = requires( const F& f ) { f.target_type(); };

} // namespace

TEST_CASE( "move_only_inplace_function: default capacity is three pointers", "[move_only]" )
{
    static_assert( nova::move_only_inplace_function< void() >::capacity::value == 3 * sizeof( void* ) );
    CHECK( true );
}

TEST_CASE( "move_only_inplace_function: empty state", "[move_only]" )
{
    nova::move_only_inplace_function< int() > f;
    CHECK( !f );
    CHECK( f == nullptr );
    CHECK_THROWS_AS( f(), std::bad_function_call );

    nova::move_only_inplace_function< int() > g( nullptr );
    CHECK( !g );
}

TEST_CASE( "move_only_inplace_function: not copyable", "[move_only]" )
{
    using fn = nova::move_only_inplace_function< void() >;
    static_assert( !std::is_copy_constructible_v< fn > );
    static_assert( !std::is_copy_assignable_v< fn > );
    static_assert( std::is_move_constructible_v< fn > );
    static_assert( std::is_move_assignable_v< fn > );
    CHECK( true );
}

TEST_CASE( "move_only_inplace_function: copyable closures still work", "[move_only]" )
{
    nova::move_only_inplace_function< int( int ) > f( []( int x ) {
        return x * 2;
    } );
    CHECK( f( 21 ) == 42 );

    nova::move_only_inplace_function< int() > g( &free_value );
    CHECK( g() == 42 );
}

TEST_CASE( "move_only_inplace_function: move-only closures", "[move_only]" )
{
    SECTION( "lambda capturing unique_ptr" )
    {
        auto                                           ptr = std::make_unique< int >( 40 );
        nova::move_only_inplace_function< int( int ) > f( [ p = std::move( ptr ) ]( int x ) {
            return x + *p;
        } );
        CHECK( f );
        CHECK( f( 2 ) == 42 );
    }

    SECTION( "move-only functor" )
    {
        struct move_only
        {
            std::unique_ptr< int > value = std::make_unique< int >( 21 );

            move_only()                                  = default;
            move_only( const move_only& )                = delete;
            move_only& operator=( const move_only& )     = delete;
            move_only( move_only&& ) noexcept            = default;
            move_only& operator=( move_only&& ) noexcept = default;

            int operator()() const
            {
                return *value * 2;
            }
        };

        static_assert( std::is_constructible_v< nova::move_only_inplace_function< int() >, move_only > );
        static_assert( !std::is_constructible_v< nova::inplace_function< int() >, move_only > );

        nova::move_only_inplace_function< int() > f( move_only {} );
        CHECK( f() == 42 );
    }
}

TEST_CASE( "move_only_inplace_function: move semantics", "[move_only]" )
{
    SECTION( "move construction empties source" )
    {
        nova::move_only_inplace_function< int() > f( [] {
            return 42;
        } );
        nova::move_only_inplace_function< int() > g( std::move( f ) );
        CHECK( !f );
        CHECK( g() == 42 );
        CHECK_THROWS_AS( f(), std::bad_function_call );
    }

    SECTION( "move assignment empties source" )
    {
        nova::move_only_inplace_function< int() > f( [] {
            return 1;
        } );
        nova::move_only_inplace_function< int() > g( [] {
            return 2;
        } );
        g = std::move( f );
        CHECK( !f );
        CHECK( g() == 1 );
    }

    SECTION( "move assignment into empty" )
    {
        nova::move_only_inplace_function< int() > f( [] {
            return 42;
        } );
        nova::move_only_inplace_function< int() > g;
        g = std::move( f );
        CHECK( g() == 42 );
        CHECK( !f );
    }

    SECTION( "self move assignment" )
    {
        nova::move_only_inplace_function< int() > f( [] {
            return 42;
        } );
#if defined( __clang__ )
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wself-move"
#endif
        f = std::move( f );
#if defined( __clang__ )
#    pragma clang diagnostic pop
#endif
        CHECK( f );
        CHECK( f() == 42 );
    }

    SECTION( "move transfers ownership of move-only state" )
    {
        auto                                      ptr = std::make_unique< int >( 21 );
        nova::move_only_inplace_function< int() > f( [ p = std::move( ptr ) ] {
            return *p * 2;
        } );
        nova::move_only_inplace_function< int() > g( std::move( f ) );
        CHECK( !f );
        CHECK( g() == 42 );
    }
}

TEST_CASE( "move_only_inplace_function: nullptr assignment", "[move_only]" )
{
    nova::move_only_inplace_function< int() > f( [] {
        return 42;
    } );
    f = nullptr;
    CHECK( !f );
    CHECK_THROWS_AS( f(), std::bad_function_call );
}

TEST_CASE( "move_only_inplace_function: swap", "[move_only]" )
{
    nova::move_only_inplace_function< int() > f( [] {
        return 1;
    } );
    nova::move_only_inplace_function< int() > g( [] {
        return 2;
    } );

    f.swap( g );
    CHECK( f() == 2 );
    CHECK( g() == 1 );

    swap( f, g );
    CHECK( f() == 1 );
    CHECK( g() == 2 );

    SECTION( "swap with empty" )
    {
        nova::move_only_inplace_function< int() > h;
        f.swap( h );
        CHECK( !f );
        CHECK( h() == 1 );
    }
}

TEST_CASE( "move_only_inplace_function: cross-capacity move", "[move_only]" )
{
    using small = nova::move_only_inplace_function< int(), 16 >;
    using large = nova::move_only_inplace_function< int(), 64 >;

    small s( [] {
        return 42;
    } );
    large l( std::move( s ) );
    CHECK( !s );
    CHECK( l() == 42 );
}

TEST_CASE( "move_only_inplace_function: qualified signatures", "[move_only]" )
{
    SECTION( "const-qualified" )
    {
        nova::move_only_inplace_function< int() const > f( [] {
            return 42;
        } );
        CHECK( f() == 42 );
        const auto& cref = f;
        CHECK( cref() == 42 );
    }

    SECTION( "noexcept" )
    {
        nova::move_only_inplace_function< int() noexcept > f( []() noexcept {
            return 42;
        } );
        static_assert( noexcept( f() ) );
        CHECK( f() == 42 );
    }

    SECTION( "rvalue-qualified with move-only state" )
    {
        auto                                         ptr = std::make_unique< int >( 42 );
        nova::move_only_inplace_function< int() && > f( [ p = std::move( ptr ) ] {
            return *p;
        } );
        CHECK( std::move( f )() == 42 );
    }
}

TEST_CASE( "move_only_inplace_function: deduction guide", "[move_only]" )
{
    nova::move_only_inplace_function f( &free_value );
    static_assert( std::is_same_v< decltype( f )::signature, int() > );
    CHECK( f() == 42 );
}

TEST_CASE( "move_only_inplace_function: target introspection is opt-in", "[move_only]" )
{
    using plain = nova::move_only_inplace_function< int() >;
    static_assert( !has_target_type< plain > );
    CHECK( true );

    using rtti_fn = nova::move_only_inplace_function< int(),
                                                      3 * sizeof( void* ),
                                                      alignof( std::max_align_t ),
                                                      nova::EnableTargetType::enabled >;
    static_assert( has_target_type< rtti_fn > );

    auto lam = [] {
        return 42;
    };
    rtti_fn f( std::move( lam ) );
    CHECK( f.target_type() == typeid( decltype( lam ) ) );
    CHECK( f.target< decltype( lam ) >() != nullptr );
    CHECK( f() == 42 );
}
