// Copyright 2024-2026 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

/*
Of the various spatial indices implemented in GEOS, geos::index::strtree::TemplateSTRtree is the only that makes sense performance-wise:
* geos::index::kdtree::KdTree was built with the specific purpose to find duplicate points and has poor performance for other tasks, also returns incorrect results in rare cases
* geos::index::quadtree::Quadtree is consistently slower than STR-tree
* geos::index::VertexSequencePackedRtree is a tiny bit faster than STR-tree for the kind of data it was designed for (polygons), and much slower for other kinds of data
All of these have wrappers here, but just TemplateSTRtree is included in the list of indices to test, the others are commented-out in the IndicesToTest definition in SpatialIndexTest.cpp
*/

#include "SpatialIndexWrapper.hpp"

#ifndef ENABLE_GEOS

template <typename TSpatialKey>
struct GeosTemplateStrTree : SpatialIndexWrapper<TSpatialKey>
{
};

template <typename TSpatialKey>
struct GeosTemplateKdTree : SpatialIndexWrapper<TSpatialKey>
{
};

template <typename TSpatialKey>
struct GeosQuadTree : SpatialIndexWrapper<TSpatialKey>
{
};

template <typename TSpatialKey>
struct GeosVertexSequencePackedRtree : SpatialIndexWrapper<TSpatialKey>
{
};

#else

#include "TestTools.hpp"
#include "GeoToolbox/SpatialTools.hpp"

#include <geos/version.h>
#include <geos/index/ItemVisitor.h>
#include <geos/index/VertexSequencePackedRtree.h>
#include <geos/index/kdtree/KdTree.h>
#include <geos/index/quadtree/Quadtree.h>
#include <geos/index/strtree/TemplateSTRtree.h>

template <class TVector>
geos::geom::Envelope ToEnvelope(TVector const& point)
{
	return geos::geom::Envelope{ point[0], point[0], point[1], point[1] };
}

template <class TVector>
geos::geom::Envelope ToEnvelope(GeoToolbox::Box<TVector> const& box)
{
	static_assert(GeoToolbox::VectorTraits<TVector>::Dimensions == 2);
	return geos::geom::Envelope{ box.Min()[0], box.Max()[0], box.Min()[1], box.Max()[1] };
}

template <typename TSpatialKey, bool DimensionsMatch = GeoToolbox::SpatialKeyTraits<TSpatialKey>::Dimensions == 2>
struct GeosTemplateStrTree : SpatialIndexWrapper<TSpatialKey>
{
	using VectorType = typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::VectorType;
	using BoxType = typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::BoxType;
	using FeaturePtr = GeoToolbox::Feature<TSpatialKey> const*;

	using IndexType = geos::index::strtree::TemplateSTRtree<FeaturePtr>;


	[[nodiscard]] std::string_view Name() const override
	{
		return "GEOS " GEOS_VERSION " STR-tree";
	}

	[[nodiscard]] bool IsDynamic() const override
	{
		// TemplateSTRtree does have a remove() operation, but maybe I don't know how to use it properly, because the query crashes later. For now use it as static index.
		return false;
	}

	[[nodiscard]] std::string GetIndexStats(std::shared_ptr<void> const& indexPtr) const override
	{
		auto& index = *static_cast<IndexType const*>(indexPtr.get());

		using namespace geos::index::strtree;
		using Node = TemplateSTRNode<FeaturePtr, EnvelopeTraits>;
		GeoToolbox::AggregateStats<int> elementsPerNode;
		std::queue<std::pair<Node const*, int>> queue;
		// For some reason TemplateSTRtree::getRoot() isn't const
		queue.emplace(const_cast<IndexType&>(index).getRoot(), 1);
		auto nodeCount = 0;
		auto maxHeight = 1;

		while (!queue.empty())
		{
			auto const [node, height] = queue.front();
			queue.pop();
			maxHeight = std::max(maxHeight, height);
			++nodeCount;
			auto elemCount = 0;
			for (auto iter = node->beginChildren(), end = node->endChildren(); iter != end; ++iter)
			{
				if (iter->isLeaf())
				{
					++elemCount;
				}
				else
				{
					queue.emplace(iter, height + 1);
				}
			}

			if (elemCount > 0)
			{
				elementsPerNode.AddValue(elemCount);
			}
		}

		std::ostringstream stream;
		stream << "Nodes: " << nodeCount << " Elems/Leaf: " << elementsPerNode;
		stream << " Height: " << maxHeight;
		return stream.str();
	}

	[[nodiscard]] std::shared_ptr<void> MakeEmptyIndex() const override
	{
		return std::make_shared<IndexType>(GeoToolbox::MaxElementsPerNode);
	}

	[[nodiscard]] std::shared_ptr<void> Load(Dataset<TSpatialKey> const& dataset) const override
	{
		auto indexPtr = MakeEmptyIndex();
		auto& index = *static_cast<IndexType*>(indexPtr.get());

		for (auto const& f : dataset.GetData())
		{
			Insert(indexPtr, &f);
		}

		index.build();
		return indexPtr;
	}

	void Insert(std::shared_ptr<void> const& indexPtr, FeaturePtr feature) const override
	{
		auto& index = *static_cast<IndexType*>(indexPtr.get());
		index.insert(ToEnvelope(feature->spatialKey), feature);
	}

	bool Erase(std::shared_ptr<void> const& /*indexPtr*/, FeaturePtr /*feature*/) const override
	{
		// There is some problem with removal, the query crashes later
		return false;// index.remove(ToEnvelope(feature->spatialKey), feature);
	}

	void Rebalance(std::shared_ptr<void> const& indexPtr) const override
	{
		auto& index = *static_cast<IndexType*>(indexPtr.get());
		index.build();
	}

	[[nodiscard]] int QueryBox(std::shared_ptr<void> const& indexPtr, BoxType const& box) const override
	{
		// For some reason TemplateSTRtree::query() isn't const
		auto& index = *const_cast<IndexType*>( static_cast<IndexType const*>(indexPtr.get()));
		auto count = 0;
		index.query(ToEnvelope(box), [&count](auto)
			{
				++count;
			});
		return count;
	}
};

template <typename TSpatialKey>
struct GeosTemplateStrTree<TSpatialKey, false> : SpatialIndexWrapper<TSpatialKey>
{
};


template <class TSpatialKey, bool DimensionsMatch = GeoToolbox::SpatialKeyTraits<TSpatialKey>::Dimensions == 2>
struct GeosQuadTree : SpatialIndexWrapper<TSpatialKey>
{
	using VectorType = typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::VectorType;

	using BoxType = typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::BoxType;

	using FeaturePtr = GeoToolbox::Feature<TSpatialKey> const*;

	using IndexType = geos::index::quadtree::Quadtree;


	[[nodiscard]] std::string_view Name() const override
	{
		return "GEOS " GEOS_VERSION " Quadtree";
	}

	[[nodiscard]] std::shared_ptr<void> MakeEmptyIndex() const override
	{
		return std::make_shared<IndexType>();
	}

	[[nodiscard]] std::string GetIndexStats(std::shared_ptr<void> const& indexPtr) const override
	{
		auto& index = *static_cast<IndexType*>(indexPtr.get());

		std::ostringstream stream;
		stream << "Height: " << index.depth();
		return stream.str();
	}

	[[nodiscard]] std::shared_ptr<void> Load(Dataset<TSpatialKey> const& dataset) const override
	{
		auto index = std::make_shared<IndexType>();
		for (auto const& f : dataset.GetData())
		{
			Insert(index, &f);
		}

		// query with empty envelope to force construction
		geos::geom::Envelope const emptyEnv;
		std::vector<void*> hits;
		index->query(&emptyEnv, hits);

		return index;
	}

	void Insert(std::shared_ptr<void> const& indexPtr, FeaturePtr feature) const override
	{
		auto& index = *static_cast<IndexType*>(indexPtr.get());

		auto const envelope = ToEnvelope(feature->spatialKey);
		index.insert(&envelope, const_cast<GeoToolbox::Feature<TSpatialKey>*>(feature));
	}

	//bool Erase(std::shared_ptr<void> const& indexPtr, FeaturePtr feature) const override
	//{
	//	auto& index = *static_cast<IndexType*>(indexPtr.get());

	//	return index.remove(ToEnvelope(feature->spatialKey), feature);
	//}

	struct CountingVisitor final : geos::index::ItemVisitor
	{
		BoxType const* queryBox = nullptr;
		int count = 0;

		void visitItem(void* item) override
		{
			auto const feature = static_cast<FeaturePtr>(item);
			if (Overlap(*queryBox, feature->spatialKey))
			{
				++count;
			}
		}
	};

	// Non-const query method??
	[[nodiscard]] int QueryBox(std::shared_ptr<void> const& indexPtr, BoxType const& box) const override
	{
		auto& index = *static_cast<IndexType*>(indexPtr.get());

		auto const envelope = ToEnvelope(box);
		CountingVisitor visitor;
		visitor.queryBox = &box;
		index.query(&envelope, visitor);
		return visitor.count;
	}
};

template <typename TSpatialKey>
struct GeosQuadTree<TSpatialKey, false> : SpatialIndexWrapper<TSpatialKey>
{
};


template <class TSpatialKey, bool IsSupported = GeoToolbox::SpatialKeyIsPoint<TSpatialKey> && GeoToolbox::SpatialKeyTraits<TSpatialKey>::Dimensions == 2>
struct GeosVertexSequencePackedRtree : SpatialIndexWrapper<TSpatialKey>
{
	using VectorType = typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::VectorType;

	using BoxType = typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::BoxType;

	using FeaturePtr = GeoToolbox::Feature<TSpatialKey> const*;

	// VertexSequencePackedRtree references the coordintes in an external container
	using IndexType = std::pair<std::shared_ptr<geos::geom::CoordinateSequence>, geos::index::VertexSequencePackedRtree>;


	[[nodiscard]] std::string_view Name() const override
	{
		return "GEOS " GEOS_VERSION " VertexRtree";
	}

	[[nodiscard]] std::shared_ptr<void> Load(Dataset<TSpatialKey> const& dataset) const override
	{
		auto coordinates = std::make_shared<geos::geom::CoordinateSequence>(geos::geom::CoordinateSequence::XY(dataset.GetSize()));
		auto i = 0;
		for (auto const& f : dataset.GetData())
		{
			DEBUG_ASSERT(f.id == i);
			coordinates->template getAt<geos::geom::CoordinateXY>(i++) = { f.spatialKey[0], f.spatialKey[1] };
		}

		return std::make_shared<IndexType>(coordinates, geos::index::VertexSequencePackedRtree{ *coordinates });
	}

	[[nodiscard]] int QueryBox(std::shared_ptr<void> const& indexPtr, BoxType const& box) const override
	{
		auto& index = *static_cast<IndexType const*>(indexPtr.get());

		std::vector<std::size_t> result;
		index.second.query(ToEnvelope(box), result);
		return int(result.size());
	}
};

// Turn off for box keys
template <typename TSpatialKey>
struct GeosVertexSequencePackedRtree<TSpatialKey, false> : SpatialIndexWrapper<TSpatialKey>
{
};


template <class TSpatialKey, bool IsSupported = GeoToolbox::SpatialKeyIsPoint<TSpatialKey> && GeoToolbox::SpatialKeyTraits<TSpatialKey>::Dimensions == 2>
struct GeosKdTree : SpatialIndexWrapper<TSpatialKey>
{
	using VectorType = typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::VectorType;

	using BoxType = typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::BoxType;

	using FeaturePtr = GeoToolbox::Feature<TSpatialKey> const*;

	using IndexType = geos::index::kdtree::KdTree;


	[[nodiscard]] std::string_view Name() const override
	{
		return "GEOS " GEOS_VERSION " K-d Tree";
	}

	[[nodiscard]] std::shared_ptr<void> MakeEmptyIndex() const override
	{
		return std::make_shared<IndexType>( /*0.1*/ );
	}

	// No optimized bulk-loading
	[[nodiscard]] std::shared_ptr<void> Load(Dataset<TSpatialKey> const& dataset) const override
	{
		auto index = std::make_shared<IndexType>( /*0.1*/ );
		for (auto const& f : dataset.GetData())
		{
			Insert(index, &f);
		}

		return index;
	}

	void Insert(std::shared_ptr<void> const& indexPtr, FeaturePtr feature) const override
	{
		auto& index = *static_cast<IndexType*>(indexPtr.get());

		index.insert({ feature->spatialKey[0], feature->spatialKey[1] }, const_cast<GeoToolbox::Feature<TSpatialKey>*>(feature));
	}

	// Non-const query method??
	[[nodiscard]] int QueryBox(std::shared_ptr<void> const& indexPtr, BoxType const& box) const override
	{
		auto& index = *static_cast<IndexType*>(indexPtr.get());

		std::vector<geos::index::kdtree::KdNode*> result;
		index.query(ToEnvelope(box), result);
		return int(result.size());
		//return int(std::accumulate(result.begin(), result.end(), std::size_t{ 0 }, [](std::size_t total, auto const& node) { return total + node->getCount(); }));
	}
};

// Turn off for box keys
template <typename TSpatialKey>
struct GeosKdTree<TSpatialKey, false> : SpatialIndexWrapper<TSpatialKey>
{
};

#endif
