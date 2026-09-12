// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

// Negative test: closures whose move constructor may throw are rejected
// (relocation is noexcept, so nothrow-move-constructibility is required).
// Built EXCLUDE_FROM_ALL; ctest runs the build itself and expects failure
// (WILL_FAIL).

#include <nova/inplace_function.hpp>

namespace {

struct throwing_move_closure
{
    throwing_move_closure()                               = default;
    throwing_move_closure( const throwing_move_closure& ) = default;
    throwing_move_closure( throwing_move_closure&& ) noexcept( false )
    {}

    int operator()( int x ) const
    {
        return x;
    }
};

} // namespace

using rejected = nova::move_only_inplace_function< int( int ) >;

int main()
{
    rejected f( throwing_move_closure {} );
    return f( 1 );
}
