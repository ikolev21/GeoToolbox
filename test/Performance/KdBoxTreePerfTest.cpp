// Copyright 2024-2026 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "NanoflannAdapter.hpp"
#include "GeoToolbox/ShapeFile.hpp"

#include "catch2/catch_test_macros.hpp"

using namespace GeoToolbox;
using namespace std;

int CompareSpatialIndices(PerfRecord&);


TEST_CASE("KdBoxTree_Performance", "[.Performance]")
{
	GetConfig().InsertOrAssign(
	{
		{ "Index", "KdBoxTree" },
		{ "DatasetSize", "5" },
		//{ "MinDatasetSize", "3" },
		//{ "MaxDatasetSize", "4" },
		{ "Dataset", "Synthetic_Uniform,Synthetic_Clusters,Synthetic_Polygon" },
		{ "Scenario", "Load" },
		{ "SpatialKey", "point" },
		{ "Vector", "array2d" }
	}
	);

#if NANOFLANN_NODE_ALIGNMENT == 8
	STATIC_REQUIRE(sizeof(NanoflannKdtreeAdapter<Vector2>::TreeType::Node) == 40);
	STATIC_REQUIRE(sizeof(NanoflannKdtreeAdapter<Box2>::TreeType::Node) == 40);
#elif NANOFLANN_NODE_ALIGNMENT == 16
	STATIC_REQUIRE(sizeof(NanoflannKdtreeAdapter<Vector2>::TreeType::Node) == 48);
	STATIC_REQUIRE(sizeof(NanoflannKdtreeAdapter<Box2>::TreeType::Node) == 48);
#elif NANOFLANN_NODE_ALIGNMENT == 32
	STATIC_REQUIRE(sizeof(NanoflannKdtreeAdapter<Vector2>::TreeType::Node) == 64);
	STATIC_REQUIRE(sizeof(NanoflannKdtreeAdapter<Box2>::TreeType::Node) == 64);
#endif 
	STATIC_REQUIRE(sizeof(NanoflannKdtreeAdapter<Vector3f>::TreeType::Node) == 32);
	STATIC_REQUIRE(sizeof(NanoflannKdtreeAdapter<Box3f>::TreeType::Node) == 32);

	PerfRecord perfRecord{ "KdBoxTree" };
	CompareSpatialIndices(perfRecord);
}
