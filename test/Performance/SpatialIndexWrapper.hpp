// Copyright 2024-2026 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include "TestTools.hpp"

#include <memory>
#include <random>

// This class defines the common interface for spatial index wrappers
template <typename TSpatialKey>
struct SpatialIndexWrapper
{
	using VectorType = typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::VectorType;

	using BoxType = typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::BoxType;

	using FeaturePtr = GeoToolbox::Feature<TSpatialKey> const*;


	virtual ~SpatialIndexWrapper() = default;

	// Put readable spatial index name here, without tab characters. If this returns an empty string, the wrapper is considered disabled
	[[nodiscard]] virtual std::string_view Name() const
	{
		return {};
	}

	// Set IsDynamic to true if the spatial index supports removing elements through the Erase() method
	[[nodiscard]] virtual bool IsDynamic() const
	{
		return false;
	}

	[[nodiscard]] virtual bool SupportsDatasetSize(int /*size*/) const
	{
		return true;
	}

	[[nodiscard]] virtual std::string GetIndexStats(std::shared_ptr<void> const& /*spatialIndex*/) const
	{
		return {};
	}

	[[nodiscard]] virtual std::shared_ptr<void> MakeEmptyIndex() const
	{
		return {};
	}

	[[nodiscard]] virtual std::shared_ptr<void> Load(Dataset<TSpatialKey> const& /*dataset*/) const
	{
		return {};
	}

	virtual void Insert(std::shared_ptr<void> const& /*spatialIndex*/, FeaturePtr /*feature*/) const
	{
	}

	virtual bool Erase(std::shared_ptr<void> const& /*spatialIndex*/, FeaturePtr /*feature*/) const
	{
		return false;
	}

	// Some indices (Spatial++ "idle" trees) need explicit re-balancing after insert/erase
	virtual void Rebalance(std::shared_ptr<void> const& /*spatialIndex*/) const
	{
	}

	// Return the count of the features found to intersect the box. Return negative value if this query is not supported
	[[nodiscard]] virtual int QueryBox(std::shared_ptr<void> const& /*spatialIndex*/, BoxType const& /*box*/) const
	{
		return -1;
	}

	// Return the sum of the squared distances to the nearest found features. This is more reliable than the feature ids, as different features may be returned if they are at the same distance.
	// Return negative value if this query is not supported
	[[nodiscard]] virtual double QueryNearest(std::shared_ptr<void> const& /*spatialIndex*/, VectorType const& /*location*/, int /*nearestCount*/) const
	{
		return -1;
	}
};


// Implementation using std containers, mostly vector

template <typename TSpatialKey, class TContainer>
struct StdContainer : SpatialIndexWrapper<TSpatialKey>
{
	using VectorType = typename SpatialIndexWrapper<TSpatialKey>::VectorType;
	using BoxType = typename SpatialIndexWrapper<TSpatialKey>::BoxType;
	using FeaturePtr = typename SpatialIndexWrapper<TSpatialKey>::FeaturePtr;

	using IndexType = TContainer;


	[[nodiscard]] bool IsDynamic() const override
	{
		return true;
	}

	[[nodiscard]] virtual bool SupportsDatasetSize(int size) const override
	{
		return size <= 100'000;
	}

	[[nodiscard]] std::shared_ptr<void> MakeEmptyIndex() const override
	{
		return std::make_shared<IndexType>();
	}

	[[nodiscard]] int QueryBox(std::shared_ptr<void> const& indexPtr, BoxType const& box) const override
	{
		auto& index = *static_cast<IndexType const*>(indexPtr.get());
		return GeoToolbox::/*Parallel*/CountIf(index, [&box](auto&& feature)
			{
				GeoToolbox::AddQueryStats_ObjectTestsCount();
				GeoToolbox::AddQueryStats_BoxOverlapsCount();
				return Overlap(box, feature->spatialKey);
			});
	}

	[[nodiscard]] double QueryNearest(std::shared_ptr<void> const& indexPtr, VectorType const& location, int nearestCount) const override
	{
		using ScalarType = typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::ScalarType;

		auto& index = *static_cast<IndexType const*>(indexPtr.get());
		ASSERT(nearestCount > 0);
		std::vector nearest(nearestCount, std::pair(GeoToolbox::FeatureId{ 0 }, std::numeric_limits<ScalarType>::max()));
		auto comparer = [](std::pair<GeoToolbox::FeatureId, ScalarType> const& a, std::pair<GeoToolbox::FeatureId, ScalarType> const& b)
			{
				return a.second < b.second;
			};

		for (auto const& feature : index)
		{
			GeoToolbox::AddQueryStats_ObjectTestsCount();
			GeoToolbox::AddQueryStats_ScalarComparisonsCount();
			auto const distance2 = GeoToolbox::GetDistanceSquared(location, feature->spatialKey);
			if (distance2 < nearest.back().second)
			{
				auto const record = std::pair(feature->id, distance2);
				auto position = lower_bound(nearest.begin(), nearest.end(), record, comparer);
				nearest.insert(position, record);
				nearest.erase(--nearest.end());
			}
		}

		auto const distSum = std::accumulate(nearest.begin(), nearest.end(), 0.0, [](double sum, auto const& f) { return sum + double(f.second); });

		//cout << Name << " nearest result for location " << location << ": " << distSum << " -> ";
		//PrintNearest(nearest);

		return distSum;
	}
};

template <typename TSpatialKey>
struct StdVector : StdContainer<TSpatialKey, std::vector<GeoToolbox::Feature<TSpatialKey> const*>>
{
	using FeaturePtr = GeoToolbox::Feature<TSpatialKey> const*;
	using IndexType = std::vector<FeaturePtr>;


	[[nodiscard]] std::string_view Name() const override
	{
		return "std::vector";
	}

	using BaseType = StdContainer<TSpatialKey, IndexType>;


	std::shared_ptr<void> Load(Dataset<TSpatialKey> const& dataset) const override;

	void Insert(std::shared_ptr<void> const& indexPtr, FeaturePtr) const override;

	bool Erase(std::shared_ptr<void> const& indexPtr, FeaturePtr) const override;
};
