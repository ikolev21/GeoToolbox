// Copyright 2024-2026 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "Boost.hpp"

#include "GeoToolbox/ShapeFile.hpp"

#include <boost/geometry.hpp>
#include <boost/geometry/index/rtree.hpp>

using namespace GeoToolbox;
using namespace std;

namespace Bgi = boost::geometry::index;


template <typename TSpatialKey>
int BoostRtree<TSpatialKey>::QueryBox(shared_ptr<void> const& indexPtr, BoxType const& queryBox) const
{
	auto& index = *static_cast<IndexType const*>(indexPtr.get());

	auto count = 0;
	index.query(Bgi::intersects(queryBox), CountingOutputIterator{ count }/*, &comparisonsCount*/);
	return count;
}

template <typename TSpatialKey>
double BoostRtree<TSpatialKey>::QueryNearest(shared_ptr<void> const& indexPtr, VectorType const& location, int nearestCount) const
{
	using ScalarType = typename SpatialKeyTraits<TSpatialKey>::ScalarType;

	auto& index = *static_cast<IndexType const*>(indexPtr.get());
	vector<pair<FeatureId, ScalarType>> nearest;
	nearest.reserve(nearestCount);
	auto comparer = [](pair<FeatureId, ScalarType> const& a, pair<FeatureId, ScalarType> const& b)
		{
			return a.second < b.second;
		};

	index.query(
		Bgi::nearest(location, nearestCount),
		OutputIteratorFunction{ [&](FeaturePtr feature)
		{
			auto const distance2 = GetDistanceSquared(location, feature->spatialKey);
			auto const record = pair(feature->id, distance2);
			auto position = lower_bound(nearest.begin(), nearest.end(), record, comparer);
			nearest.insert(position, record);
			if (Size(nearest) > nearestCount)
			{
				nearest.erase(--nearest.end());
			}
		} });

	return accumulate(nearest.begin(), nearest.end(), 0.0, [](double sum, pair<FeatureId, ScalarType> const& f) { return sum + double(f.second); });
}

template struct BoostRtree<Vector2>;
template struct BoostRtree<Vector3f>;
template struct BoostRtree<Box2>;
template struct BoostRtree<Box3f>;
#if defined( ENABLE_EIGEN )
template struct BoostRtree<EVector2>;
template struct BoostRtree<Box<EVector2>>;
#endif


#include "catch2/catch_template_test_macros.hpp"
#include "catch2/benchmark/catch_benchmark.hpp"

TEST_CASE("RTreePerformance_Insert_Boost", "[.Performance]")
{
	WarnInDebugBuild();

	if constexpr (01)
	{
		ShapeFile const properties("../../data/Property_Boundary_View.shp");
		auto const boxes = properties.GetKeys<Box2>();
		cout << "Boxes count: " << boxes.size() << '\n';

		Bgi::rtree< Box2, Bgi::rstar<MaxElementsPerNode> > rtree;

		for (auto const& box : boxes)
		{
			rtree.insert(box);
		}
	}

	if constexpr (0)
	{
		auto const points = ShapeFile("../../data/Property_Point_View.shp").GetKeys<Vector2>();
		cout << "Poins count: " << points.size() << '\n';

		Bgi::rtree< Vector2, Bgi::rstar<MaxElementsPerNode> > rtree;

		for (auto const& p : points)
		{
			rtree.insert(p);
		}
	}
}

TEST_CASE("RTreePerformance_Boost", "[.Performance]")
{
	WarnInDebugBuild();

	auto const points = ShapeFile("../../data/Property_Point_View.shp").GetKeys<Vector2>();

	ShapeFile const properties("../../data/Property_Boundary_View.shp");
	auto const boxes = properties.GetKeys<Box2>();

	static auto constexpr PointCount = SelectDebugRelease(1000, 500000);
	static auto constexpr BoxCount = SelectDebugRelease(1000, 500000);
	static auto constexpr SegmentCount = SelectDebugRelease(1000, 150000);

	//copy( filesystem::directory_iterator{ "." }, filesystem::directory_iterator{}, back_inserter( 

	using BgSegment = boost::geometry::model::segment<Vector2>;

	//auto const segments = properties.GetSegments<BgSegment>();
	auto const segments = Transform(
		ShapeFile("../../data/Street.shp").GetSegments(),
		[](auto const& s) { return BgSegment(s.first, s.second); }
	);

	REQUIRE(points.size() > PointCount);
	REQUIRE(boxes.size() > BoxCount);
	REQUIRE(segments.size() > SegmentCount);

	BENCHMARK("BG R-tree - points: bulk load")
	{
		Bgi::rtree< Vector2, Bgi::rstar<16> > const rtree{ points.begin(), points.begin() + PointCount };
		return rtree.bounds();
	};

	BENCHMARK("BG R-tree - boxes: bulk load")
	{
		Bgi::rtree< Box2, Bgi::rstar<16> > const rtree{ boxes.begin(), boxes.begin() + BoxCount };
		return rtree.bounds();
	};

	BENCHMARK("BG R-tree - segments: bulk load")
	{
		Bgi::rtree< BgSegment, Bgi::rstar<16> > const rtree{ segments.begin(), segments.begin() + SegmentCount };
		return rtree.bounds();
	};
}
