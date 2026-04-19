// Copyright 2024-2026 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include "SpatialIndexWrapper.hpp"

#ifndef ENABLE_ALGLIB

template <typename TSpatialKey>
struct AlglibKdtree : SpatialIndexWrapper<TSpatialKey>
{
};

#else

#include "TestTools.hpp"
#include "GeoToolbox/SpatialTools.hpp"

#include <alglib-cpp/src/alglibmisc.h>

// TODO: Convert from float to double for float keys
template <typename TSpatialKey, bool IsSupported = GeoToolbox::SpatialKeyIsPoint<TSpatialKey> && std::is_same_v<typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::ScalarType, double>>
struct AlglibKdtree : SpatialIndexWrapper<TSpatialKey>
{
	static constexpr auto Dimensions = GeoToolbox::SpatialKeyTraits<TSpatialKey>::Dimensions;

	using VectorType = typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::VectorType;

	using DVector = GeoToolbox::Vector<double, GeoToolbox::SpatialKeyTraits<TSpatialKey>::Dimensions>;

	using BoxType = GeoToolbox::Box<TSpatialKey>;

	using FeaturePtr = GeoToolbox::Feature<TSpatialKey> const*;


	[[nodiscard]] std::string_view Name() const override
	{
		return "Alglib K-d Tree";
	}

	[[nodiscard]] std::string GetIndexStats(std::shared_ptr<void> const& indexPtr) const override
	{
		auto& index = *static_cast<alglib::kdtree const*>(indexPtr.get());
		std::ostringstream stream;
		stream << "Nodes: " << index.c_ptr()->nodes.cnt;
		return stream.str();
	}

	[[nodiscard]] std::shared_ptr<void> MakeEmptyIndex() const override
	{
		return std::make_shared<alglib::kdtree>();
	}

	[[nodiscard]] std::shared_ptr<void> Load(Dataset<TSpatialKey> const& dataset) const override
	{
		if (dataset.IsEmpty())
		{
			return {};
		}

		auto const ids = dataset.GetIds();
		alglib::integer_1d_array tags;
		tags.setcontent(GeoToolbox::Size(ids), ids.data());

		constexpr auto dimensions = GeoToolbox::SpatialKeyTraits<TSpatialKey>::Dimensions;
		auto keys = dataset.GetKeys();
		alglib::real_2d_array coords;
		static_assert(std::is_same_v<typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::ScalarType, double>, "Need to copy+convert to double");
		coords.attach_to_ptr(GeoToolbox::Size(keys), dimensions, &keys[0][0]);

		auto result = std::make_shared<alglib::kdtree>();
		alglib::kdtreebuildtagged(coords, tags, dimensions, 0, 2, *result);
		return result;
	}

	[[nodiscard]] int QueryBox(std::shared_ptr<void> const& indexPtr, BoxType const& queryBox) const override
	{
		if (indexPtr == nullptr)
		{
			return 0;
		}

		auto& index = *static_cast<alglib::kdtree const*>(indexPtr.get());

		alglib::kdtreerequestbuffer buffer;
		alglib::kdtreecreaterequestbuffer(index, buffer);

		alglib::real_1d_array boxmin;
		auto min = GeoToolbox::Convert<DVector>(queryBox.Min());
		auto max = GeoToolbox::Convert<DVector>(queryBox.Max());
		boxmin.setcontent(GeoToolbox::SpatialKeyTraits<TSpatialKey>::Dimensions, &min[0]);
		alglib::real_1d_array boxmax;
		boxmax.setcontent(GeoToolbox::SpatialKeyTraits<TSpatialKey>::Dimensions, &max[0]);

		return int(alglib::kdtreetsquerybox(index, buffer, boxmin, boxmax));
	}

	[[nodiscard]] double QueryNearest(std::shared_ptr<void> const& indexPtr, VectorType const& location, int nearestCount) const override
	{
		if (indexPtr == nullptr)
		{
			return 0;
		}

		auto& index = *static_cast<alglib::kdtree const*>(indexPtr.get());

		alglib::kdtreerequestbuffer buffer;
		alglib::kdtreecreaterequestbuffer(index, buffer);

		auto dlocation = GeoToolbox::Convert<DVector>(location);
		alglib::real_1d_array x;
		x.setcontent(GeoToolbox::SpatialKeyTraits<TSpatialKey>::Dimensions, &dlocation[0]);

		// This retrieves real distances, while we need squared
		//alglib::real_1d_array distances;
		//alglib::kdtreetsqueryresultsdistances(index, buffer, distances);
		//return std::accumulate(distances.getcontent(), distances.getcontent() + count, 0.0);

		auto const count = alglib::kdtreetsqueryknn(index, buffer, x, nearestCount);
		auto const begin = buffer.c_ptr()->r.ptr.p_double;
		return std::accumulate(begin, begin + count, 0.0);
	}
};

// Turn off for box keys
template <typename TSpatialKey>
struct AlglibKdtree<TSpatialKey, false> : SpatialIndexWrapper<TSpatialKey>
{
};

#endif
