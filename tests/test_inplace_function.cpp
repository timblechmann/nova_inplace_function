// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/inplace_function.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace {

int free_add( int a, int b )
{
    return a + b;
}

int free_noexcept_add( int a, int b ) noexcept
{
    return a + b;
}

struct functor
{
    int operator()( int x ) const
    {
        return x * 2;
    }
};

struct move_only_functor
{
    std::unique_ptr< int > value;

    explicit move_only_functor( int v ) :
        value( std::make_unique< int >( v ) )
    {}

    move_only_functor( const move_only_functor& )            = delete;
    move_only_functor& operator=( const move_only_functor& ) = delete;
    move_only_functor( move_only_functor&& )                 = default;
    move_only_functor& operator=( move_only_functor&& )      = default;

    int operator()( int x ) const
    {
        return x + *value;
    }
};

struct throwing_move_functor
{
    throwing_move_functor()                               = default;
    throwing_move_functor( const throwing_move_functor& ) = default;
    throwing_move_functor( throwing_move_functor&& ) noexcept( false )
    {}

    int operator()( int x ) const
    {
        return x;
    }
};

struct large_functor
{
    char padding[ 64 ];

    large_functor()
    {
        padding[ 0 ] = 1;
    }

    int operator()( int x ) const
    {
        return x + padding[ 0 ];
    }
};

template < class F >
concept has_target_type = requires( const F& f ) { f.target_type(); };

template < class F, class T >
concept has_target = requires( F& f ) { f.template target< T >(); };

} // namespace

TEST_CASE( "inplace_function: default capacity is three pointers", "[inplace_function]" )
{
    static_assert( nova::inplace_function< void() >::capacity::value == 3 * sizeof( void* ) );
    static_assert( nova::inplace_function< void() >::alignment::value == alignof( std::max_align_t ) );
    CHECK( true );
}

TEST_CASE( "inplace_function: empty state", "[inplace_function]" )
{
    nova::inplace_function< int( int ) > f;
    CHECK( !f );
    CHECK( f == nullptr );
    CHECK( !( f != nullptr ) );
    CHECK_THROWS_AS( f( 1 ), std::bad_function_call );

    nova::inplace_function< int( int ) > g( nullptr );
    CHECK( !g );
    CHECK_THROWS_AS( g( 1 ), std::bad_function_call );
}

TEST_CASE( "inplace_function: construction from callables", "[inplace_function]" )
{
    SECTION( "free function" )
    {
        nova::inplace_function< int( int, int ) > f( free_add );
        CHECK( f );
        CHECK( f( 2, 3 ) == 5 );
    }

    SECTION( "stateless lambda" )
    {
        nova::inplace_function< int( int ) > f( []( int x ) {
            return x + 1;
        } );
        CHECK( f( 41 ) == 42 );
    }

    SECTION( "capturing lambda" )
    {
        int                                  offset = 10;
        nova::inplace_function< int( int ) > f( [ offset ]( int x ) {
            return x + offset;
        } );
        CHECK( f( 32 ) == 42 );
    }

    SECTION( "functor" )
    {
        nova::inplace_function< int( int ) > f( functor {} );
        CHECK( f( 21 ) == 42 );
    }

    SECTION( "function pointer deduction guide" )
    {
        nova::inplace_function f( &free_add );
        static_assert( std::is_same_v< decltype( f )::signature, int( int, int ) > );
        CHECK( f( 2, 3 ) == 5 );
    }

    SECTION( "void signature" )
    {
        bool                             called = false;
        nova::inplace_function< void() > f( [ & ] {
            called = true;
        } );
        f();
        CHECK( called );
    }

    SECTION( "reference return" )
    {
        int                              value = 7;
        nova::inplace_function< int&() > f( [ & ]() -> int& {
            return value;
        } );
        CHECK( &f() == &value );
    }
}

TEST_CASE( "inplace_function: copy semantics", "[inplace_function]" )
{
    static_assert( std::is_copy_constructible_v< nova::inplace_function< void() > > );
    static_assert( std::is_copy_assignable_v< nova::inplace_function< void() > > );

    auto                             counter = std::make_shared< int >( 0 );
    nova::inplace_function< void() > f( [ counter ] {
        ++( *counter );
    } );

    SECTION( "copy construction shares behaviour" )
    {
        nova::inplace_function< void() > g( f );
        CHECK( f );
        CHECK( g );
        f();
        g();
        CHECK( *counter == 2 );
    }

    SECTION( "copy assignment" )
    {
        nova::inplace_function< void() > g;
        g = f;
        CHECK( g );
        g();
        CHECK( *counter == 1 );
    }

    SECTION( "self copy assignment" )
    {
#if defined( __clang__ )
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wself-assign-overloaded"
#endif
        f = f;
#if defined( __clang__ )
#    pragma clang diagnostic pop
#endif
        CHECK( f );
        f();
        CHECK( *counter == 1 );
    }
}

TEST_CASE( "inplace_function: move semantics", "[inplace_function]" )
{
    static_assert( std::is_move_constructible_v< nova::inplace_function< void() > > );
    static_assert( std::is_move_assignable_v< nova::inplace_function< void() > > );

    SECTION( "move construction empties source" )
    {
        nova::inplace_function< int( int ) > f( []( int x ) {
            return x * 2;
        } );
        nova::inplace_function< int( int ) > g( std::move( f ) );
        CHECK( !f );
        CHECK( g );
        CHECK( g( 21 ) == 42 );
        CHECK_THROWS_AS( f( 1 ), std::bad_function_call );
    }

    SECTION( "move assignment empties source" )
    {
        nova::inplace_function< int( int ) > f( []( int x ) {
            return x + 1;
        } );
        nova::inplace_function< int( int ) > g;
        g = std::move( f );
        CHECK( !f );
        CHECK( g( 41 ) == 42 );
    }

    SECTION( "self move assignment" )
    {
        nova::inplace_function< int( int ) > f( []( int x ) {
            return x + 1;
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
        CHECK( f( 41 ) == 42 );
    }
}

TEST_CASE( "inplace_function: nullptr assignment", "[inplace_function]" )
{
    nova::inplace_function< int( int ) > f( []( int x ) {
        return x;
    } );
    f = nullptr;
    CHECK( !f );
    CHECK_THROWS_AS( f( 1 ), std::bad_function_call );
}

TEST_CASE( "inplace_function: assignment from callable", "[inplace_function]" )
{
    nova::inplace_function< int( int ) > f;
    f = nova::inplace_function< int( int ) >( []( int x ) {
        return x * 3;
    } );
    CHECK( f( 14 ) == 42 );
}

TEST_CASE( "inplace_function: swap", "[inplace_function]" )
{
    nova::inplace_function< int() > f( [] {
        return 1;
    } );
    nova::inplace_function< int() > g( [] {
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
        nova::inplace_function< int() > h;
        f.swap( h );
        CHECK( !f );
        CHECK( h() == 1 );
        f.swap( h );
        CHECK( f() == 1 );
        CHECK( !h );
    }

    SECTION( "self swap" )
    {
        f.swap( f );
        CHECK( f() == 1 );
    }
}

TEST_CASE( "inplace_function: cross-capacity conversions", "[inplace_function]" )
{
    using small = nova::inplace_function< int( int ), 16 >;
    using large = nova::inplace_function< int( int ), 64 >;

    small s( []( int x ) {
        return x + 1;
    } );

    SECTION( "copy small into large" )
    {
        large l( s );
        CHECK( l( 41 ) == 42 );
        CHECK( s( 41 ) == 42 );
    }

    SECTION( "move small into large" )
    {
        large l( std::move( s ) );
        CHECK( !s );
        CHECK( l( 41 ) == 42 );
    }

    SECTION( "wrong direction is not constructible (requires-clause, no hard error)" )
    {
        static_assert( !std::is_constructible_v< small, const large& > );
        static_assert( !std::is_constructible_v< small, large&& > );
        CHECK( true );
    }
}

TEST_CASE( "inplace_function: custom capacity", "[inplace_function]" )
{
    nova::inplace_function< int( int ), 64 > f( []( int x ) {
        return x * 2;
    } );
    CHECK( f( 21 ) == 42 );

    // oversized closures are rejected with a static_assert (as in SG14);
    // a roomy buffer accepts the same functor
    static_assert( std::is_constructible_v< nova::inplace_function< int( int ), 128 >, large_functor > );
}

TEST_CASE( "inplace_function: move-only closures are rejected", "[inplace_function]" )
{
    static_assert( !std::is_constructible_v< nova::inplace_function< int( int ) >, move_only_functor > );
    CHECK( true );
}

TEST_CASE( "inplace_function: throwing-move closures are rejected", "[inplace_function]" )
{
    // relocation is noexcept, so only nothrow-move-constructible closures qualify
    static_assert( !std::is_constructible_v< nova::inplace_function< int( int ) >, throwing_move_functor > );
    static_assert( !std::is_constructible_v< nova::move_only_inplace_function< int( int ) >, throwing_move_functor > );
    // control: copyable nothrow-movable closures are accepted by both
    static_assert( std::is_constructible_v< nova::inplace_function< int( int ) >, functor > );
    static_assert( std::is_constructible_v< nova::move_only_inplace_function< int( int ) >, functor > );
    CHECK( true );
}

TEST_CASE( "inplace_function: const-qualified signature", "[inplace_function]" )
{
    SECTION( "const callable works" )
    {
        nova::inplace_function< int( int ) const > f( []( int x ) {
            return x * 2;
        } );
        CHECK( f( 21 ) == 42 );

        const auto& cref = f;
        CHECK( cref( 21 ) == 42 );
    }

    SECTION( "mutable lambda rejected for const signature" )
    {
        auto mutable_lambda = [ count = 0 ]( int x ) mutable {
            return x + count++;
        };
        static_assert(
            !std::is_constructible_v< nova::inplace_function< int( int ) const >, decltype( mutable_lambda ) > );
        static_assert( std::is_constructible_v< nova::inplace_function< int( int ) >, decltype( mutable_lambda ) > );
        CHECK( true );
    }
}

TEST_CASE( "inplace_function: noexcept signature", "[inplace_function]" )
{
    nova::inplace_function< int( int, int ) noexcept > f( free_noexcept_add );
    static_assert( noexcept( f( 1, 2 ) ) );
    CHECK( f( 2, 3 ) == 5 );

    SECTION( "deduction guide preserves noexcept" )
    {
        nova::inplace_function g( &free_noexcept_add );
        static_assert( std::is_same_v< decltype( g )::signature, int( int, int ) noexcept > );
        static_assert( noexcept( g( 1, 2 ) ) );
        CHECK( g( 2, 3 ) == 5 );
    }

    SECTION( "empty noexcept call terminates (death by terminate)" )
    {
        // calling an empty noexcept wrapper cannot throw: it terminates.
        // only check the type-level property here.
        nova::inplace_function< void() noexcept > h;
        static_assert( noexcept( h() ) );
        CHECK( !h );
    }
}

TEST_CASE( "inplace_function: ref-qualified signatures", "[inplace_function]" )
{
    SECTION( "lvalue-qualified call" )
    {
        nova::inplace_function< int() & > f( [] {
            return 42;
        } );
        CHECK( f() == 42 );
    }

    SECTION( "rvalue-qualified call" )
    {
        nova::inplace_function< int() && > f( [] {
            return 42;
        } );
        CHECK( std::move( f )() == 42 );
    }

    SECTION( "const lvalue-qualified call" )
    {
        nova::inplace_function< int() const& > f( [] {
            return 42;
        } );
        const auto&                            cref = f;
        CHECK( cref() == 42 );
    }
}

TEST_CASE( "inplace_function: std::string payload", "[inplace_function]" )
{
    // a small closure returning a large object still works (only the closure is inline)
    nova::inplace_function< std::string() > f( [] {
        return std::string( "hello" );
    } );
    CHECK( f() == "hello" );

    nova::inplace_function< std::string() > g( f );
    CHECK( g() == "hello" );

    nova::inplace_function< std::string() > h( std::move( f ) );
    CHECK( h() == "hello" );
    CHECK( !f );
}

TEST_CASE( "inplace_function: object layout packs storage first", "[inplace_function]" )
{
    // Explicit Alignment=16 (> sizeof(void*)) discriminates member order:
    // storage-first gives 24 + 8 = 32, vtable-first would need 8 + 8 pad + 24
    // rounded up to 48. (With default alignment on platforms where
    // alignof(max_align_t) == sizeof(void*), both orders give the same size.)
    using tight = nova::inplace_function< void(), 24, 16 >;
    static_assert( sizeof( tight ) == 32 );
    static_assert( alignof( tight ) == 16 );
    CHECK( sizeof( tight ) == 32 );
}

TEST_CASE( "inplace_function: vtable pays only for enabled features", "[inplace_function]" )
{
    using nova::EnableTargetType;
    using nova::inplace_function_detail::copy_ptr_storage;
    using nova::inplace_function_detail::typeid_ptr_storage;
    using nova::inplace_function_detail::vtable_impl;

    static_assert( std::is_empty_v< copy_ptr_storage< false > > );
    static_assert( !std::is_empty_v< copy_ptr_storage< true > > );
    static_assert( std::is_empty_v< typeid_ptr_storage< EnableTargetType::disabled > > );
    static_assert( !std::is_empty_v< typeid_ptr_storage< EnableTargetType::enabled > > );

    // invoke + relocate + destroy = 3 pointers; copy and typeid add one each
    static_assert( sizeof( vtable_impl< void, false, false, true, EnableTargetType::enabled > ) == 5 * sizeof( void* ) );
    static_assert( sizeof( vtable_impl< void, false, false, false, EnableTargetType::enabled > ) == 4 * sizeof( void* ) );
    static_assert( sizeof( vtable_impl< void, false, false, true, EnableTargetType::disabled > ) == 4 * sizeof( void* ) );
    static_assert( sizeof( vtable_impl< void, false, false, false, EnableTargetType::disabled > )
                   == 3 * sizeof( void* ) );
    CHECK( true );
}

TEST_CASE( "inplace_function: target introspection is opt-in", "[inplace_function]" )
{
    using plain = nova::inplace_function< int() >;
    static_assert( !has_target_type< plain > );
    static_assert( !has_target< plain, functor > );
    CHECK( true );
}

TEST_CASE( "inplace_function: target introspection when enabled", "[inplace_function]" )
{
    using rtti_fn
        = nova::inplace_function< int( int ), 3 * sizeof( void* ), alignof( std::max_align_t ), nova::EnableTargetType::enabled >;

    auto lam = []( int x ) {
        return x * 2;
    };
    rtti_fn f( lam );
    CHECK( f.target_type() == typeid( decltype( lam ) ) );
    REQUIRE( f.target< decltype( lam ) >() != nullptr );
    CHECK( ( *f.target< decltype( lam ) >() )( 21 ) == 42 );
    CHECK( f.target< functor >() == nullptr );

    SECTION( "copy preserves target" )
    {
        rtti_fn g( f );
        CHECK( g.target_type() == typeid( decltype( lam ) ) );
        CHECK( g.target< decltype( lam ) >() != nullptr );
    }

    SECTION( "const access" )
    {
        const auto& cref = f;
        CHECK( cref.target_type() == typeid( decltype( lam ) ) );
        CHECK( cref.target< decltype( lam ) >() != nullptr );
    }

    SECTION( "empty wrapper reports void" )
    {
        rtti_fn g;
        CHECK( g.target_type() == typeid( void ) );
        CHECK( g.target< decltype( lam ) >() == nullptr );
    }
}

TEST_CASE( "inplace_function: cross-capacity conversion preserves target", "[inplace_function]" )
{
    using small_rtti
        = nova::inplace_function< int( int ), 16, alignof( std::max_align_t ), nova::EnableTargetType::enabled >;
    using large_rtti
        = nova::inplace_function< int( int ), 64, alignof( std::max_align_t ), nova::EnableTargetType::enabled >;

    auto lam = []( int x ) {
        return x + 1;
    };
    small_rtti s( lam );

    SECTION( "copy preserves target" )
    {
        large_rtti l( s );
        CHECK( l( 41 ) == 42 );
        CHECK( l.target_type() == typeid( decltype( lam ) ) );
        CHECK( l.target< decltype( lam ) >() != nullptr );
    }

    SECTION( "move preserves target and empties source" )
    {
        large_rtti l( std::move( s ) );
        CHECK( !s );
        CHECK( l( 41 ) == 42 );
        CHECK( l.target_type() == typeid( decltype( lam ) ) );
        CHECK( l.target< decltype( lam ) >() != nullptr );
    }
}
