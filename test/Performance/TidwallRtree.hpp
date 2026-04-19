// Copyright 2024-2026 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#ifndef ENABLE_TIDWALL_RTREE

template <typename TSpatialKey>
struct TidwallRtree : SpatialIndexWrapper<TSpatialKey>
{
};

#else

#include "SpatialIndexWrapper.hpp"
#include "TestTools.hpp"
#include "GeoToolbox/Profiling.hpp"
#include "GeoToolbox/SpatialTools.hpp"

extern "C"
{
#include <tidwallrtree-src/rtree.h>
}

namespace Tidwall
{
	// Copied from rtree.c

	using DATATYPE = void*;
	using NUMTYPE = double;
	typedef int rc_t;
	constexpr auto MAXITEMS = 64;
	enum kind {
		LEAF = 1,
		BRANCH = 2,
	};
	struct rect {
		NUMTYPE min[ENABLE_TIDWALL_RTREE];
		NUMTYPE max[ENABLE_TIDWALL_RTREE];
	};
	struct item {
		const DATATYPE data;
	};
	struct node {
		rc_t rc;            // reference counter for copy-on-write
		enum kind kind;     // LEAF or BRANCH
		int count;          // number of rects
		struct rect rects[MAXITEMS];
		union {
			struct node* nodes[MAXITEMS];
			struct item datas[MAXITEMS];
		};
	};

	struct rtree {
		struct rect rect;
		struct node* root;
		size_t count;
		size_t height;
	#ifdef USE_PATHHINT
		int path_hint[16];
	#endif
		bool relaxed;
		void* (*malloc)(size_t);
		void (*free)(void*);
		void* udata;
		bool (*item_clone)(const DATATYPE item, DATATYPE* into, void* udata);
		void (*item_free)(const DATATYPE item, void* udata);
	};
}

struct RtreeDeleter
{
	void operator()(rtree* tree) const
	{
		rtree_free(tree);
	}
};

template <typename TSpatialKey, bool DimensionsMatch = GeoToolbox::SpatialKeyTraits<TSpatialKey>::Dimensions == ENABLE_TIDWALL_RTREE>
struct TidwallRtree : SpatialIndexWrapper<TSpatialKey>
{
	static constexpr auto Dimensions = 2;

	using VectorType = typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::VectorType;
	using VectorTypeDouble = typename GeoToolbox::VectorTraits<VectorType>::template Reconfigure<double, 3>;

	using BoxType = typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::BoxType;
	using BoxTypeDouble = GeoToolbox::Box<VectorTypeDouble>;

	using FeaturePtr = GeoToolbox::Feature<TSpatialKey> const*;

	using IndexType = rtree;


	[[nodiscard]] std::string_view Name() const override
	{
		return "Tidwall R-Tree";
	}

	[[nodiscard]] bool IsDynamic() const override
	{
		return true;
	}

	[[nodiscard]] std::shared_ptr<void> MakeEmptyIndex() const override
	{
		auto result = std::shared_ptr<rtree>(rtree_new_with_allocator(TrackedMalloc, TrackedFree), RtreeDeleter{});
		rtree_opt_relaxed_atomics(result.get());
		return result;
	}

	[[nodiscard]] std::string GetIndexStats(std::shared_ptr<void> const& indexPtr) const override
	{
		GeoToolbox::AggregateStats<int> elementsPerNode;
		std::queue<Tidwall::node*> queue;
		auto const rtree = static_cast<Tidwall::rtree*>(indexPtr.get());
		queue.emplace(rtree->root);
		auto nodeCount = 0;

		while (!queue.empty())
		{
			auto const node = queue.front();
			queue.pop();
			++nodeCount;
			if (node->kind == Tidwall::BRANCH)
			{
				for (auto i = 0; i < node->count; ++i)
				{
					queue.emplace(node->nodes[i]);
				}
			}
			else
			{
				elementsPerNode.AddValue(node->count);
			}
		}

		std::ostringstream stream;
		stream << "Nodes: " << nodeCount << " Elems/Leaf: " << elementsPerNode;
		stream << " Height: " << rtree->height;
		return stream.str();
	}

	[[nodiscard]] std::shared_ptr<void> Load(Dataset<TSpatialKey> const& dataset) const override
	{
		auto index = MakeEmptyIndex();
		for (auto const& f : dataset.GetData())
		{
			Insert(index, &f);
		}

		return index;
	}

	void Insert(std::shared_ptr<void> const& indexPtr, FeaturePtr feature) const override
	{
		auto index = static_cast<rtree*>(indexPtr.get());

		if constexpr (GeoToolbox::SpatialKeyIsPoint<TSpatialKey>)
		{
			auto const key = GeoToolbox::Convert<VectorTypeDouble>(feature->spatialKey);
			rtree_insert(index, &key[0], nullptr, feature);
		}
		else
		{
			auto const min = GeoToolbox::Convert<VectorTypeDouble>(feature->spatialKey.Min());
			auto const max = GeoToolbox::Convert<VectorTypeDouble>(feature->spatialKey.Max());
			rtree_insert(index, &min[0], &max[0], feature);
		}
	}

	bool Erase(std::shared_ptr<void> const& indexPtr, FeaturePtr feature) const override
	{
		auto index = static_cast<rtree*>(indexPtr.get());

		if constexpr (GeoToolbox::SpatialKeyIsPoint<TSpatialKey>)
		{
			auto const key = GeoToolbox::Convert<VectorTypeDouble>(feature->spatialKey);
			rtree_delete(index, &key[0], nullptr, feature);
		}
		else
		{
			auto const min = GeoToolbox::Convert<VectorTypeDouble>(feature->spatialKey.Min());
			auto const max = GeoToolbox::Convert<VectorTypeDouble>(feature->spatialKey.Max());
			rtree_delete(index, &min[0], &max[0], feature);
		}

		return true;
	}

	static bool CountMatches(double const* /*min*/, double const* /*max*/, void const* /*data*/, void* udata)
	{
		++*static_cast<int*>(udata);
		return true;
	}

	[[nodiscard]] int QueryBox(std::shared_ptr<void> const& indexPtr, BoxType const& queryBox) const override
	{
		auto index = static_cast<rtree*>(indexPtr.get());

		auto count = 0;
		auto const queryBoxDouble = BoxTypeDouble::Convert(queryBox);
		rtree_search(index, &queryBoxDouble.Min()[0], &queryBoxDouble.Max()[0], CountMatches, &count);
		return count;
	}
};

template <typename TSpatialKey>
struct TidwallRtree<TSpatialKey, false> : SpatialIndexWrapper<TSpatialKey>
{
};

#endif
