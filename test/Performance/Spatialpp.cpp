#include "Spatialpp.hpp"

#include <spatial_2.1.8/src/region_iterator.hpp>

using namespace GeoToolbox;
using namespace std;

template <class TVector>
struct SpatialppFeatureMetric
{
	static_assert(IsVector<TVector>, "Type is not a vector");

	using KeyType = TVector;

	using distance_type = double;

	distance_type distance_to_key(spatial::dimension_type, Feature<KeyType> const* origin, Feature<KeyType> const* key) const
	{
		return GetDistanceSquared(origin->spatialKey, key->spatialKey);
	}

	distance_type distance_to_plane(spatial::dimension_type, spatial::dimension_type dim, Feature<KeyType> const* origin, Feature<KeyType> const* key) const
	{
		return pow(origin->spatialKey[dim] - key->spatialKey[dim], 2.0);
	}
};

template <class TVector>
struct SpatialppFeatureMetric<Box<TVector>>
{
	using KeyType = Box<TVector>;

	using distance_type = double;

	distance_type distance_to_key(spatial::dimension_type, Feature<KeyType> const* origin, Feature<KeyType> const* key) const
	{
		return GetDistanceSquared(origin->spatialKey.Min(), key->spatialKey);
	}

	distance_type distance_to_plane(spatial::dimension_type, spatial::dimension_type dim, Feature<KeyType> const* origin, Feature<KeyType> const* key) const
	{
		if (dim >= KeyType::Dimensions)
		{
			dim -= KeyType::Dimensions;
		}

		auto const closest = GetClosestPointOnBox(key->spatialKey, origin->spatialKey.Min());
		return pow(origin->spatialKey.Min()[dim] - closest[dim], 2.0);
		//auto const originValue = origin.spatialKey->Min()[dim];
		//return originValue < key.spatialKey.Min()[dim] ? pow(originValue - key.spatialKey.Min()[dim], 2.0)
		//	: originValue > key.spatialKey.Max()[dim] ? pow(originValue - key.spatialKey.Max()[dim], 2.0)
		//	: 0.0;
	}
};

template <typename TSpatialKey, bool IsPoint>
int SpatialppKdtree<TSpatialKey, IsPoint>::QueryBox(IndexType const& index, BoxType const& box)
{
	auto count = 0;
	if constexpr (IsPoint)
	{
		Feature<TSpatialKey> const lower = { 0, box.Min() };
		Feature<TSpatialKey> const upper = { 0, box.Max() };
		for (auto iter = region_begin(index, &lower, &upper); iter != index.end(); ++iter)
		{
			++count;
		}
	}
	else
	{
		Feature<TSpatialKey> featureBox = { 0, box };
		for (auto iter = overlap_region_begin(index, &featureBox, spatial::llhh_layout_tag()); iter != index.end(); ++iter)
		{
			++count;
		}
	}

	return count;
}

template <typename TSpatialKey, bool IsPoint>
double SpatialppKdtree<TSpatialKey, IsPoint>::QueryNearest(IndexType const& index, TSpatialKey const& location, int nearestCount)
{
	if constexpr (!IsPoint)
	{
		return -1;
	}
	else
	{
		auto count = 0;
		auto distSum = 0.0;
		//vector<pair<FeatureId, double>> nearest;
		//nearest.reserve(nearestCount);
		Feature<TSpatialKey> const locationFeature = { 0, location };
		for (auto iter = neighbor_begin(index, SpatialppFeatureMetric<TSpatialKey>(), &locationFeature); iter != index.end() && count < nearestCount; ++iter, ++count)
		{
			//nearest.emplace_back(iter->id, GetDistanceSquared(location, iter->spatialKey));
			distSum += GetDistanceSquared(location, (*iter)->spatialKey);
		}

		//auto const distSum = std::accumulate(nearest.begin(), nearest.end(), 0.0, [](double sum, pair<FeatureId, double> const& f) { return sum + f.second; });

		return distSum;
	}
}

template struct SpatialppKdtree<Vector2>;
#if defined( ENABLE_EIGEN )
template struct SpatialppKdtree<EVector2>;
#endif


#include "GeoToolbox/Profiling.hpp"
#include "GeoToolbox/ShapeFile.hpp"

#include "catch2/catch_template_test_macros.hpp"

#include <iostream>

TEST_CASE("SpatialCpp_BoxTree")
{
	spatial::idle_box_multiset<4, Vector4> boxTree;

	boxTree.insert(Vector4{ 0, 0, 1, 1 });
	boxTree.insert(Vector4{ 1, 0, 2, 1 });
	boxTree.insert(Vector4{ 0, 1, 1, 2 });
	boxTree.insert(Vector4{ 2, 2, 3, 3 });

	//boxTree.rebalance();

	constexpr auto searchBox = Vector4{ 0, 0, 1.5, 1.5 };

	std::cout << "Enclosed:\n";
	auto count = 0;
	for (auto iter = enclosed_region_begin(boxTree, searchBox); iter != boxTree.end(); ++iter, ++count)
	{
		std::cout << (*iter)[0] << ' ' << (*iter)[1] << ' ' << (*iter)[2] << ' ' << (*iter)[3] << '\n';
	}

	REQUIRE(count == 1);

	std::cout << "Overlap:\n";
	count = 0;
	for (auto iter = overlap_region_begin(boxTree, searchBox); iter != boxTree.end(); ++iter, ++count)
	{
		std::cout << (*iter)[0] << ' ' << (*iter)[1] << ' ' << (*iter)[2] << ' ' << (*iter)[3] << '\n';
	}

	REQUIRE(count == 3);
}

TEST_CASE("RTreePerformance_Insert_SpatialCpp", "[.Performance]")
{
	{
		Stopwatch const x;

		ShapeFile const properties("../../../data/Property_Boundary_View.shp");

		//ASSERT( x.ElapsedMilliseconds() == 0 );

		auto const boxes = properties.GetKeys<Box2>();

		cout << "elapsed: " << x.ElapsedMilliseconds() << '\n';
		cout << "Boxes count: " << boxes.size() << '\n';

		spatial::idle_box_multiset<4, Vector4> boxTree;

		for (auto const& box : boxes)
		{
			boxTree.insert(*reinterpret_cast<Vector4 const*>(&box));
		}

		//boxTree.rebalance();
		for (auto iter = region_begin(boxTree, Vector4{ 0, 0, 0, 0 }, Vector4{ 1, 1, 1, 1 }); iter != boxTree.end(); ++iter)
		{
		}
	}

	{
		auto const points = ShapeFile("../../data/Property_Point_View.shp").GetKeys<Vector2>();
		cout << "Points count: " << points.size() << '\n';

		spatial::idle_point_multiset<2, Vector2> pointTree;

		for (auto const& p : points)
		{
			pointTree.insert(p);
		}
	}
}
