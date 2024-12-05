#pragma once

template <typename TSpatialKey>
struct GeosTemplateStrTree;

template <typename TSpatialKey>
struct GeosQuadTree;

#ifndef ENABLE_GEOS

template <typename TSpatialKey>
struct GeosTemplateStrTree
{
	using IndexType = void;
};

template <typename TSpatialKey>
struct GeosQuadTree
{
	using IndexType = void;
};

#else

#include "TestTools.hpp"
#include "GeoToolbox/SpatialTools.hpp"

#include <geos/version.h>
#include <geos/index/ItemVisitor.h>
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
	return geos::geom::Envelope{ box.Min()[0], box.Max()[0], box.Min()[1], box.Max()[1] };
}

template <typename TSpatialKey>
struct GeosTemplateStrTree
{
	static constexpr std::string_view Name = "GEOS " GEOS_VERSION " TemplateSTRTree";

	// TemplateSTRtree does have a remove() operation, but maybe I don't know how to use it properly, because the query crashes later. For now use it as static index.
	static constexpr auto IsDynamic = false;

	using VectorType = typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::VectorType;

	using BoxType = typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::BoxType;

	using FeaturePtr = GeoToolbox::Feature<TSpatialKey> const*;

	using IndexType = geos::index::strtree::TemplateSTRtree<FeaturePtr>;

	static IndexType MakeEmptyIndex(GeoToolbox::SharedAllocatedSize const&)
	{
		return IndexType{};
	}

	static IndexType Load(Dataset<TSpatialKey> const& dataset, GeoToolbox::SharedAllocatedSize const&)
	{
		IndexType index;
		for (auto const&f : dataset.GetData())
		{
			Insert(index, &f);
		}

		index.build();
		return index;
	}

	static void Insert(IndexType& index, FeaturePtr feature)
	{
		index.insert(ToEnvelope(feature->spatialKey), feature);
	}

	static bool Erase(IndexType& /*index*/, FeaturePtr /*feature*/)
	{
		// There is some problem with removal, the query crashes later
		return false;// index.remove(ToEnvelope(feature->spatialKey), feature);
	}

	static void Rebalance(IndexType& index)
	{
		index.build();
	}

	static int QueryBox(IndexType& index, BoxType const& box)
	{
		auto count = 0;
		index.query(ToEnvelope(box), [&count](auto) { ++count; });
		return count;
	}

	static double QueryNearest(IndexType& /*index*/, VectorType const& /*location*/, int /*nearestCount*/)
	{
		return -1;
	}
};

template <class TSpatialKey>
struct GeosQuadTree
{
	static constexpr std::string_view Name = "GEOS " GEOS_VERSION " QuadTree";

	static constexpr auto IsDynamic = false;

	using VectorType = typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::VectorType;

	using BoxType = typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::BoxType;

	using FeaturePtr = GeoToolbox::Feature<TSpatialKey> const*;

	// Need to put the Quadtree into a unique_ptr because it has a deleted copy ctor but doesn't have a move ctor and Load() can't return it by value
	// Quadtree simply needs to be added a default move ctor, but I prefer not to apply a patch the original source
	using IndexType = std::unique_ptr<geos::index::quadtree::Quadtree>;

	static IndexType MakeEmptyIndex(GeoToolbox::SharedAllocatedSize const&)
	{
		return std::make_unique<geos::index::quadtree::Quadtree>();
	}

	static IndexType Load(Dataset<TSpatialKey> const& dataset, GeoToolbox::SharedAllocatedSize const& allocatedSize )
	{
		auto index = MakeEmptyIndex(allocatedSize);
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

	static void Insert(IndexType const& index, FeaturePtr feature)
	{
		auto const envelope = ToEnvelope(feature->spatialKey);
		index->insert(&envelope, const_cast<GeoToolbox::Feature<TSpatialKey>*>(feature));
	}

	static bool Erase(IndexType& /*index*/, FeaturePtr /*feature*/)
	{
		return false;// index.remove(ToEnvelope(feature->spatialKey), feature);
	}

	static void Rebalance(IndexType&)
	{
	}

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

	static int QueryBox(IndexType const& index, BoxType const& box)
	{
		auto const envelope = ToEnvelope(box);
		CountingVisitor visitor;
		visitor.queryBox = &box;
		index->query(&envelope, visitor);
		return visitor.count;
	}

	static double QueryNearest(IndexType const& /*index*/, VectorType const& /*location*/, int /*nearestCount*/)
	{
		return -1;
	}
};

#endif
