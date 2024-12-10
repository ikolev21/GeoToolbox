// Copyright 2024 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#ifdef ENABLE_NANOFLANN

#pragma GCC system_header

#include "GeoToolbox/SpatialTools.hpp"

#ifdef _MSC_VER
#	pragma warning( disable : 4127 ) // conditional expression is constant
#	pragma warning( disable : 4267 ) // '=': conversion from 'size_t' to 'int', possible loss of data
#endif

#include <nanoflann.hpp>

#ifdef _MSC_VER
#	pragma warning( default : 4127 )
#	pragma warning( default : 4267 )
#endif

struct NanoflannStaticKdtreeBase
{
	static constexpr std::string_view Name = "nanoflann " MAKE_STRING(NANOFLANN_VERSION) " Static";

	static constexpr auto IsDynamic = false;
};

template <typename TVector>
struct NanoflannStaticKdtree : NanoflannStaticKdtreeBase
{
	using Point = TVector;

	using ScalarType = typename GeoToolbox::VectorTraits<TVector>::ScalarType;

	static constexpr auto Dimensions = GeoToolbox::VectorTraits<TVector>::Dimensions;

	using BoxType = GeoToolbox::Box<TVector>;

	using FeaturePtr = GeoToolbox::Feature<Point> const*;

	struct Data
	{
		Dataset<Point> const* dataset = nullptr;

		[[nodiscard]] size_t kdtree_get_point_count() const
		{
			return dataset != nullptr ? size_t(dataset->GetSize()) : 0;
		}

		[[nodiscard]] ScalarType kdtree_get_pt(size_t index, size_t const dim) const
		{
			DEBUG_ASSERT(dim < Dimensions);
			auto const& feature = dataset->GetData()[index];
			return feature.spatialKey[dim];
		}

		template <class BBOX>
		bool kdtree_get_bbox(BBOX& bb) const
		{
			if (dataset == nullptr)
			{
				return false;
			}

			auto const& box = dataset->GetBoundingBox();
			for (auto dim = 0; dim < Dimensions; ++dim)
			{
				bb[dim].low = box.Min()[dim];
				bb[dim].high = box.Max()[dim];
			}

			return true;
		}
	};

	using TreeType = nanoflann::KDTreeSingleIndexAdaptor<nanoflann::L2_Simple_Adaptor<ScalarType, Data, ScalarType, int>, Data, Dimensions, int>;
	using IndexType = std::pair<std::unique_ptr<Data>, std::unique_ptr<TreeType>>;

	static bool Overlap(typename TreeType::BoundingBox const& treeBox, BoxType const& queryBox)
	{
		for (auto i = 0; i < int(Dimensions); ++i)
		{
			if (treeBox[i].low > queryBox.Max()[i]
				|| treeBox[i].high < queryBox.Min()[i])
			{
				return false;
			}
		}

		return true;
	}

	static IndexType Load(Dataset<Point> const& dataset, GeoToolbox::SharedAllocatedSize const&)
	{
		auto data = std::make_unique<Data>();
		data->dataset = &dataset;
		auto const dataPtr = data.get();
		// Parallel build does run faster, but only in rare cases
		//auto const params = nanoflann::KDTreeSingleIndexAdaptorParams(16, nanoflann::KDTreeSingleIndexAdaptorFlags::None, 8);
		return { std::move(data), std::make_unique<TreeType>(int(Dimensions), *dataPtr/*, params*/) };
	}

	static int QueryBox(IndexType const& index, BoxType const& queryBox)
	{
		return !Overlap(index.second->root_bbox_, queryBox) ? 0 : BoxSearchLevel(*index.first->dataset, index.second->root_node_, queryBox);
	}

	static int BoxSearchLevel(Dataset<Point> const& dataset, typename TreeType::Node* node, BoxType const& queryBox)
	{
		auto resultCount = 0;

		while (node != nullptr)
		{
			if (node->child1 == nullptr && node->child2 == nullptr)
			{
				for (auto i = node->node_type.lr.left; i < node->node_type.lr.right; ++i)
				{
					if (GeoToolbox::Overlap<Point>(queryBox, dataset.GetData()[i].spatialKey))
					{
						++resultCount;
					}
				}

				return resultCount;
			}

			auto const dimensionIndex = node->node_type.sub.divfeat;
			typename TreeType::Node* nextNode = nullptr;
			if (node->child1 != nullptr && queryBox.Min()[dimensionIndex] <= node->node_type.sub.divlow)
			{
				nextNode = node->child1;
			}

			if (node->child2 != nullptr && queryBox.Max()[dimensionIndex] >= node->node_type.sub.divhigh)
			{
				if (nextNode != nullptr)
				{
					resultCount += BoxSearchLevel(dataset, node->child2, queryBox);
				}
				else
				{
					nextNode = node->child2;
				}
			}

			node = nextNode;
		}

		return resultCount;
	}

	static double QueryNearest(IndexType const& index, TVector const& location, int nearestCount)
	{
		std::vector nearestIndices(nearestCount, 0);
		std::vector nearestDistances(nearestCount, ScalarType{ 0 });
		index.second->knnSearch(location.data(), nearestCount, nearestIndices.data(), nearestDistances.data());
		return GeoToolbox::Accumulate(nearestDistances);
	}
};

template <typename TVector>
struct NanoflannStaticKdtree<GeoToolbox::Box<TVector>> : NanoflannStaticKdtreeBase
{
	static constexpr auto Dimensions = GeoToolbox::VectorTraits<TVector>::Dimensions;

	using ScalarType = typename GeoToolbox::VectorTraits<TVector>::ScalarType;

	using Point = typename GeoToolbox::VectorTraits<TVector>::template Reconfigure<ScalarType, Dimensions * 2>;

	using BoxType = GeoToolbox::Box<TVector>;

	using FeaturePtr = GeoToolbox::Feature<BoxType> const*;

	struct Data
	{
		Dataset<BoxType> const* dataset = nullptr;

		[[nodiscard]] size_t kdtree_get_point_count() const
		{
			return dataset != nullptr ? size_t(dataset->GetSize()) : 0;
		}

		[[nodiscard]] ScalarType kdtree_get_pt(size_t index, size_t const dim) const
		{
			DEBUG_ASSERT(dim < Dimensions * 2);
			auto const& feature = dataset->GetData()[index];
			return feature.spatialKey[int(dim) / 2][dim % Dimensions];
		}

		template <class BBOX>
		bool kdtree_get_bbox(BBOX&) const
		{
			return false;
		}
	};

	using TreeType = nanoflann::KDTreeSingleIndexAdaptor<nanoflann::L2_Simple_Adaptor<ScalarType, Data, ScalarType, int>, Data, Dimensions * 2, int>;
	using IndexType = std::pair<std::unique_ptr<Data>, std::unique_ptr<TreeType>>;

	static IndexType Load(Dataset<BoxType> const& dataset, GeoToolbox::SharedAllocatedSize const&)
	{
		auto data = std::make_unique<Data>();
		data->dataset = &dataset;
		auto const dataPtr = data.get();
		// Parallel build does run faster, but only in rare cases
		//auto const params = nanoflann::KDTreeSingleIndexAdaptorParams(16, nanoflann::KDTreeSingleIndexAdaptorFlags::None, 8);
		return { std::move(data), std::make_unique<TreeType>(int(Dimensions), *dataPtr/*, params*/) };
	}

	static int QueryBox(IndexType const& index, BoxType const& queryBox)
	{
		return BoxSearchLevel(*index.first->dataset, index.second->root_node_, queryBox);
	}

	static int BoxSearchLevel(Dataset<BoxType> const& dataset, typename TreeType::Node* node, BoxType const& queryBox)
	{
		auto resultCount = 0;

		while (node != nullptr)
		{
			if (node->child1 == nullptr && node->child2 == nullptr)
			{
				for (auto i = node->node_type.lr.left; i < node->node_type.lr.right; ++i)
				{
					if (GeoToolbox::Overlap<TVector>(queryBox, dataset.GetData()[i].spatialKey))
					{
						++resultCount;
					}
				}

				return resultCount;
			}

			auto const dimensionIndex = node->node_type.sub.divfeat;
			typename TreeType::Node* nextNode = nullptr;
			if (node->child1 != nullptr
				&& (dimensionIndex < Dimensions || queryBox.Min()[dimensionIndex - Dimensions] <= node->node_type.sub.divhigh))
			{
				nextNode = node->child1;
			}

			if (node->child2 != nullptr
				&& (dimensionIndex >= Dimensions || queryBox.Max()[dimensionIndex] >= node->node_type.sub.divlow))
			{
				if (nextNode != nullptr)
				{
					resultCount += BoxSearchLevel(dataset, node->child2, queryBox);
				}
				else
				{
					nextNode = node->child2;
				}
			}

			node = nextNode;
		}

		return resultCount;
	}

	static double QueryNearest(IndexType const& /*index*/, TVector const& /*location*/, int /*nearestCount*/)
	{
		return -1;
	}
};

#endif
