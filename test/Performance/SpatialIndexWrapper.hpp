// Copyright 2024-2025 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include "TestTools.hpp"
#include "GeoToolbox/Iterators.hpp"

//#pragma warning( push )
//
//#pragma warning( disable : 4244 )
//#include <hash_set8.hpp>
//
//#pragma warning( pop )

#include <random>
#include <set>

// This class defines the common interface for spatial index wrappers
template <typename TSpatialKey>
struct SpatialIndexWrapper
{
	static constexpr std::string_view Name = "Put readable index name here, without tab characters";

	// Set IsDynamic to true if the index supports removing elements through the Erase() method
	static constexpr auto IsDynamic = true;

	using VectorType = typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::VectorType;

	using BoxType = typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::BoxType;

	using FeaturePtr = GeoToolbox::Feature<TSpatialKey> const*;

	// Define the spatial index type
	using IndexType = std::vector<FeaturePtr, GeoToolbox::ProfileAllocator<FeaturePtr>>;

	static IndexType MakeEmptyIndex(GeoToolbox::SharedAllocatedSize /*allocatorStats*/)
	{
		return {};
	}

	static IndexType Load(Dataset<TSpatialKey> const& /*dataset*/, GeoToolbox::SharedAllocatedSize /*allocatorStats*/)
	{
		return {};
	}

	// Some indices (Spatial++ "idle" trees) need explicit re-balancing after insert/erase
	static void Rebalance(IndexType&)
	{
	}

	// Return the count of the features found to intersect the box. Return negative value if this query is not supported
	static int QueryBox(IndexType const& /*index*/, BoxType const& /*box*/)
	{
		return -1;
	}

	// Return the sum of the squared distances to the nearest found features. This is more reliable than the feature ids, as different features may be returned if they are at the same distance.
	// Return negative value if this query is not supported
	static double QueryNearest(IndexType const& /*index*/, VectorType const& /*location*/, int /*nearestCount*/)
	{
		return -1;
	}
};

// Implementation using std::vector and std::unordered_set

template <typename TSpatialKey, class TContainer>
struct StdContainer
{
	static constexpr std::string_view Name = "Put readable index name here, without tab characters";

	// Set IsDynamic to true if the index supports removing elements by the Erase() method
	static constexpr auto IsDynamic = true;

	using VectorType = typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::VectorType;

	using BoxType = typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::BoxType;

	using FeaturePtr = GeoToolbox::Feature<TSpatialKey> const*;

	using IndexType = TContainer;

	using AllocatorType = typename TContainer::allocator_type;

	static IndexType MakeEmptyIndex(GeoToolbox::SharedAllocatedSize allocatorStats)
	{
		return IndexType{ AllocatorType{ std::move(allocatorStats) } };
	}

	// Some indices (Spatial++ "idle" trees) need explicit re-balancing after insert/erase
	static void Rebalance(IndexType&)
	{
	}

	// Return the count of the features found to intersect the box. Return negative value if this query is not supported
	static int QueryBox(IndexType const& index, BoxType const& box)
	{
		return GeoToolbox::/*Parallel*/CountIf(index, [&box](auto&& feature)
			{
				GeoToolbox::AddQueryStats_ObjectOverlapsCount();
				return Overlap(box, feature->spatialKey);
			});
	}

	// Return the sum of the squared distances to the nearest found features. This is more reliable than the feature ids, as different features may be returned if they are at close distance
	// Return negative value if this query is not supported
	static double QueryNearest(IndexType const& index, VectorType const& location, int nearestCount)
	{
		ASSERT(nearestCount > 0);
		std::vector nearest(nearestCount, std::pair(FeaturePtr{ nullptr }, std::numeric_limits<double>::max()));
		auto comparer = [](std::pair<FeaturePtr, double> const& a, std::pair<FeaturePtr, double> const& b)
			{
				return a.second < b.second;
			};

		for (auto const& feature : index)
		{
			auto distance2 = GeoToolbox::GetDistanceSquared(location, feature->spatialKey);
			if (distance2 < nearest.back().second)
			{
				auto const record = std::pair(feature, distance2);
				auto position = lower_bound(nearest.begin(), nearest.end(), record, comparer);
				nearest.insert(position, record);
				nearest.erase(--nearest.end());
			}
		}

		auto const distSum = std::accumulate(nearest.begin(), nearest.end(), 0.0, [](double sum, auto const& f) { return sum + f.second; });

		//cout << Name << " nearest result for location " << location << ": " << distSum << " -> ";
		//PrintNearest(nearest);

		return distSum;
	}
};

template <typename TSpatialKey, typename FeaturePtr = GeoToolbox::Feature<TSpatialKey> const*>
struct StdVector : StdContainer<TSpatialKey, std::vector<FeaturePtr, GeoToolbox::ProfileAllocator<FeaturePtr>>>
{
	static constexpr std::string_view Name = "std::vector";

	using IndexType = std::vector<FeaturePtr, GeoToolbox::ProfileAllocator<FeaturePtr>>;

	using BaseType = StdContainer<TSpatialKey, IndexType>;


	static IndexType Load(Dataset<TSpatialKey> const& dataset, GeoToolbox::SharedAllocatedSize allocatorStats);

	static void Insert(IndexType& index, FeaturePtr feature);

	static bool Erase(IndexType& index, FeaturePtr feature);
};

template <
	typename TSpatialKey,
	typename FeaturePtr = GeoToolbox::Feature<TSpatialKey> const*,
	class TElementAllocator = GeoToolbox::ProfileAllocator<FeaturePtr*, GeoToolbox::ListPoolAllocatorSync<FeaturePtr>>>
struct StdHashset : StdContainer<TSpatialKey, std::unordered_set<FeaturePtr, std::hash<FeaturePtr>, std::equal_to<FeaturePtr>, TElementAllocator>>
{
	static constexpr std::string_view Name = "std::unordered_set";

	using IndexType = std::unordered_set<FeaturePtr, std::hash<FeaturePtr>, std::equal_to<FeaturePtr>, TElementAllocator>;
	using BaseType = StdContainer<TSpatialKey, IndexType>;

	static IndexType Load(Dataset<TSpatialKey> const& dataset, GeoToolbox::SharedAllocatedSize allocatorStats)
	{
		auto const data = dataset.GetData();
		return IndexType{ GeoToolbox::ValueIterator{ data.data() }, GeoToolbox::ValueIterator{ data.data() + data.size() }, 0, TElementAllocator{ std::move(allocatorStats) } };
	}

	static void Insert(IndexType& index, FeaturePtr feature)
	{
		DEBUG_ASSERT(!Contains(index, feature));
		index.insert(feature);
	}

	static bool Erase(IndexType& index, FeaturePtr feature)
	{
		return index.erase(feature) > 0;
	}
};

template <
	typename TSpatialKey,
	typename FeaturePtr = GeoToolbox::Feature<TSpatialKey> const*,
	class TElementAllocator = GeoToolbox::ProfileAllocator<FeaturePtr, GeoToolbox::ListPoolAllocatorSync<FeaturePtr>>>
struct StdSet : StdContainer<TSpatialKey, std::set<FeaturePtr, std::less<FeaturePtr>, TElementAllocator>>
{
	static constexpr std::string_view Name = "std::set";

	using IndexType = std::set<FeaturePtr, std::less<FeaturePtr>, TElementAllocator>;
	using BaseType = StdContainer<TSpatialKey, IndexType>;

	static IndexType Load(Dataset<TSpatialKey> const& dataset, GeoToolbox::SharedAllocatedSize allocatorStats)
	{
		auto const data = dataset.GetData();
		return IndexType{ GeoToolbox::ValueIterator{ data.data() }, GeoToolbox::ValueIterator{ data.data() + data.size() }, TElementAllocator{ std::move(allocatorStats) } };
	}

	static void Insert(IndexType& index, FeaturePtr feature)
	{
		DEBUG_ASSERT(!Contains(index, feature));
		index.insert(feature);
	}

	static bool Erase(IndexType& index, FeaturePtr feature)
	{
		return index.erase(feature) > 0;
	}
};

#if 0

template <
	typename TSpatialKey,
	typename FeaturePtr = GeoToolbox::Feature<TSpatialKey> const*>
struct EmhashSet8 : StdContainer<TSpatialKey, emhash8::HashSet<FeaturePtr>>
{
	static constexpr std::string_view Name = "EmhashSet8";

	using IndexType = emhash8::HashSet<FeaturePtr>;
	using BaseType = StdContainer<TSpatialKey, IndexType>;

	static IndexType MakeEmptyIndex(GeoToolbox::SharedAllocatedSize)
	{
		return IndexType{};
	}

	static IndexType Load(Dataset<TSpatialKey> const& dataset, GeoToolbox::SharedAllocatedSize)
	{
		auto const data = dataset.GetData();
		return IndexType{ GeoToolbox::ValueIterator{ data.data() }, GeoToolbox::ValueIterator{ data.data() + data.size() } };
	}

	static void Insert(IndexType& index, FeaturePtr feature)
	{
		DEBUG_ASSERT(!Contains(index, feature));
		index.insert(feature);
	}

	static bool Erase(IndexType& index, FeaturePtr feature)
	{
		return index.erase(feature) > 0;
	}
};

#endif
