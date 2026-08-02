// Copyright 2024-2026 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include "SpatialIndexAdapter.hpp"
#include "GeoToolbox/KdBoxTree.hpp"
#include "GeoToolbox/Profiling.hpp"

namespace GeoToolbox
{
	struct KdBoxTreeStats
	{
		int maxHeight;
		AggregateStats<int> elementsPerNode;
		AggregateStats<double> middlePercent;
		AggregateStats<int> heightBalance;
		AggregateStats<int> elementsCountBalance;
	};

	template <typename TElement, bool StoreSpatialKeys, class TTraits, class TElementAllocator>
	KdBoxTreeStats CalcStats( KdBoxTree<TElement, StoreSpatialKeys, TTraits, TElementAllocator> const& );


	template <typename TSpatialKey>
	struct KdBoxTreeAdapter : SpatialIndexAdapter<TSpatialKey>
	{
		using VectorType = typename SpatialKeyTraits<TSpatialKey>::VectorType;

		using BoxType = typename SpatialKeyTraits<TSpatialKey>::BoxType;

		using FeaturePtr = Feature<TSpatialKey> const*;

		using IndexType = KdBoxTree<FeaturePtr, true>;


		[[nodiscard]] std::string_view Name() const override
		{
			return "KdBoxTree";
		}

		[[nodiscard]] std::string GetIndexStats(std::shared_ptr<void> const& indexPtr) const override
		{
			auto& index = *static_cast<IndexType const*>(indexPtr.get());

			auto const stats = CalcStats(index);
			std::ostringstream stream;
			stream << "Nodes: " << index.GetNodesCount() << " Elems/Leaf: " << stats.elementsPerNode;
			if (!stats.middlePercent.IsEmpty())
			{
				stream << " mid%: " << stats.middlePercent;
			}

			stream << " Height: " << stats.maxHeight;// << ", balance: " << stats.heightBalance << ", element count balance: " << stats.elementsCountBalance;
			return stream.str();
		}

		[[nodiscard]] std::shared_ptr<void> Load(Dataset<TSpatialKey> const& dataset) const override
		{
			auto const data = dataset.GetData();
			return std::make_shared<IndexType>(MakePointersVector(data.begin(), data.end()), MaxElementsPerNode);
		}

		[[nodiscard]] int QueryRange(std::shared_ptr<void> const& indexPtr, BoxType const& box) const override
		{
			auto& index = *static_cast<IndexType const*>(indexPtr.get());
			// QueryRange is faster than iterating with RangeQueryIterator
			//return int(std::distance(index.BeginRangeQuery(box), index.EndRangeQuery()));
			auto count = 0;
			index.QueryRange(box, [&count](auto) { ++count; });
			return count;
		}

		[[nodiscard]] double QueryNearest(std::shared_ptr<void> const& indexPtr, VectorType const& location, int nearestCount) const override
		{
			auto& index = *static_cast<IndexType const*>(indexPtr.get());
			auto result = index.QueryNearest(location, nearestCount);
			return Accumulate(result, 0.0, [](double a, std::pair<int, double> const& p) { return a + p.second; });
		}
	};


	template <typename TElement, bool StoreSpatialKeys, class TTraits, class TElementAllocator>
	KdBoxTreeStats CalcStats( KdBoxTree<TElement, StoreSpatialKeys, TTraits, TElementAllocator> const& tree )
	{
		constexpr auto spatialKeyIsBox = SpatialKeyIsBox<typename KdBoxTree<TElement, StoreSpatialKeys, TTraits, TElementAllocator>::SpatialKeyType>;

		struct NodeStats
		{
			int depth = 0, height = 1, totalElements = 0;
		};

		KdBoxTreeStats result;

		std::vector<NodeStats> stats( tree.GetNodesCount() );
		auto nodeIndex = 0;
		auto const nodes = tree.Nodes();
		for ( auto const& node : nodes )
		{
			stats[nodeIndex].depth = node.parent < 0 ? 1 : stats[node.parent].depth + 1;
			++nodeIndex;
		}

		nodeIndex = int( nodes.size() );
		std::array<int, 3> children{};
		auto childrenCount = 0;
		auto const addChild = [&]( int childIndex )
		{
			if ( childIndex >= 0 )
			{
				children[childrenCount++] = childIndex;
			}
		};

		for ( auto const& node : ReverseIterable( nodes ) )
		{
			--nodeIndex;
			auto& s = stats[nodeIndex];
			childrenCount = 0;
			addChild( node.GetLowChild() );
			addChild( node.GetHighChild() );
			if constexpr ( spatialKeyIsBox )
			{
				addChild( node.boxData.middleChild );
			}

			s.height = 1;
			auto middleCount = s.totalElements = node.GetElementsCount();
			if ( childrenCount == 0 ) // For elementsPerNode only take into account leaf nodes
			{
				result.elementsPerNode.AddValue( s.totalElements );
			}

			for ( auto i = 0; i < childrenCount; ++i )
			{
				auto const& childStats = stats[children[i]];
				s.height = std::max( s.height, childStats.height + 1 );
				s.totalElements += childStats.totalElements;
			}

			if ( childrenCount > 0 ) // For balances and middlePercent only take into account non-leaf nodes
			{
				auto const lowChild = node.GetLowChild();
				auto const highChild = node.GetHighChild();
				auto const lowHeight = lowChild >= 0 ? stats[lowChild].height : 0;
				auto const lowCount = lowChild >= 0 ? stats[lowChild].totalElements : 0;
				auto const highHeight = highChild >= 0 ? stats[highChild].height : 0;
				auto const highCount = highChild >= 0 ? stats[highChild].totalElements : 0;
				result.heightBalance.AddValue( lowHeight - highHeight );
				result.elementsCountBalance.AddValue( lowCount - highCount );
				if constexpr ( spatialKeyIsBox )
				{
					if ( node.boxData.middleChild >= 0 )
					{
						middleCount += stats[node.boxData.middleChild].totalElements;
					}

					result.middlePercent.AddValue( middleCount * 100.0 / s.totalElements );
				}
			}
		}

		result.maxHeight = stats.front().height;
		return result;
	}
}
