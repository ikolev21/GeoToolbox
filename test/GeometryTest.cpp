// Copyright 2024 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "GeoToolbox/GeometryTools.hpp"

#include "GeoToolbox/Profiling.hpp"
#include "GeoToolbox/SpatialTools.hpp"

#include <catch2/catch_test_macros.hpp>

#include <iostream>
#include <unordered_set>

using namespace GeoToolbox;
using namespace std;

TEST_CASE("Vector")
{
	Vector2 const x{ DoNotOptimize(1.0), DoNotOptimize(2.0) };
	Vector2 const y{ 3, DoNotOptimize(4.0) };
	auto z = x + y;
	REQUIRE(z[0] == 4);
	REQUIRE(z[1] == 6);
	z = z / DoNotOptimize(2.0);
	REQUIRE(z[0] == 2);
	REQUIRE(z[1] == 3);

	REQUIRE(MinimumValue(z) == 2.0);
	REQUIRE(MaximumValue(z) == 3.0);

	z = 2 * Flat<Vector2>(1.0);
	REQUIRE(z[0] == 2.0);
	REQUIRE(z[1] == 2.0);

	REQUIRE(Min(z, x) == x);
	REQUIRE(Max(z, x) == z);


	REQUIRE(y - x == Vector2{ 2, 2 });
	REQUIRE(GetDistanceSquared(x, y - x) == 1.0);

#if defined(ENABLE_EIGEN)
	EVector2 const ex{ DoNotOptimize(1.0), DoNotOptimize(2.0) };
	EVector2 const ey{ 1, DoNotOptimize(3.0) };

	REQUIRE(ey - ex == EVector2{ 0, 1 });
	REQUIRE(GetDistanceSquared(ex, ey) == 1.0);
#endif
}

TEST_CASE("Box")
{
	STATIC_REQUIRE(Box2().IsEmpty());

	STATIC_REQUIRE(Box2::Bound({ 0, 1 }, { 1, 0 }) == Box2({ 0, 0 }, { 1, 1 }));
	// array::op== is constexpr since C++20, can't do STATIC_REQUIRE
	REQUIRE(Box2() + Vector2{ 1, 1 } == Box2({ 1, 1 }));

	constexpr Vector2 a{ 0, 1 };
	constexpr Vector2 b{ 1, 0 };
	REQUIRE(Min(a, b) == Vector2{ 0, 0 });
	REQUIRE(Max(a, b) == Vector2{ 1, 1 });

	auto box = Box2::Bound(a, b);

	constexpr auto boxMiddle = Box2{ {0.5, 0.5} };
	REQUIRE(Intersect(box, boxMiddle) == boxMiddle);
	REQUIRE(Intersect(box, Box2{ {0.5, 0.5}, {1.5, 1.5} }) == Box2{ {0.5, 0.5}, {1., 1.} });

	box.Add({ 2, 2 });
	REQUIRE(box == Box2({ 0, 0 }, { 2, 2 }));
	REQUIRE(box.Center() == Vector2{ 1, 1 });

	REQUIRE(Box2().Add({ 1, 1 }) == Box2({ 1, 1 }));

	{
		array const boxes = { Box2{ { 0, 0 }, { 1, 1 } }, Box2{ { 1, 1 }, { 2, 2 } } };
		REQUIRE(Bound(boxes) == Box2{ { 0, 0 }, { 2, 2 } });
	}
}

TEST_CASE("Feature")
{
	[[maybe_unused]] std::unordered_set<Feature<Vector2>> const featureCanBeStoredInAHashContainer;
}
