// Copyright 2024-2026 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "GeoToolbox/KdBoxTree.hpp"
#include "GeoToolbox/Profiling.hpp"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <random>

using namespace GeoToolbox;
using namespace std;

namespace
{
	template <class TTree>
	FeatureId AccumulateRange(TTree const& tree, Box2 const& box)
	{
		auto sum = 0;
		tree.QueryRange(box, [&sum](auto const elem) { sum += int(elem->id); });
		return sum;
	}

	// Checks the tree's queries against a brute-force scan over random data, for both point and box keys.
	// Runs with and without StoreSpatialKeys - a tree whose key array fell out of step with its elements would stay self-consistent, and only show up as elements reported for the wrong locations
	template <bool StoreSpatialKeys, typename KeyType>
	void Test_KdBoxTree_QueryCorrectness(KeyType)
	{
		std::mt19937 rng{ 0x5EED };
		std::uniform_real_distribution coord{ 0.0, 100.0 };
		std::uniform_real_distribution extent{ 0.0, 5.0 };

		auto const makeKey = [&]
			{
				auto const x = coord(rng), y = coord(rng);
				if constexpr (IsSpecialization<KeyType, Box>)
				{
					return KeyType{ { x, y }, { x + extent(rng), y + extent(rng) } };
				}
				else
				{
					return KeyType{ { x, y } };
				}
			};

		vector<Feature<KeyType>> features;
		features.reserve(2000);
		for (auto i = 0; i < 2000; ++i)
		{
			features.push_back({ FeatureId(i + 1), makeKey() });
		}

		using ElementType = Feature<KeyType> const*;
		KdBoxTree<ElementType, StoreSpatialKeys> const tree{ MakePointersVector(as_const(features)), 8 };
		auto const elements = tree.Elements();

		// Query boxes are drawn a bit wider than the data so they range from empty to nearly full coverage.
		std::uniform_real_distribution queryOrigin{ -10.0, 110.0 };
		std::uniform_real_distribution queryExtent{ 0.0, 25.0 };

		for (auto q = 0; q < 200; ++q)
		{
			auto const x = queryOrigin(rng), y = queryOrigin(rng);
			Box2 const range{ { x, y }, { x + queryExtent(rng), y + queryExtent(rng) } };

			vector<FeatureId> expected;
			for (auto const element : elements)
			{
				if (Overlap(range, element->spatialKey))
				{
					expected.push_back(element->id);
				}
			}

			std::sort(expected.begin(), expected.end());

			vector<FeatureId> byIndex;
			for (auto const index : tree.QueryRange(range))
			{
				byIndex.push_back(elements[index]->id);
			}

			std::sort(byIndex.begin(), byIndex.end());
			REQUIRE(byIndex == expected);

			// The visitor overload must return the same set as the index overload.
			vector<FeatureId> byVisitor;
			tree.QueryRange(range, [&byVisitor](ElementType element) { byVisitor.push_back(element->id); });
			std::sort(byVisitor.begin(), byVisitor.end());
			REQUIRE(byVisitor == expected);
		}

		for (auto q = 0; q < 200; ++q)
		{
			Vector2 const point{ queryOrigin(rng), queryOrigin(rng) };
			auto const nearestCount = 1 + int(rng() % 16);

			// The k nearest neighbours are exactly the k smallest distances; compare distances (not ids) so
			// ties at the k-th position don't make the check depend on which equally-distant element is kept.
			vector<double> expected;
			expected.reserve(elements.size());
			for (auto const element : elements)
			{
				expected.push_back(GetDistanceSquared(point, element->spatialKey));
			}

			std::sort(expected.begin(), expected.end());
			expected.resize(std::min(size_t(nearestCount), expected.size()));

			auto const result = tree.QueryNearest(point, nearestCount);
			REQUIRE(result.size() == expected.size());
			for (auto i = 0; i < int(result.size()); ++i)
			{
				REQUIRE(result[i].second == expected[i]);
			}
		}
	}

	template <typename KeyType>
	void Test_KdBoxTree_Common(KeyType)
	{
		{
			KdBoxTree<KeyType> const tree{ vector<KeyType>{} };
			REQUIRE(tree.IsEmpty());
			REQUIRE(tree.Elements().size() == 0);
			REQUIRE(tree.GetNodesCount() == 1);
			REQUIRE(tree.GetMaxElementsPerNode() == KdBoxTree<KeyType>::MaxElementsPerNode);
			REQUIRE(tree.Elements().begin() == tree.Elements().end());
			REQUIRE(++tree.BeginNodes() == tree.EndNodes());
			REQUIRE(tree.GetRootNode() != tree.EndNodes());
			REQUIRE(tree.BeginRangeQuery(Box2{ {1, 1} }) == tree.EndRangeQuery());
			REQUIRE(tree.QueryRange(Box2{ {1, 1} }).empty());
		}

		// Basic
		{
			vector<Feature<KeyType>> const features
			{
				{ 2, KeyType{ { 2, 0 } } },
				{ 1, KeyType{ { 1, 0 } } },
			};

			using ElementType = Feature<KeyType> const*;
			KdBoxTree<ElementType> const tree{ MakePointersVector(features), 1 };
			REQUIRE(tree.GetNodesCount() == 3);
			REQUIRE(tree.GetRootNode()->GetElementsCount() == 0);
			REQUIRE(tree.GetRootNode().GetLowChild()->GetElementsCount() == 1);
			REQUIRE(tree.GetRootNode().GetHighChild()->GetElementsCount() == 1);
			if constexpr (tree.KeyIsBox)
			{
				REQUIRE_FALSE(tree.GetRootNode().GetMiddleChild().IsValid());
			}

			REQUIRE(std::distance(tree.Elements().begin(), tree.Elements().end()) == Size(features));
			for (auto featurePtr : tree.Elements())
			{
				REQUIRE((featurePtr->id == 1 || featurePtr->id == 2));
			}

			REQUIRE(std::distance(tree.BeginNodes(), tree.EndNodes()) == 3);
			REQUIRE(AccumulateRange(tree, { { 10, 0 }, { 10, 0 } }) == 0);
			REQUIRE(AccumulateRange(tree, { { 1, 0 }, { 1, 0 } }) == 1);
			REQUIRE(AccumulateRange(tree, { { 2, 0 }, { 2, 0 } }) == 2);

			auto result = tree.QueryNearest({ -1, 0 }, 0, 2);
			REQUIRE(result.size() == 1);
			REQUIRE(result.front() == pair{ 0, 4.0 });
			result = tree.QueryNearest({ -1, 0 }, 1);
			REQUIRE(result.size() == 1);
			REQUIRE(result.front() == pair{ 0, 4.0 });
			result = tree.QueryNearest({ 4, 0 }, 2, 2);
			REQUIRE(result.size() == 1);
			REQUIRE(result.front() == pair{ 1, 4.0 });
			result = tree.QueryNearest({ 4, 0 }, 3);
			REQUIRE(result.size() == 2);

			REQUIRE(tree.QueryRange({ { 1, 0 }, { 1, 0 } }).size() == 1);
		}
	}
}

TEST_CASE("KdBoxTree_Common")
{
	STATIC_REQUIRE(sizeof(KdBoxTree<Vector2>::Node) == 48);
	STATIC_REQUIRE(sizeof(KdBoxTree<Box2>::Node) == 56);
	STATIC_REQUIRE(sizeof(KdBoxTree<Vector3f>::Node) == 40);
	STATIC_REQUIRE(sizeof(KdBoxTree<Box3f>::Node) == 48);

	Test_KdBoxTree_Common(Vector2{});
	Test_KdBoxTree_Common(Box2{});

	vector<Feature<Box2>> const features
	{
		{ 4, Box2{ { 4, 2 }, { 5, 3 } } },
		{ 3, Box2{ { 4, 0 }, { 5, 1 } } },
		{ 2, Box2{ { 8, 1 }, { 9, 2 } } },
		{ 1, Box2{ { 1, 1 }, { 2, 2 } } },
	};

	KdBoxTree const tree{ MakePointersVector(features), 1 };
	auto result = tree.QueryNearest({ 1.5, 1.5 }, 1);
	REQUIRE(tree.Elements()[result.front().first]->id == 1);
}

TEST_CASE("KdBoxTree_Allocators")
{
	using KeyType = Box2;
	using ElementType = Feature<KeyType> const*;

	vector<Feature<KeyType>> const features
	{
		{ 1, KeyType{ { 1, 0 } } },
		{ 2, KeyType{ { 2, 0 } } },
	};

	using TreeType = KdBoxTree<ElementType, false, KdBoxTreeTraits<ElementType>, std::pmr::polymorphic_allocator<ElementType>>;
	ProfileMemoryResource profileMemoryResource;
	std::pmr::polymorphic_allocator<ElementType> const elementAllocator{ &profileMemoryResource };
	auto elements = MakePointersVector(features, elementAllocator);
	auto const elementAllocationsCount = profileMemoryResource.GetCurrentAllocationsCount();
	REQUIRE(elementAllocationsCount > 0);
	{
		TreeType const tree{ std::move(elements), 1 };
		REQUIRE(profileMemoryResource.GetCurrentAllocationsCount() > elementAllocationsCount);
	}
	RELEASE_ONLY(REQUIRE(profileMemoryResource.GetCurrentAllocationsCount() == 0));
}

TEST_CASE("KdBoxTree_BoxKey")
{
	vector<Feature<Box2>> const features
	{
		{ 10, Box2{ { 30, 0 }, { 30, 10 } } },
		{ 3, Box2{ { 19, 4 }, { 21, 10 } } },
		{ 2, Box2{ { 19, 0 }, { 21, 6 } } },
		{ 1, Box2{ { 10, 0 }, { 10, 10 } } },
	};

	{
		KdBoxTree const tree{ MakePointersVector(features.data() + 1, features.data() + 3), 1 };
		REQUIRE_FALSE(tree.GetRootNode().GetLowChild().IsValid());
		REQUIRE_FALSE(tree.GetRootNode().GetMiddleChild().IsValid());
		REQUIRE_FALSE(tree.GetRootNode().GetHighChild().IsValid());
	}
	{
		KdBoxTree const tree{ MakePointersVector(features.data(), features.data() + 3), 1 };
		REQUIRE_FALSE(tree.GetRootNode().GetMiddleChild().IsValid());
	}
	{
		KdBoxTree const tree{ MakePointersVector(features), 1 };
		REQUIRE(tree.GetRootNode().GetLowChild().IsValid());
		REQUIRE(tree.GetRootNode().GetHighChild().IsValid());
	}
}

TEST_CASE("KdBoxTree_CoincidentPoints")
{
	// More coincident points than fit in a leaf must not send Build() into an endless split loop. Their bounding box has zero size, so no split can separate them
	vector const points(8, Vector2{ { 3, 7 } });

	KdBoxTree const tree{ points, 1 };

	REQUIRE_FALSE(tree.IsEmpty());
	REQUIRE(tree.GetElementsCount() == Size(points));
	// All points coincide, so a range query at that location returns all of them, and nowhere else.
	REQUIRE(tree.QueryRange({ { 3, 7 }, { 3, 7 } }).size() == points.size());
	REQUIRE(tree.QueryRange({ { 0, 0 }, { 0, 0 } }).empty());
}

TEST_CASE("KdBoxTree_NodeIterationVisitsMiddleChild")
{
	// A node with no low child but both a middle and a high child must still be fully traversed by NodeIterator
	vector<Feature<Box2>> const features
	{
		{ 1, Box2{ { 0, 0 }, { 10, 1 } } },
		{ 2, Box2{ { 8, 0 }, { 9, 1 } } },
		{ 3, Box2{ { 9, 0 }, { 10, 1 } } },
	};

	KdBoxTree const tree{ MakePointersVector(features), 1 };

	auto const root = tree.GetRootNode();
	REQUIRE_FALSE(root.GetLowChild().IsValid());
	REQUIRE(root.GetMiddleChild().IsValid());
	REQUIRE(root.GetHighChild().IsValid());

	// Every node must be reachable by walking BeginNodes()..EndNodes(), even when there's no low child
	REQUIRE(std::distance(tree.BeginNodes(), tree.EndNodes()) == tree.GetNodesCount());
}

TEST_CASE("KdBoxTree_PartitionClassification")
{
	// Exercises every branch of PartitionBoxes over random boxes and checks that each split classifies its children correctly.
	// Query results alone can't catch a mis-partition (child boxes are recomputed to bound whatever lands in them), so we verify the structural postcondition:
	// the split position is recoverable as box.Min + box.Sizes / 2 on the stored split axis, low boxes stay below it, high boxes at or above it, middle boxes straddle it.
	std::mt19937 rng{ 0xB0A75 };
	std::uniform_real_distribution origin{ 0.0, 100.0 };
	std::uniform_real_distribution extent{ 0.0, 5.0 };

	vector<Feature<Box2>> features;
	features.reserve(4000);
	for (auto i = 0; i < 4000; ++i)
	{
		auto const x = origin(rng), y = origin(rng);
		features.push_back({ FeatureId(i + 1), Box2{ { x, y }, { x + extent(rng), y + extent(rng) } } });
	}

	KdBoxTree const tree{ MakePointersVector(as_const(features)), 4 };

	auto const nodes = tree.Nodes();
	for (auto const& node : nodes)
	{
		if (node.IsLeaf())
		{
			continue;
		}

		auto const axis = int(node.splitAxis);
		auto const split = node.box.Min()[axis] + node.box.Sizes()[axis] / 2;

		auto const low = node.GetLowChild();
		auto const middle = node.boxData.middleChild;
		auto const high = node.GetHighChild();

		if (low >= 0)
		{
			REQUIRE(nodes[low].box.Max()[axis] < split);
		}

		if (middle >= 0)
		{
			REQUIRE(nodes[middle].box.Min()[axis] < split);
			REQUIRE(nodes[middle].box.Max()[axis] >= split);
		}

		if (high >= 0)
		{
			REQUIRE(nodes[high].box.Min()[axis] >= split);
		}
	}
}

TEST_CASE("KdBoxTree_QueryCorrectness")
{
	Test_KdBoxTree_QueryCorrectness<false>(Vector2{});
	Test_KdBoxTree_QueryCorrectness<false>(Box2{});
	Test_KdBoxTree_QueryCorrectness<true>(Vector2{});
	Test_KdBoxTree_QueryCorrectness<true>(Box2{});
}
