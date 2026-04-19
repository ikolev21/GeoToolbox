// Copyright 2024-2026 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#ifndef ENABLE_BOOST

template <typename TSpatialKey>
struct BoostRtree : SpatialIndexWrapper<TSpatialKey>
{
};

#else

#include "SpatialIndexWrapper.hpp"
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
BOOST_GEOMETRY_REGISTER_BOX(GeoToolbox::Box<GeoToolbox::Vector3f>, GeoToolbox::Vector3f, Min(), Max())
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
struct BoostRtree : SpatialIndexWrapper<TSpatialKey>
{
	using VectorType = typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::VectorType;

	using BoxType = typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::BoxType;

	using FeaturePtr = GeoToolbox::Feature<TSpatialKey> const*;

	using IndexType = Bgi::rtree<FeaturePtr, Bgi::rstar<GeoToolbox::MaxElementsPerNode>>;

	[[nodiscard]] std::string_view Name() const override
	{
		return "Boost " BOOST_LIB_VERSION " R-tree";
	}

	[[nodiscard]] bool IsDynamic() const override
	{
		return true;
	}

	[[nodiscard]] std::shared_ptr<void> MakeEmptyIndex() const override
	{
		return std::make_shared<IndexType>(Bgi::rstar<GeoToolbox::MaxElementsPerNode>{});
	}

	[[nodiscard]] std::shared_ptr<void> Load(Dataset<TSpatialKey> const& dataset) const override
	{
		auto const data = dataset.GetData();
		return std::make_shared<IndexType>(GeoToolbox::ValueIterator{ data.data() }, GeoToolbox::ValueIterator{ data.data() + data.size() });
	}

	void Insert(std::shared_ptr<void> const& indexPtr, FeaturePtr feature) const override
	{
		auto& index = *static_cast<IndexType*>(indexPtr.get());

		index.insert(feature);
	}

	bool Erase(std::shared_ptr<void> const& indexPtr, FeaturePtr feature) const override
	{
		auto& index = *static_cast<IndexType*>(indexPtr.get());

		return index.remove(feature) == 1;
	}

	[[nodiscard]] int QueryBox(std::shared_ptr<void> const& indexPtr, BoxType const& box) const override;

	[[nodiscard]] double QueryNearest(std::shared_ptr<void> const& indexPtr, VectorType const& location, int nearestCount) const override;
};

#endif
