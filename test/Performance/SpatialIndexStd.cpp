// Copyright 2024 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "SpatialIndexWrapper.hpp"

using namespace GeoToolbox;
using namespace std;

template <typename TSpatialKey, typename FeaturePtr>
bool StdVector<TSpatialKey, FeaturePtr>::Erase(IndexType& index, FeaturePtr feature)
{
	auto const location = Find(index, feature);
	if (location == index.end())
	{
		return false;
	}

	auto const last = --index.end();
	if (location != last)
	{
		std::swap(*location, *last);
	}

	index.erase(last);
	return true;
}

template <typename TSpatialKey, typename FeaturePtr>
auto StdVector<TSpatialKey, FeaturePtr>::Load(Dataset<TSpatialKey> const& dataset, SharedAllocatedSize allocatorStats) -> IndexType
{
	auto const data = dataset.GetData();
	return { SelfIterator{ data.begin() }, SelfIterator{ data.end() }, ProfileAllocator<FeaturePtr>{ std::move(allocatorStats) } };
}

template <typename TSpatialKey, typename FeaturePtr>
void StdVector<TSpatialKey, FeaturePtr>::Insert(IndexType& index, FeaturePtr feature)
{
	DEBUG_ASSERT(!Contains(index, feature));
	index.push_back(feature);
}

template struct StdVector<Vector2>;
template struct StdVector<Box2>;
#if defined( ENABLE_EIGEN )
template struct StdVector<EVector2>;
template struct StdVector<Box<EVector2>>;
#endif
