#include "Boost.hpp"

#include "GeoToolbox/ShapeFile.hpp"

#include <boost/geometry.hpp>
#include <boost/geometry/index/rtree.hpp>

using namespace GeoToolbox;
using namespace std;

namespace Bgi = boost::geometry::index;


template <typename TSpatialKey>
int BoostRtree<TSpatialKey>::QueryBox(IndexType const& index, BoxType const& queryBox)
{
	auto count = 0;
	index.query(Bgi::intersects(queryBox), CountingOutputIterator{ count }/*, &comparisonsCount*/);
	return count;
}

template <typename TSpatialKey>
double BoostRtree<TSpatialKey>::QueryNearest(IndexType const& index, VectorType const& location, int nearestCount)
{
	//vector<pair<FeatureId, double>> nearest;
	//nearest.reserve(nearestCount);
	auto distSum = 0.0;
	index.query(
		Bgi::nearest(location, nearestCount),
		OutputIteratorFunction{ [&distSum, &location](FeaturePtr value) { distSum += GetDistanceSquared(location, value->spatialKey); } });
	//auto const distSum = std::accumulate(nearest.begin(), nearest.end(), 0.0, [](double sum, pair<FeatureId, double> const& f) { return sum + f.second; });

	//cout << "BoostRtree nearest result for location " << location << ": " << distSum << " -> ";
	//PrintNearest(nearest);

	return distSum;
}

template struct BoostRtree<Vector2>;
template struct BoostRtree<Box2>;
#if defined( ENABLE_EIGEN )
template struct BoostRtree<EVector2>;
template struct BoostRtree<Box<EVector2>>;
#endif


#include "catch2/catch_template_test_macros.hpp"
#include "catch2/benchmark/catch_benchmark.hpp"

TEST_CASE("RTreePerformance_Insert_Boost", "[.Performance]")
{
	if constexpr (01)
	{
		ShapeFile const properties("../../data/Property_Boundary_View.shp");
		auto const boxes = properties.GetKeys<Box2>();
		std::cout << "Boxes count: " << boxes.size() << '\n';

		Bgi::rtree< Box2, Bgi::rstar<MaxElementsPerNode> > rtree;

		for (auto const& box : boxes)
		{
			rtree.insert(box);
		}
	}

	if constexpr (0)
	{
		auto const points = ShapeFile("../../data/Property_Point_View.shp").GetKeys<Vector2>();
		std::cout << "Poins count: " << points.size() << '\n';

		Bgi::rtree< Vector2, Bgi::rstar<MaxElementsPerNode> > rtree;

		for (auto const& p : points)
		{
			rtree.insert(p);
		}
	}
}

TEST_CASE("RTreePerformance_Boost", "[.Performance]")
{
	auto const points = ShapeFile("../../data/Property_Point_View.shp").GetKeys<Vector2>();

	ShapeFile const properties("../../data/Property_Boundary_View.shp");
	auto const boxes = properties.GetKeys<Box2>();

	static auto constexpr PointCount = SelectDebugRelease(1000, 500000);
	static auto constexpr BoxCount = SelectDebugRelease(1000, 500000);
	static auto constexpr SegmentCount = SelectDebugRelease(1000, 150000);

	//std::copy( std::filesystem::directory_iterator{ "." }, std::filesystem::directory_iterator{}, back_inserter( 

	using BgSegment = boost::geometry::model::segment<Vector2>;

	//auto const segments = properties.GetSegments<BgSegment>();
	auto const segments = Transform(
		ShapeFile("../../data/Street.shp").GetSegments(),
		[](auto const& s) { return BgSegment(s.first, s.second); }
	);

	REQUIRE(points.size() > PointCount);
	REQUIRE(boxes.size() > BoxCount);
	REQUIRE(segments.size() > SegmentCount);

	BENCHMARK("BG R-Tree - points: bulk load")
	{
		Bgi::rtree< Vector2, Bgi::rstar<16> > const rtree{ points.begin(), points.begin() + PointCount };
		return rtree.bounds();
	};

	BENCHMARK("BG R-Tree - boxes: bulk load")
	{
		Bgi::rtree< Box2, Bgi::rstar<16> > const rtree{ boxes.begin(), boxes.begin() + BoxCount };
		return rtree.bounds();
	};

	BENCHMARK("BG R-Tree - segments: bulk load")
	{
		Bgi::rtree< BgSegment, Bgi::rstar<16> > const rtree{ segments.begin(), segments.begin() + SegmentCount };
		return rtree.bounds();
	};
}
