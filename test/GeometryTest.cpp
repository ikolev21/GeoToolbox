// Copyright 2024-2026 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "GeoToolbox/GeometryTools.hpp"

#include "GeoToolbox/Profiling.hpp"
#include "GeoToolbox/SpatialTools.hpp"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <iostream>
#include <unordered_set>

#include "Performance/TestTools.hpp"

using namespace GeoToolbox;
using namespace std;

TEST_CASE("Vector")
{
	STATIC_REQUIRE(VectorTraits<Vector3>::Name == "array3d"sv);

	Vector2 const x{ DoNotOptimize(1.0), DoNotOptimize(2.0) };
	Vector2 const y{ 3, DoNotOptimize(4.0) };
	auto z = x + y;
	REQUIRE(z[0] == 4);
	REQUIRE(z[1] == 6);
	z = z / DoNotOptimize(2.0);
	REQUIRE(z[0] == 2);
	REQUIRE(z[1] == 3);

	REQUIRE(MinimumValue(z).first == 2.0);
	REQUIRE(MaximumValue(z).first == 3.0);

	z = 2 * Flat<Vector2>(1.0);
	REQUIRE(z[0] == 2.0);
	REQUIRE(z[1] == 2.0);

	z += Zero<Vector2>();
	REQUIRE(z == Vector2{ 2, 2 });

	z = 4 / z;
	REQUIRE(z == Vector2{ 2, 2 });

	REQUIRE(Min(z, x) == x);
	REQUIRE(Max(z, x) == z);


	REQUIRE(y - x == Vector2{ 2, 2 });
	REQUIRE(GetDistanceSquared(x, y - x) == 1.0);

	z = ComponentMultiply(x, y);
	REQUIRE(z == Vector2{ 3, 8 });

#if defined(ENABLE_EIGEN)
	EVector2 const ex{ DoNotOptimize(1.0), DoNotOptimize(2.0) };
	EVector2 const ey{ 1, DoNotOptimize(3.0) };

	REQUIRE(ey - ex == EVector2{ 0, 1 });
	REQUIRE(GetDistanceSquared(ex, ey) == 1.0);
	REQUIRE(Zero<EVector2>() == EVector2{ 0, 0 });
#endif

	REQUIRE(Convert<Vector3>(Vector3f{ 1.f, 2.f, 0 }) == Vector3{ 1., 2., 0 });
}

TEST_CASE("Box")
{
	STATIC_REQUIRE(Box2{}.IsEmpty());

	STATIC_REQUIRE(Box2::Bound({ 0, 1 }, { 1, 0 }) == Box2({ 0, 0 }, { 1, 1 }));
	// array::op== is constexpr since C++20, can't do STATIC_REQUIRE
	REQUIRE(Box2{} + Vector2{ 1, 1 } == Box2({ 1, 1 }));

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

	REQUIRE(Box2{}.Add({ 1, 1 }) == Box2({ 1, 1 }));

	{
		array const boxes = { Box2{ { 0, 0 }, { 1, 1 } }, Box2{ { 1, 1 }, { 2, 2 } } };
		REQUIRE(Bound(boxes) == Box2{ { 0, 0 }, { 2, 2 } });
	}
	{
		array const points = { Vector2{ 0, 0 }, Vector2{ 1, 1 }, Vector2{ 2, 2 } };
		REQUIRE(Bound(points) == Box2{ { 0, 0 }, { 2, 2 } });
	}

	REQUIRE(Overlap(box, Box2{ { 0.5, 0.5 }, { 1, 1 } }));
	REQUIRE(Contains(box, Box2{ { 0.5, 0.5 }, { 1, 1 } }));
	REQUIRE(Overlap(box, Box2{ { 0.5, 0.5 }, { 3, 3 } }));
	REQUIRE_FALSE(Contains(box, Box2{ { 0.5, 0.5 }, { 3, 3 } }));

	auto boxf = Box<Vector2f>::Convert( box );
	REQUIRE( boxf == Box2f{ { 0.f, 0.f }, { 2.f, 2.f } } );
}

// The empty box (min at +infinity, max at -infinity) is a legitimate input to the value-building operations and to the Overlap/Intersect predicates, where it consistently behaves like "nothing".
// Operations whose result on an empty box is meaningless (Contains, Center, distance, closest point) forbid it by precondition and are not tested here.
TEST_CASE("Box empty")
{
	constexpr Box2 empty;
	REQUIRE(empty.IsEmpty());
	REQUIRE_FALSE(Box2{ { 0, 0 }, { 1, 1 } }.IsEmpty());

	// A genuinely unbounded box is not empty - only a +infinity min marks emptiness
	REQUIRE_FALSE(Box2{ Flat<Vector2>(-numeric_limits<double>::infinity()), Flat<Vector2>(numeric_limits<double>::infinity()) }.IsEmpty());

	// operator==: two empties are equal, an empty is never equal to a real box
	REQUIRE(empty == Box2{});
	REQUIRE_FALSE(empty == Box2{ { 0, 0 } });

	// Growing an empty box absorbs the first point exactly, at both ends and including negative coordinates
	REQUIRE(Box2{}.Add({ -3, 5 }) == Box2{ { -3, 5 } });
	REQUIRE((Box2{} + Vector2{ 2, -2 }) == Box2{ { 2, -2 } });

	// A sequence of Adds must shrink the low end and grow the high end correctly
	{
		Box2 growing;
		growing.Add({ 1, 1 });
		growing.Add({ -1, 3 });
		growing.Add({ 5, 0 });
		REQUIRE(growing == Box2{ { -1, 0 }, { 5, 3 } });
	}

	// Merging with an empty box either way is a no-op / a copy
	{
		Box2 box{ { 0, 0 }, { 1, 1 } };
		box.Add(Box2{});
		REQUIRE(box == Box2{ { 0, 0 }, { 1, 1 } });
	}
	{
		Box2 box;
		box.Add(Box2{ { 0, 0 }, { 1, 1 } });
		REQUIRE(box == Box2{ { 0, 0 }, { 1, 1 } });
	}

	// Moving an empty box keeps it empty
	REQUIRE(Box2{}.Move({ 1, 1 }).IsEmpty());

	// Converting an empty box yields an empty box (must not trip the min <= max assertion)
	REQUIRE(Box<Vector2f>::Convert(empty).IsEmpty());

	// An empty box overlaps nothing, in either argument order and against a point
	Box2 const unit{ { 0, 0 }, { 1, 1 } };
	REQUIRE_FALSE(Overlap(empty, unit));
	REQUIRE_FALSE(Overlap(unit, empty));
	REQUIRE_FALSE(Overlap(empty, empty));
	REQUIRE_FALSE(Overlap(empty, Vector2{ 0, 0 }));

	// Intersecting with an empty box is empty, in either argument order
	REQUIRE(Intersect(empty, unit).IsEmpty());
	REQUIRE(Intersect(unit, empty).IsEmpty());
}

TEST_CASE("Feature")
{
	[[maybe_unused]] std::unordered_set<Feature<Vector2>> const featureCanBeStoredInAHashContainer;
}
