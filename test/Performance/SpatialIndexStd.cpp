// Copyright 2024-2026 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "SpatialIndexWrapper.hpp"

using namespace GeoToolbox;
using namespace std;

template <typename TSpatialKey>
shared_ptr<void> StdVector<TSpatialKey>::Load(Dataset<TSpatialKey> const& dataset) const
{
	auto const data = dataset.GetData();
	if (data.size() > 100'000)
	{
		// Too large datasets take too much time
		return nullptr;
	}

	return make_shared<IndexType>( ValueIterator{ data.data() }, ValueIterator{ data.data() + data.size() } );
}

template <typename TSpatialKey>
void StdVector<TSpatialKey>::Insert(std::shared_ptr<void> const& indexPtr, FeaturePtr feature) const
{
	auto& index = *static_cast<IndexType*>(indexPtr.get());
	DEBUG_ASSERT(!Contains(index, feature));
	index.push_back(feature);
}

template <typename TSpatialKey>
bool StdVector<TSpatialKey>::Erase(std::shared_ptr<void> const& indexPtr, FeaturePtr feature) const
{
	auto& index = *static_cast<IndexType*>(indexPtr.get());
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

template struct StdVector<Vector2>;
template struct StdVector<Vector3f>;
template struct StdVector<Box2>;
template struct StdVector<Box3f>;
#if defined( ENABLE_EIGEN )
template struct StdVector<EVector2>;
template struct StdVector<Box<EVector2>>;
#endif
