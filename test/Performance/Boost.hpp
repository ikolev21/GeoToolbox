// Copyright 2024-2025 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#ifndef ENABLE_BOOST

template <typename TSpatialKey>
struct BoostRtree
{
	using IndexType = void;
};

#else

#include "TestTools.hpp"
#include "GeoToolbox/Iterators.hpp"
#include "GeoToolbox/SpatialTools.hpp"

#pragma GCC system_header
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/adapted/std_array.hpp>
#include <boost/geometry/geometries/register/box.hpp>
#include <boost/geometry/geometries/register/point.hpp>
#include <boost/geometry/index/rtree.hpp>

#pragma GCC diagnostic pop

namespace Bgi = boost::geometry::index;

BOOST_GEOMETRY_REGISTER_STD_ARRAY_CS(cs::cartesian)
BOOST_GEOMETRY_REGISTER_BOX(GeoToolbox::Box<GeoToolbox::Vector2>, GeoToolbox::Vector2, Min(), Max())
#if defined( ENABLE_EIGEN )
BOOST_GEOMETRY_REGISTER_POINT_2D(GeoToolbox::EVector2, double, cs::cartesian, operator[](0), operator[](1))
BOOST_GEOMETRY_REGISTER_BOX(GeoToolbox::Box<GeoToolbox::EVector2>, GeoToolbox::EVector2, Min(), Max())
#endif

template <typename TSpatialKey>
struct Bgi::indexable<GeoToolbox::Feature<TSpatialKey> const*>
{
	// ReSharper disable once CppInconsistentNaming
	using result_type = TSpatialKey;

	result_type operator()(GeoToolbox::Feature<TSpatialKey> const* v) const
	{
		return v->spatialKey;
	}
};


template <typename TSpatialKey>
struct BoostRtree
{
	static constexpr std::string_view Name = "Boost R-tree " BOOST_LIB_VERSION;

	static constexpr auto IsDynamic = true;

	using VectorType = typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::VectorType;

	using BoxType = typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::BoxType;

	using FeaturePtr = GeoToolbox::Feature<TSpatialKey> const*;

	using AllocatorType = GeoToolbox::ProfileAllocator<FeaturePtr, boost::container::new_allocator<FeaturePtr>>;

	using IndexType = Bgi::rtree<
		FeaturePtr,
		Bgi::rstar<GeoToolbox::MaxElementsPerNode>,
		Bgi::indexable<FeaturePtr>,
		Bgi::equal_to<FeaturePtr>,
		AllocatorType>;

	static IndexType MakeEmptyIndex(GeoToolbox::SharedAllocatedSize allocatorStats)
	{
		return IndexType{
			Bgi::rstar<GeoToolbox::MaxElementsPerNode>{},
			Bgi::indexable<FeaturePtr>{},
			Bgi::equal_to<FeaturePtr>{},
			AllocatorType{ std::move(allocatorStats) } };
	}

	static IndexType Load(Dataset<TSpatialKey> const& dataset, GeoToolbox::SharedAllocatedSize allocatorStats)
	{
		auto const data = dataset.GetData();
		return { GeoToolbox::ValueIterator{ data.data() }, GeoToolbox::ValueIterator{ data.data() + data.size() }, AllocatorType{ std::move(allocatorStats) } };
	}

	static void Insert(IndexType& index, FeaturePtr feature)
	{
		index.insert(feature);
	}

	static bool Erase(IndexType& index, FeaturePtr feature)
	{
		return index.remove(feature) == 1;
	}

	static void Rebalance(IndexType&)
	{
	}

	static int QueryBox(IndexType const&, BoxType const& queryBox);

	static double QueryNearest(IndexType const&, VectorType const& location, int nearestCount);
};

#endif
