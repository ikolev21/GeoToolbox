// Copyright 2024-2025 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <stdexcept>

#ifndef MAKE_STRING
#	define MAKE_STRING( x ) MAKE_STRING_2( x )
#	define MAKE_STRING_2( x ) #x
#endif

#if !defined( DEBUG_ONLY )
#	if defined( NDEBUG )
#		define DEBUG_ONLY( x )
#	else
#		define DEBUG_ONLY( x )	x
#	endif
#endif

#if !defined( RELEASE_ONLY )
#	if defined( NDEBUG )
#		define RELEASE_ONLY( x )	x
#	else
#		define RELEASE_ONLY( x )
#	endif
#endif

#if !defined( SELECT_DEBUG_RELEASE )
#	if defined( NDEBUG )
#		define SELECT_DEBUG_RELEASE( debugExpr, releaseExpr )	releaseExpr
#	else
#		define SELECT_DEBUG_RELEASE( debugExpr, releaseExpr )	debugExpr
#	endif
#endif

namespace GeoToolbox
{
	constexpr auto IsReleaseBuild =
#if defined( NDEBUG )
		true;
#else
		false;
#endif

	template <typename T>
	constexpr T SelectDebugRelease([[maybe_unused]] T debugValue, [[maybe_unused]] T releaseValue)
	{
#if defined( NDEBUG )
		return releaseValue;
#else
		return debugValue;
#endif
	}

	template <class TException = std::logic_error>
	constexpr void Assert(bool condition, char const* message)
	{
		if (!condition)
		{
			throw TException{ message };
		}
	}
}

#ifndef ASSERT
#	define ASSERT( condition, ... ) \
		GeoToolbox::Assert<__VA_ARGS__>( condition, "Assertion failed: " #condition " at " __FILE__ ":" MAKE_STRING( __LINE__ ) )
#endif

#if !defined( DEBUG_ASSERT )
#	if defined( NDEBUG )
#		define DEBUG_ASSERT( x, ... )	(void)0
#	else
#		define DEBUG_ASSERT( x, ... )	ASSERT( x, __VA_ARGS__ )
#	endif
#endif
