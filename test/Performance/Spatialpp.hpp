// Copyright 2024-2025 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#ifndef ENABLE_SPATIALCPP

template <typename TSpatialKey>
struct SpatialppKdtree
{
	using IndexType = void;
};

#else

#pragma GCC system_header

#include "TestTools.hpp"
#include "GeoToolbox/Iterators.hpp"
#include "GeoToolbox/SpatialTools.hpp"

#include <spatial_2.1.8/src/box_multiset.hpp>
#include <spatial_2.1.8/src/idle_box_multiset.hpp>
#include <spatial_2.1.8/src/idle_point_multiset.hpp>
#include <spatial_2.1.8/src/neighbor_iterator.hpp>


template <typename TSpatialKey, bool IsPoint = GeoToolbox::SpatialKeyIsPoint<TSpatialKey>>
struct SpatialppKeyCompare
{
	bool operator()(spatial::dimension_type n, GeoToolbox::Feature<TSpatialKey> const* x, GeoToolbox::Feature<TSpatialKey> const* y) const
	{
		return x->spatialKey[n] < y->spatialKey[n];
	}

	bool operator()(spatial::dimension_type a, GeoToolbox::Feature<TSpatialKey> const* x, spatial::dimension_type b, GeoToolbox::Feature<TSpatialKey> const* y) const
	{
		return x->spatialKey[a] < y->spatialKey[b];
	}
};

template <typename TSpatialKey>
struct SpatialppKeyCompare<TSpatialKey, false>
{
	static constexpr auto Dimensions = GeoToolbox::SpatialKeyTraits<TSpatialKey>::Dimensions;

	bool operator()(spatial::dimension_type n, GeoToolbox::Feature<TSpatialKey> const* x, GeoToolbox::Feature<TSpatialKey> const* y) const
	{
		return n < Dimensions ? x->spatialKey.Min()[n] < y->spatialKey.Min()[n] : x->spatialKey.Max()[n - Dimensions] < y->spatialKey.Max()[n - Dimensions];
	}

	bool operator()(spatial::dimension_type a, GeoToolbox::Feature<TSpatialKey> const* x, spatial::dimension_type b, GeoToolbox::Feature<TSpatialKey> const* y) const
	{
		return ( a < Dimensions ? x->spatialKey.Min()[a] : x->spatialKey.Max()[a - Dimensions] ) < (b < Dimensions ? y->spatialKey.Min()[b] : y->spatialKey.Max()[b - Dimensions]);
	}
};

template <typename TSpatialKey, bool IsPoint = GeoToolbox::SpatialKeyIsPoint<TSpatialKey>>
struct SpatialppKdtree
{
	static constexpr std::string_view Name = "Spatial++ K-d Tree " MAKE_STRING(SPATIAL_VERSION_MAJOR) "." MAKE_STRING(SPATIAL_VERSION_MINOR) "." MAKE_STRING(SPATIAL_VERSION_RELEASE);

	static constexpr auto Dimensions = GeoToolbox::SpatialKeyTraits<TSpatialKey>::Dimensions;

	static constexpr auto IsDynamic = true;

	using BoxType = GeoToolbox::Box<TSpatialKey>;

	using FeaturePtr = GeoToolbox::Feature<TSpatialKey> const*;

	using AllocatorType = GeoToolbox::ProfileAllocator<FeaturePtr>;

	using IndexType = std::conditional_t<IsPoint,
		spatial::idle_point_multiset<Dimensions, FeaturePtr, SpatialppKeyCompare<TSpatialKey>, AllocatorType>,
		spatial::idle_box_multiset<Dimensions * 2, FeaturePtr, SpatialppKeyCompare<TSpatialKey>, AllocatorType>>;

	static IndexType MakeEmptyIndex(GeoToolbox::SharedAllocatedSize allocatorStats)
	{
		return IndexType{ typename IndexType::key_compare{}, AllocatorType{ std::move(allocatorStats) } };
	}

	static IndexType Load(Dataset<TSpatialKey> const& dataset, GeoToolbox::SharedAllocatedSize allocatorStats)
	{
		auto result = MakeEmptyIndex(std::move(allocatorStats));
		auto const data = dataset.GetData();
		result.insert(GeoToolbox::ValueIterator{ data.data() }, GeoToolbox::ValueIterator{ data.data() + data.size() });
		return result;
	}

	static void Insert(IndexType& index, FeaturePtr feature)
	{
		index.insert(feature);
	}

	static bool Erase(IndexType& index, FeaturePtr feature)
	{
		return index.erase(feature) > 0;
	}

	static void Rebalance(IndexType& index)
	{
		index.rebalance();
	}


	static int QueryBox(IndexType const&, BoxType const&);

	static double QueryNearest(IndexType const&, TSpatialKey const& location, int nearestCount);
};

// This turns off Spatial++ for box keys altogether
template <typename TSpatialKey>
struct SpatialppKdtree<TSpatialKey, false>
{
	using IndexType = void;
};

#endif
