// Copyright 2024-2026 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "GeoToolbox/StlExtensions.hpp"

#include <catch2/catch_test_macros.hpp>

#pragma message( "__cplusplus: " MAKE_STRING( __cplusplus ) )
#if defined( __clang__ )
#	pragma message( "Clang: "  MAKE_STRING( __clang_major__ ) "." MAKE_STRING( __clang_minor__ ) "." MAKE_STRING( __clang_patchlevel__ ) )
#elif defined( _MSC_VER )
#	pragma message( "_MSC_VER: " MAKE_STRING( _MSC_VER ) )
#	pragma message( "_MSVC_LANG: " MAKE_STRING( _MSVC_LANG ) )
#elif defined( __GNUC__ )
#	pragma message( "GCC: " MAKE_STRING( __GNUC__ ) "." MAKE_STRING( __GNUC_MINOR__ ) "." MAKE_STRING( __GNUC_PATCHLEVEL__ ) )
#endif

using namespace GeoToolbox;
using namespace std;

TEST_CASE("PointerOrInt_Pointer")
{
	// Default-constructed holds a null pointer
	PointerOrInt<int> const empty;
	REQUIRE(empty.IsPointer());
	REQUIRE_FALSE(empty.IsInt());
	REQUIRE(empty == nullptr);

	PointerOrInt<int> const null{ nullptr };
	REQUIRE(null.IsPointer());
	REQUIRE_FALSE(null.IsInt());
	REQUIRE(null == nullptr);

	// Round-trips a real pointer, and dereferences to it
	auto object = 42;
	PointerOrInt const p{ &object };
	REQUIRE(p.IsPointer());
	REQUIRE_FALSE(p.IsInt());
	REQUIRE(p != nullptr);
	REQUIRE(p.get() == &object);
	REQUIRE(*p == 42);

	// operator-> forwards to the pointee
	string thing = "asd";
	PointerOrInt const t{ &thing };
	REQUIRE(t->length() == 3);
	REQUIRE(*t == "asd");
}

TEST_CASE("PointerOrInt_Integer")
{
	// A non-negative integer is stored as an int, distinct from the pointer case
	PointerOrInt<int> const five{ 5 };
	REQUIRE(five.IsInt());
	REQUIRE_FALSE(five.IsPointer());
	REQUIRE(five.GetInt() == 5);

	// Integer 0 is an int (storage bit set), and must not be confused with a default-constructed null pointer
	PointerOrInt<int> const zero{ int64_t{ 0 } };
	REQUIRE(zero.IsInt());
	REQUIRE(zero.GetInt() == 0);

	// A large positive value within the supported half-range round-trips
	PointerOrInt<int> const big{ 1'000'000 };
	REQUIRE(big.IsInt());
	REQUIRE(big.GetInt() == 1'000'000);
}

// The class promises "half the range of int64_t" (i.e. signed), so a negative integer must round-trip
TEST_CASE("PointerOrInt_NegativeInteger")
{
	PointerOrInt<int> const negative{ -3 };
	REQUIRE(negative.IsInt());
	REQUIRE(negative.GetInt() == -3);

	// -1 is the sharpest case: it encodes to storage_ == -1 (all bits set), which must still read as IsInt()
	PointerOrInt<int> const minusOne{ -1 };
	REQUIRE(minusOne.IsInt());
	REQUIRE(minusOne.GetInt() == -1);
}
