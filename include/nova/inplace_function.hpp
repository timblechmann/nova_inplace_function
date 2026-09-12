// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <type_traits>
#include <typeinfo>
#include <utility>

#if !defined( NOVA_INPLACE_FUNCTION_THROW )
#    if defined( __cpp_exceptions ) || defined( _MSC_VER )
#        define NOVA_INPLACE_FUNCTION_THROW( x ) throw( x )
#    else
// Compiling without C++ exceptions: define NOVA_INPLACE_FUNCTION_THROW explicitly,
// e.g. -DNOVA_INPLACE_FUNCTION_THROW(x)=std::abort().
// Note: _MSC_VER is exempt on purpose — _CPPUNWIND is absent in default MSVC
// builds, but throw still compiles there, so erroring would break status quo.
#        error \
            "nova/inplace_function.hpp: compiling without C++ exceptions requires defining NOVA_INPLACE_FUNCTION_THROW"
#    endif
#endif

#if !defined( NOVA_HAS_RTTI )
#    if defined( _MSC_VER )
#        if defined( _CPPRTTI )
#            define NOVA_HAS_RTTI 1
#        else
#            define NOVA_HAS_RTTI 0
#        endif
#    elif defined( __clang__ )
#        if __has_feature( cxx_rtti )
#            define NOVA_HAS_RTTI 1
#        else
#            define NOVA_HAS_RTTI 0
#        endif
#    elif defined( __GNUC__ )
#        if defined( __GXX_RTTI )
#            define NOVA_HAS_RTTI 1
#        else
#            define NOVA_HAS_RTTI 0
#        endif
#    else
// Unknown compiler: assume RTTI is available; define NOVA_HAS_RTTI=0 manually if not.
#        define NOVA_HAS_RTTI 1
#    endif
#endif

#if defined( _MSC_VER )
#    define NOVA_INPLACE_FUNCTION_EMPTY_BASES __declspec( empty_bases )
#else
#    define NOVA_INPLACE_FUNCTION_EMPTY_BASES
#endif

namespace nova {

/**
 * @brief Opt-in switch for RTTI-based target introspection
 *        (`target_type()`/`target<T>()`) on the inplace function wrappers.
 *
 * Disabled by default to keep the vtable minimal; pass
 * `EnableTargetType::enabled` as the last template argument to opt in.
 * Opting in requires RTTI (see `NOVA_HAS_RTTI` — unknown toolchains default
 * to available, define it manually if not).
 */
enum class EnableTargetType
{
    disabled,
    enabled,
};

namespace inplace_function_detail {

struct inplace_function_tag
{};

template < class T >
struct wrapper
{
    using type = T;
};

// Throws std::bad_function_call for empty-wrapper invocation.
//
// Separated from the (possibly noexcept) invoke trampoline so compilers do
// not warn about a throwing noexcept function. NOVA_INPLACE_FUNCTION_THROW
// must throw or terminate and never return normally.
[[noreturn]] inline void throw_bad_function_call()
{
    NOVA_INPLACE_FUNCTION_THROW( std::bad_function_call() );
}

enum class ref_qual
{
    none,
    lvalue,
    rvalue,
};

// EBO slot holding the vtable's copy entry.
//
// Empty (zero size via the empty-base optimization) when the wrapper is
// move-only, so no storage is wasted on a copy pointer that can never be used.
template < bool CopyableV >
struct copy_ptr_storage
{
    using process_ptr_t = void ( * )( void*, void* ) noexcept;
    process_ptr_t copy_ptr;
};

template <>
struct copy_ptr_storage< false >
{
#ifdef _MSC_VER
    constexpr copy_ptr_storage() noexcept = default;

    // MSVC emits a 1-byte store when copy-initializing an empty base from a prvalue
    constexpr copy_ptr_storage( const copy_ptr_storage& ) noexcept
    {}
#endif
};

// EBO slot holding the vtable's typeid entry.
//
// Empty (zero size via the empty-base optimization) unless RTTI-based
// target introspection is opted in via EnableTargetType::enabled.
template < EnableTargetType EnableTarget >
struct typeid_ptr_storage
{
    using typeid_ptr_t = const std::type_info& ( * )() noexcept;
    typeid_ptr_t typeid_ptr;
};

template <>
struct typeid_ptr_storage< EnableTargetType::disabled >
{
#ifdef _MSC_VER
    constexpr typeid_ptr_storage() noexcept = default;

    // MSVC emits a 1-byte store when copy-initializing an empty base from a prvalue
    constexpr typeid_ptr_storage( const typeid_ptr_storage& ) noexcept
    {}
#endif
};

template < class R, bool IsConst, bool IsNoexcept, bool CopyableV, EnableTargetType EnableTarget, class... Args >
struct NOVA_INPLACE_FUNCTION_EMPTY_BASES vtable_impl : copy_ptr_storage< CopyableV >, typeid_ptr_storage< EnableTarget >
{
    using storage_ptr_t    = void*;
    using invoke_ptr_t     = R ( * )( storage_ptr_t, Args&&... ) noexcept( IsNoexcept );
    using process_ptr_t    = void ( * )( storage_ptr_t, storage_ptr_t ) noexcept;
    using destructor_ptr_t = void ( * )( storage_ptr_t ) noexcept;

    invoke_ptr_t     invoke_ptr;
    process_ptr_t    relocate_ptr;
    destructor_ptr_t destructor_ptr;

    static_assert( EnableTarget == EnableTargetType::disabled || NOVA_HAS_RTTI == 1,
                   "nova::inplace_function_detail::vtable_impl: EnableTargetType::enabled requires RTTI" );

    explicit constexpr vtable_impl() noexcept :
        copy_ptr_storage< CopyableV > {
            make_empty_copy_slot(),
        },
        typeid_ptr_storage< EnableTarget > {
            make_empty_typeid_slot(),
        },
        invoke_ptr {
            +[]( storage_ptr_t, Args&&... ) noexcept( IsNoexcept ) -> R {
                throw_bad_function_call();
            },
        },
        relocate_ptr {
            +[]( storage_ptr_t, storage_ptr_t ) noexcept {},
        },
        destructor_ptr {
            +[]( storage_ptr_t ) noexcept {},
        }
    {}

    template < class C >
    explicit vtable_impl( wrapper< C > ) noexcept :
        copy_ptr_storage< CopyableV > {
            make_copy_slot< C >(),
        },
        typeid_ptr_storage< EnableTarget > {
            make_typeid_slot< C >(),
        },
        invoke_ptr {
            +[]( storage_ptr_t storage, Args&&... args ) noexcept( IsNoexcept ) -> R {
                if constexpr ( IsConst )
                    return ( *static_cast< const C* >( storage ) )( static_cast< Args&& >( args )... );
                else
                    return ( *static_cast< C* >( storage ) )( static_cast< Args&& >( args )... );
            },
        },
        relocate_ptr {
            +[]( storage_ptr_t dst, storage_ptr_t src ) noexcept {
                ::new ( dst ) C { std::move( *static_cast< C* >( src ) ) };
                static_cast< C* >( src )->~C();
            },
        },
        destructor_ptr {
            +[]( storage_ptr_t storage ) noexcept {
                static_cast< C* >( storage )->~C();
            },
        }
    {}

    vtable_impl( const vtable_impl& )            = delete;
    vtable_impl( vtable_impl&& )                 = delete;
    vtable_impl& operator=( const vtable_impl& ) = delete;
    vtable_impl& operator=( vtable_impl&& )      = delete;
    ~vtable_impl()                               = default;

private:
    static constexpr copy_ptr_storage< CopyableV > make_empty_copy_slot() noexcept
    {
        if constexpr ( CopyableV )
            return { +[]( void*, void* ) noexcept {} };
        else
            return {};
    }

    template < class C >
    static constexpr copy_ptr_storage< CopyableV > make_copy_slot() noexcept
    {
        if constexpr ( CopyableV && std::is_copy_constructible_v< C > )
            return { +[]( void* dst, void* src ) noexcept {
                ::new ( dst ) C { *static_cast< const C* >( src ) };
            } };
        else
            return {};
    }

    static constexpr typeid_ptr_storage< EnableTarget > make_empty_typeid_slot() noexcept
    {
#if NOVA_HAS_RTTI
        if constexpr ( EnableTarget == EnableTargetType::enabled )
            return { +[]() noexcept -> const std::type_info& {
                return typeid( void );
            } };
        else
            return {};
#else
        return {};
#endif
    }

    template < class C >
    static constexpr typeid_ptr_storage< EnableTarget > make_typeid_slot() noexcept
    {
#if NOVA_HAS_RTTI
        if constexpr ( EnableTarget == EnableTargetType::enabled )
            return { +[]() noexcept -> const std::type_info& {
                return typeid( C );
            } };
        else
            return {};
#else
        return {};
#endif
    }
};

template < class Sig >
struct sig_traits;

#define NOVA_INPLACE_FUNCTION_SIG_TRAIT( CV, REF, NOEXCEPT, IS_CONST, IS_NOEXCEPT, REF_VAL )           \
    template < class R, class... Args >                                                                \
    struct sig_traits< R( Args... ) CV REF NOEXCEPT >                                                  \
    {                                                                                                  \
        using return_type                        = R;                                                  \
        static constexpr bool        is_const    = IS_CONST;                                           \
        static constexpr bool        is_noexcept = IS_NOEXCEPT;                                        \
        static constexpr ref_qual    ref         = REF_VAL;                                            \
        static constexpr std::size_t arity       = sizeof...( Args );                                  \
        template < bool CopyableV, EnableTargetType EnableTarget >                                     \
        using vtable_type = vtable_impl< R, IS_CONST, IS_NOEXCEPT, CopyableV, EnableTarget, Args... >; \
        template < class C >                                                                           \
        static constexpr bool invocable                                                                \
            = std::is_invocable_r_v< R, std::conditional_t< IS_CONST, const C&, C& >, Args... >;       \
    };

NOVA_INPLACE_FUNCTION_SIG_TRAIT(, , , false, false, ref_qual::none )
NOVA_INPLACE_FUNCTION_SIG_TRAIT(, , noexcept, false, true, ref_qual::none )
NOVA_INPLACE_FUNCTION_SIG_TRAIT(, &, , false, false, ref_qual::lvalue )
NOVA_INPLACE_FUNCTION_SIG_TRAIT(, &, noexcept, false, true, ref_qual::lvalue )
NOVA_INPLACE_FUNCTION_SIG_TRAIT(, &&, , false, false, ref_qual::rvalue )
NOVA_INPLACE_FUNCTION_SIG_TRAIT(, &&, noexcept, false, true, ref_qual::rvalue )
NOVA_INPLACE_FUNCTION_SIG_TRAIT( const, , , true, false, ref_qual::none )
NOVA_INPLACE_FUNCTION_SIG_TRAIT( const, , noexcept, true, true, ref_qual::none )
NOVA_INPLACE_FUNCTION_SIG_TRAIT( const, &, , true, false, ref_qual::lvalue )
NOVA_INPLACE_FUNCTION_SIG_TRAIT( const, &, noexcept, true, true, ref_qual::lvalue )
NOVA_INPLACE_FUNCTION_SIG_TRAIT( const, &&, , true, false, ref_qual::rvalue )
NOVA_INPLACE_FUNCTION_SIG_TRAIT( const, &&, noexcept, true, true, ref_qual::rvalue )

#undef NOVA_INPLACE_FUNCTION_SIG_TRAIT

template < class Sig, bool CopyableV, EnableTargetType EnableTarget >
inline constexpr typename sig_traits< Sig >::template vtable_type< CopyableV, EnableTarget > empty_vtable {};

template < std::size_t DstCap, std::size_t DstAlign, std::size_t SrcCap, std::size_t SrcAlign >
struct is_valid_inplace_dst : std::bool_constant< ( DstCap >= SrcCap ) && ( DstAlign % SrcAlign == 0 ) >
{};

// Shared implementation for inplace_function (copyable) and
// move_only_inplace_function (move-only).
//
// CopyableV true: copy operations enabled, construction requires a
// copy-constructible closure. false: copy operations deleted,
// construction accepts any move-constructible closure.
template < bool CopyableV, typename Sig, std::size_t Capacity, std::size_t Alignment, EnableTargetType EnableTarget >
class base : inplace_function_tag
{
    using traits = sig_traits< Sig >;

    static constexpr bool     is_const    = traits::is_const;
    static constexpr bool     is_noexcept = traits::is_noexcept;
    static constexpr ref_qual ref         = traits::ref;

public:
    using signature = Sig;
    using capacity  = std::integral_constant< std::size_t, Capacity >;
    using alignment = std::integral_constant< std::size_t, Alignment >;

    static constexpr bool is_copyable = CopyableV;

    static_assert( std::is_function_v< Sig >, "nova::inplace_function_detail::base: Sig must be a function type" );
    static_assert( Capacity > 0, "nova::inplace_function_detail::base: Capacity must be greater than zero" );

    using vtable_type     = typename traits::template vtable_type< CopyableV, EnableTarget >;
    using vtable_ptr_type = const vtable_type*;

    template < bool, typename, std::size_t, std::size_t, EnableTargetType >
    friend class base;

    base() noexcept :
        vtable_ptr_ {
            std::addressof( empty_vtable< Sig, CopyableV, EnableTarget > ),
        }
    {}

    base( std::nullptr_t ) noexcept :
        base()
    {}

    template < typename F, typename C = std::decay_t< F > >
        requires( !std::is_base_of_v< inplace_function_tag, C > && traits::template invocable< C >
                  && std::is_nothrow_move_constructible_v< C > && std::is_constructible_v< C, F >
                  && (!CopyableV || std::is_copy_constructible_v< C >))
    base( F&& closure )
    {
        static_assert( sizeof( C ) <= Capacity,
                       "inplace_function cannot be constructed from object with this (large) size" );

        static_assert( Alignment % alignof( C ) == 0,
                       "inplace_function cannot be constructed from object with this (large) alignment" );

        static const vtable_type vt { wrapper< C > {} };
        vtable_ptr_ = std::addressof( vt );

        ::new ( std::addressof( storage_ ) ) C { std::forward< F >( closure ) };
    }

    template < std::size_t OtherCap, std::size_t OtherAlign >
        requires( CopyableV && is_valid_inplace_dst< Capacity, Alignment, OtherCap, OtherAlign >::value )
    base( const base< CopyableV, Sig, OtherCap, OtherAlign, EnableTarget >& other ) :
        vtable_ptr_ {
            other.vtable_ptr_,
        }
    {
        vtable_ptr_->copy_ptr( std::addressof( storage_ ), std::addressof( other.storage_ ) );
    }

    template < std::size_t OtherCap, std::size_t OtherAlign >
        requires( is_valid_inplace_dst< Capacity, Alignment, OtherCap, OtherAlign >::value )
    base( base< CopyableV, Sig, OtherCap, OtherAlign, EnableTarget >&& other ) noexcept :
        vtable_ptr_ {
            std::exchange( other.vtable_ptr_, std::addressof( empty_vtable< Sig, CopyableV, EnableTarget > ) ),
        }
    {
        vtable_ptr_->relocate_ptr( std::addressof( storage_ ), std::addressof( other.storage_ ) );
    }

    base( const base& other )
        requires( CopyableV )
        :
        vtable_ptr_ {
            other.vtable_ptr_,
        }
    {
        vtable_ptr_->copy_ptr( std::addressof( storage_ ), std::addressof( other.storage_ ) );
    }

    base( base&& other ) noexcept :
        vtable_ptr_ {
            std::exchange( other.vtable_ptr_, std::addressof( empty_vtable< Sig, CopyableV, EnableTarget > ) ),
        }
    {
        vtable_ptr_->relocate_ptr( std::addressof( storage_ ), std::addressof( other.storage_ ) );
    }

    base& operator=( const base& other )
        requires( CopyableV )
    {
        if ( this == std::addressof( other ) )
            return *this;
        base tmp( other );
        vtable_ptr_->destructor_ptr( std::addressof( storage_ ) );
        vtable_ptr_ = std::exchange( tmp.vtable_ptr_, std::addressof( empty_vtable< Sig, CopyableV, EnableTarget > ) );
        vtable_ptr_->relocate_ptr( std::addressof( storage_ ), std::addressof( tmp.storage_ ) );
        return *this;
    }

    base& operator=( base&& other ) noexcept
    {
        if ( this == std::addressof( other ) )
            return *this;
        vtable_ptr_->destructor_ptr( std::addressof( storage_ ) );
        vtable_ptr_ = std::exchange( other.vtable_ptr_, std::addressof( empty_vtable< Sig, CopyableV, EnableTarget > ) );
        vtable_ptr_->relocate_ptr( std::addressof( storage_ ), std::addressof( other.storage_ ) );
        return *this;
    }

    base& operator=( std::nullptr_t ) noexcept
    {
        vtable_ptr_->destructor_ptr( std::addressof( storage_ ) );
        vtable_ptr_ = std::addressof( empty_vtable< Sig, CopyableV, EnableTarget > );
        return *this;
    }

    ~base()
    {
        vtable_ptr_->destructor_ptr( std::addressof( storage_ ) );
    }

    template < typename... CallArgs >
        requires( !is_const && ( ref == ref_qual::none ) && ( sizeof...( CallArgs ) == traits::arity ) )
    typename traits::return_type operator()( CallArgs&&... args ) noexcept( is_noexcept )
    {
        return vtable_ptr_->invoke_ptr( std::addressof( storage_ ), std::forward< CallArgs >( args )... );
    }

    template < typename... CallArgs >
        requires( !is_const && ( ref == ref_qual::lvalue ) && ( sizeof...( CallArgs ) == traits::arity ) )
    typename traits::return_type operator()( CallArgs&&... args ) & noexcept( is_noexcept )
    {
        return vtable_ptr_->invoke_ptr( std::addressof( storage_ ), std::forward< CallArgs >( args )... );
    }

    template < typename... CallArgs >
        requires( !is_const && ( ref == ref_qual::rvalue ) && ( sizeof...( CallArgs ) == traits::arity ) )
    typename traits::return_type operator()( CallArgs&&... args ) && noexcept( is_noexcept )
    {
        return vtable_ptr_->invoke_ptr( std::addressof( storage_ ), std::forward< CallArgs >( args )... );
    }

    template < typename... CallArgs >
        requires( is_const && ( ref == ref_qual::none ) && ( sizeof...( CallArgs ) == traits::arity ) )
    typename traits::return_type operator()( CallArgs&&... args ) const noexcept( is_noexcept )
    {
        return vtable_ptr_->invoke_ptr( std::addressof( storage_ ), std::forward< CallArgs >( args )... );
    }

    template < typename... CallArgs >
        requires( is_const && ( ref == ref_qual::lvalue ) && ( sizeof...( CallArgs ) == traits::arity ) )
    typename traits::return_type operator()( CallArgs&&... args ) const& noexcept( is_noexcept )
    {
        return vtable_ptr_->invoke_ptr( std::addressof( storage_ ), std::forward< CallArgs >( args )... );
    }

    template < typename... CallArgs >
        requires( is_const && ( ref == ref_qual::rvalue ) && ( sizeof...( CallArgs ) == traits::arity ) )
    typename traits::return_type operator()( CallArgs&&... args ) const&& noexcept( is_noexcept )
    {
        return vtable_ptr_->invoke_ptr( std::addressof( storage_ ), std::forward< CallArgs >( args )... );
    }

    constexpr bool operator==( std::nullptr_t ) const noexcept
    {
        return !operator bool();
    }

    constexpr bool operator!=( std::nullptr_t ) const noexcept
    {
        return operator bool();
    }

    explicit constexpr operator bool() const noexcept
    {
        return vtable_ptr_ != std::addressof( empty_vtable< Sig, CopyableV, EnableTarget > );
    }

    const std::type_info& target_type() const noexcept
        requires( EnableTarget == EnableTargetType::enabled )
    {
        return vtable_ptr_->typeid_ptr();
    }

// target<T>() is parsed out without RTTI: compilers reject the typeid token
// even in dependent, never-instantiated bodies.
#if NOVA_HAS_RTTI
    template < class T >
    T* target() noexcept
        requires( EnableTarget == EnableTargetType::enabled )
    {
        return typeid( T ) == target_type() ? static_cast< T* >( static_cast< void* >( std::addressof( storage_ ) ) )
                                            : nullptr;
    }

    template < class T >
    const T* target() const noexcept
        requires( EnableTarget == EnableTargetType::enabled )
    {
        return typeid( T ) == target_type()
                   ? static_cast< const T* >( static_cast< const void* >( std::addressof( storage_ ) ) )
                   : nullptr;
    }
#endif

    void swap( base& other ) noexcept
    {
        if ( this == std::addressof( other ) )
            return;

        alignas( Alignment ) std::array< std::byte, Capacity > tmp;
        vtable_ptr_->relocate_ptr( std::addressof( tmp ), std::addressof( storage_ ) );

        other.vtable_ptr_->relocate_ptr( std::addressof( storage_ ), std::addressof( other.storage_ ) );

        vtable_ptr_->relocate_ptr( std::addressof( other.storage_ ), std::addressof( tmp ) );

        std::swap( vtable_ptr_, other.vtable_ptr_ );
    }

    friend void swap( base& lhs, base& rhs ) noexcept
    {
        lhs.swap( rhs );
    }

private:
    alignas( Alignment ) mutable std::array< std::byte, Capacity > storage_;
    vtable_ptr_type vtable_ptr_;
};

} // namespace inplace_function_detail

// =============================================================================
// inplace_function
// =============================================================================

/**
 * @brief Fixed-capacity, non-allocating, copyable function wrapper.
 *
 * Based on the SG14 `stdext::inplace_function` proposal. The closure is stored
 * inline in a buffer of `Capacity` bytes, so no heap allocation ever happens.
 * Copy construction/assignment require a copyable closure; move operations
 * relocate the closure and leave the source empty. Stored closures must be
 * nothrow-move-constructible; anything else is rejected at compile time.
 *
 * Calling an empty wrapper throws `std::bad_function_call` (customizable via
 * `NOVA_INPLACE_FUNCTION_THROW`). For `noexcept` signatures this terminates.
 *
 * @tparam Sig function signature, e.g. `int(double)`. `const`, `noexcept` and
 *         `&`/`&&` qualifiers are supported and constrain the call operator.
 * @tparam Capacity inline storage size in bytes (default: 3 pointers).
 * @tparam Alignment inline storage alignment (default: alignof(std::max_align_t)).
 * @tparam EnableTarget opt in to RTTI-based target introspection
 *         (`target_type()`/`target<T>()`). Disabled by default to keep the
 *         vtable minimal. Enabling requires RTTI; combining `enabled` with a
 *         no-RTTI build (`-fno-rtti`/`/GR-`) is a compile-time error.
 */
template < typename Sig,
           std::size_t      Capacity     = 3 * sizeof( void* ),
           std::size_t      Alignment    = alignof( std::max_align_t ),
           EnableTargetType EnableTarget = EnableTargetType::disabled >
class inplace_function : public inplace_function_detail::base< true, Sig, Capacity, Alignment, EnableTarget >
{
    using base_type = inplace_function_detail::base< true, Sig, Capacity, Alignment, EnableTarget >;

public:
    using base_type::base_type;
    using base_type::operator=;

    friend void swap( inplace_function& lhs, inplace_function& rhs ) noexcept
    {
        lhs.swap( rhs );
    }
};

template < typename R, typename... Args >
inplace_function( R ( * )( Args... ) ) -> inplace_function< R( Args... ) >;

template < typename R, typename... Args >
inplace_function( R ( * )( Args... ) noexcept ) -> inplace_function< R( Args... ) noexcept >;

// =============================================================================
// move_only_inplace_function
// =============================================================================

/**
 * @brief Fixed-capacity, non-allocating, move-only function wrapper.
 *
 * Same design as `inplace_function`, but the closure only needs to be
 * nothrow-move-constructible, so move-only callables (e.g. lambdas capturing a
 * `std::unique_ptr`) can be stored. Copy operations are deleted.
 *
 * @tparam Sig function signature, e.g. `void()`. `const`, `noexcept` and
 *         `&`/`&&` qualifiers are supported and constrain the call operator.
 * @tparam Capacity inline storage size in bytes (default: 3 pointers).
 * @tparam Alignment inline storage alignment (default: alignof(std::max_align_t)).
 * @tparam EnableTarget opt in to RTTI-based target introspection
 *         (`target_type()`/`target<T>()`). Disabled by default to keep the
 *         vtable minimal. Enabling requires RTTI; combining `enabled` with a
 *         no-RTTI build (`-fno-rtti`/`/GR-`) is a compile-time error.
 */
template < typename Sig,
           std::size_t      Capacity     = 3 * sizeof( void* ),
           std::size_t      Alignment    = alignof( std::max_align_t ),
           EnableTargetType EnableTarget = EnableTargetType::disabled >
class move_only_inplace_function : public inplace_function_detail::base< false, Sig, Capacity, Alignment, EnableTarget >
{
    using base_type = inplace_function_detail::base< false, Sig, Capacity, Alignment, EnableTarget >;

public:
    using base_type::base_type;
    using base_type::operator=;

    friend void swap( move_only_inplace_function& lhs, move_only_inplace_function& rhs ) noexcept
    {
        lhs.swap( rhs );
    }
};

template < typename R, typename... Args >
move_only_inplace_function( R ( * )( Args... ) ) -> move_only_inplace_function< R( Args... ) >;

template < typename R, typename... Args >
move_only_inplace_function( R ( * )( Args... ) noexcept ) -> move_only_inplace_function< R( Args... ) noexcept >;

} // namespace nova
