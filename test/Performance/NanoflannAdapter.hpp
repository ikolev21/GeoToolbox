// Copyright 2024-2026 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include "SpatialIndexWrapper.hpp"

#ifndef ENABLE_NANOFLANN

template <typename TSpatialKey>
struct NanoflannStaticKdtree : SpatialIndexWrapper<TSpatialKey>
{
};

#else

#pragma GCC system_header

#include "TestTools.hpp"

#ifdef _MSC_VER
#	pragma warning( disable : 4324 ) // Node: structure was padded due to alignment specifier
#endif

// Default is 16, but tests don't show much difference in performance
#define NANOFLANN_NODE_ALIGNMENT 8

// Nanoflann uses malloc/free (in addition to new/delete), need to hook these to track memory usage
#define malloc TrackedMalloc
#define free TrackedFree
#include <nanoflann.hpp>
#undef malloc
#undef free

#include <queue>

#ifdef _MSC_VER
#	pragma warning( default : 4324 )
#endif

template <typename TSpatialKey>
struct NanoflannStaticKdtreeBase : SpatialIndexWrapper<TSpatialKey>
{
	[[nodiscard]] std::string_view Name() const override
	{
		return "nanoflann " MAKE_STRING(NANOFLANN_VERSION) " Align " MAKE_STRING(NANOFLANN_NODE_ALIGNMENT);
	}

	// Parallel build does run faster, but only in rare cases
	static constexpr auto MaxThreadCount = 1; // 8;

	template <class TreeType>
	static std::string GetTreeStats(TreeType const& tree)
	{
		using Node = typename TreeType::Node;
		GeoToolbox::AggregateStats<int> elementsPerNode;
		auto maxHeight = 1;
		std::queue<std::pair<Node*, int>> queue;
		queue.emplace(tree.root_node_, 1);
		auto nodeCount = 0;

		while (!queue.empty())
		{
			auto const [node, height] = queue.front();
			maxHeight = std::max(maxHeight, height);
			queue.pop();
			++nodeCount;
			if (node->child1 == nullptr && node->child2 == nullptr)
			{
				elementsPerNode.AddValue(int(node->node_type.lr.right - node->node_type.lr.left));
			}
			else
			{
				if (node->child1 != nullptr)
				{
					queue.emplace(node->child1, height + 1);
				}

				if (node->child2 != nullptr)
				{
					queue.emplace(node->child2, height + 1);
				}
			}
		}

		std::ostringstream stream;
		stream << "Nodes: " << nodeCount << " Elems/Leaf: " << elementsPerNode;
		stream << " Height: " << maxHeight;
		return stream.str();
	}
};

template <typename TVector>
struct NanoflannStaticKdtree final : NanoflannStaticKdtreeBase<TVector>
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
	using IndexType = std::pair<Data, std::unique_ptr<TreeType>>;

	[[nodiscard]] std::string GetIndexStats(std::shared_ptr<void> const& indexPtr) const override
	{
		auto& index = *static_cast<IndexType const*>(indexPtr.get());

		return NanoflannStaticKdtreeBase<TVector>::GetTreeStats(*index.second);
	}

	static void Convert(BoxType const& queryBox, typename TreeType::BoundingBox& treeBox)
	{
		for (auto i = 0; i < int(Dimensions); ++i)
		{
			treeBox[i].low = queryBox.Min()[i];
			treeBox[i].high = queryBox.Max()[i];
		}
	}

	static auto Convert(BoxType const& queryBox) -> typename TreeType::BoundingBox
	{
		typename TreeType::BoundingBox result;
		Convert(queryBox, result);
		return result;
	}

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

	[[nodiscard]] std::shared_ptr<void> Load(Dataset<Point> const& dataset) const override
	{
		auto result = std::make_shared<IndexType>();
		result->first.dataset = &dataset;
		auto const params = nanoflann::KDTreeSingleIndexAdaptorParams(GeoToolbox::MaxElementsPerNode, nanoflann::KDTreeSingleIndexAdaptorFlags::None, NanoflannStaticKdtreeBase<TVector>::MaxThreadCount);
		result->second = std::make_unique<TreeType>(int(Dimensions), result->first, params);
		return result;
	}

	struct QueryBoxResultSet
	{
		std::vector<int> indices;

		void init() { clear(); }
		void clear() { indices.clear(); }

		size_t size() const noexcept { return indices.size(); }
		bool empty() const noexcept { return indices.empty(); }
		bool full() const noexcept { return true; }

		bool addPoint(ScalarType /*distance*/, int index)
		{
			indices.push_back(index);
			return true;
		}

		ScalarType worstDist() const noexcept { return std::numeric_limits<ScalarType>::max(); }

		//void sort() { std::sort(m_indices_dists.begin(), m_indices_dists.end(), IndexDist_Sorter()); }
	};


	[[nodiscard]] int QueryBox(std::shared_ptr<void> const& indexPtr, BoxType const& queryBox) const override
	{
		auto& index = *static_cast<IndexType const*>(indexPtr.get());

#if 0
		// For some reason findWithinBox() is 2-3 times slower than BoxSearchLevel()
		QueryBoxResultSet resultSet;
		return int(index.second->findWithinBox(resultSet, Convert(queryBox)));
#else
		return !Overlap(index.second->root_bbox_, queryBox) ? 0 : BoxSearchLevel(*index.second, *index.first.dataset, index.second->root_node_, queryBox); 
#endif
	}

	static int BoxSearchLevel(TreeType const& tree, Dataset<Point> const& dataset, typename TreeType::Node* node, BoxType const& queryBox)
	{
		auto resultCount = 0;

		while (node != nullptr)
		{
			if (node->child1 == nullptr && node->child2 == nullptr)
			{
				GeoToolbox::AddQueryStats_VisitedNodesCount();

				for (auto i = node->node_type.lr.left; i < node->node_type.lr.right; ++i)
				{
					GeoToolbox::AddQueryStats_ObjectTestsCount();
					if (GeoToolbox::Overlap<Point>(queryBox, dataset.GetData()[tree.vAcc_[i]].spatialKey))
					{
						++resultCount;
					}
				}

				return resultCount;
			}

			auto const dimensionIndex = node->node_type.sub.divfeat;
			typename TreeType::Node* nextNode = nullptr;
			GeoToolbox::AddQueryStats_ScalarComparisonsCount();
			if (node->child1 != nullptr && queryBox.Min()[dimensionIndex] <= node->node_type.sub.divlow)
			{
				nextNode = node->child1;
			}

			GeoToolbox::AddQueryStats_ScalarComparisonsCount();
			if (node->child2 != nullptr && queryBox.Max()[dimensionIndex] >= node->node_type.sub.divhigh)
			{
				if (nextNode != nullptr)
				{
					resultCount += BoxSearchLevel(tree, dataset, node->child2, queryBox);
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

	[[nodiscard]] double QueryNearest(std::shared_ptr<void> const& indexPtr, TVector const& location, int nearestCount) const override
	{
		auto& index = *static_cast<IndexType const*>(indexPtr.get());

		std::vector nearestIndices(nearestCount, 0);
		std::vector nearestDistances(nearestCount, ScalarType{ 0 });
		index.second->knnSearch(location.data(), nearestCount, nearestIndices.data(), nearestDistances.data());
		return GeoToolbox::Accumulate(nearestDistances, 0.0, [](double a, ScalarType d) { return a + d; });
	}
};

template <typename TVector>
struct NanoflannStaticKdtree<GeoToolbox::Box<TVector>> final : NanoflannStaticKdtreeBase<GeoToolbox::Box<TVector>>
{
	static constexpr auto Dimensions = GeoToolbox::VectorTraits<TVector>::Dimensions;

	using BaseType = NanoflannStaticKdtreeBase<GeoToolbox::Box<TVector>>;

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
			return feature.spatialKey[int(dim) / Dimensions][dim % Dimensions];
		}

		template <class BBOX>
		static bool kdtree_get_bbox(BBOX&)
		{
			return false;
		}
	};

	using TreeType = nanoflann::KDTreeSingleIndexAdaptor<nanoflann::L2_Simple_Adaptor<ScalarType, Data, ScalarType, int>, Data, Dimensions * 2, int>;
	using IndexType = std::pair<Data, std::unique_ptr<TreeType>>;

	[[nodiscard]] std::shared_ptr<void> Load(Dataset<BoxType> const& dataset) const override
	{
		auto result = std::make_shared<IndexType>();
		result->first.dataset = &dataset;
		auto const params = nanoflann::KDTreeSingleIndexAdaptorParams(GeoToolbox::MaxElementsPerNode, nanoflann::KDTreeSingleIndexAdaptorFlags::None, BaseType::MaxThreadCount);
		result->second = std::make_unique<TreeType>(int(Dimensions), result->first, params);
		return result;
	}

	[[nodiscard]] std::string GetIndexStats(std::shared_ptr<void> const& indexPtr) const override
	{
		auto& index = *static_cast<IndexType const*>(indexPtr.get());

		return BaseType::GetTreeStats(*index.second);
	}

	[[nodiscard]] int QueryBox(std::shared_ptr<void> const& indexPtr, BoxType const& queryBox) const override
	{
		auto& index = *static_cast<IndexType const*>(indexPtr.get());

		return BoxSearchLevel(*index.second, *index.first.dataset, index.second->root_node_, queryBox);
	}

	static int BoxSearchLevel(TreeType const& tree, Dataset<BoxType> const& dataset, typename TreeType::Node* node, BoxType const& queryBox)
	{
		auto resultCount = 0;

		while (node != nullptr)
		{
			if (node->child1 == nullptr && node->child2 == nullptr)
			{
				GeoToolbox::AddQueryStats_VisitedNodesCount();

				for (auto i = node->node_type.lr.left; i < node->node_type.lr.right; ++i)
				{
					GeoToolbox::AddQueryStats_ObjectTestsCount();
					if (GeoToolbox::Overlap<TVector>(queryBox, dataset.GetData()[tree.vAcc_[i]].spatialKey))
					{
						++resultCount;
					}
				}

				return resultCount;
			}

			auto const dimensionIndex = node->node_type.sub.divfeat;
			typename TreeType::Node* nextNode = nullptr;
			if (node->child1 != nullptr
				&& (GeoToolbox::AddQueryStats_ScalarComparisonsCount(), dimensionIndex < Dimensions || queryBox.Min()[dimensionIndex - Dimensions] <= node->node_type.sub.divlow))
			{
				nextNode = node->child1;
			}

			if (node->child2 != nullptr
				&& (GeoToolbox::AddQueryStats_ScalarComparisonsCount(), dimensionIndex >= Dimensions || queryBox.Max()[dimensionIndex] >= node->node_type.sub.divhigh))
			{
				if (nextNode != nullptr)
				{
					resultCount += BoxSearchLevel(tree, dataset, node->child2, queryBox);
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
};

#endif
