// Copyright 2024-2026 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include "GeoToolbox/GeometryTools.hpp"
#include "GeoToolbox/Profiling.hpp"
#include "GeoToolbox/Span.hpp"
#include "GeoToolbox/SpatialTools.hpp"

#include <queue>

namespace GeoToolbox
{
	// The default implementation assumes that the tree element type TElement coincides with one of the supported spatial key types, that's either a vector or a Box<Vector>
	template <typename TElement>
	struct BoxTreeTraits
	{
		using ElementType = TElement;

		using SpatialKeyType = TElement;

		using VectorType = typename GetVectorType<TElement>::type;

		static_assert(IsVector<VectorType>, "Invalid TElement");

		[[nodiscard]] static SpatialKeyType GetSpatialKey(TElement const& element)
		{
			return element;
		}

		[[nodiscard]] static bool AreEqual(TElement const& a, TElement const& b)
		{
			return a == b;
		}
	};

	template <typename TSpatialKey>
	struct BoxTreeTraits<Feature<TSpatialKey> const*>
	{
		using ElementType = Feature<TSpatialKey> const*;

		using SpatialKeyType = TSpatialKey;

		using VectorType = typename SpatialKeyTraits<TSpatialKey>::VectorType;

		[[nodiscard]] static bool AreEqual(ElementType const& a, ElementType const& b)
		{
			return a == b;
		}

		[[nodiscard]] static auto GetSpatialKey(ElementType feature)
		{
			return feature->spatialKey;
		}
	};

	namespace Detail
	{
		template <int NDimensions>
		struct BoxTreeStaticNode_BoxData
		{
			int middleChild = -1;
			// An std::bitset would serve better, but unfortunately it's size cannot be controlled, and GCC uses 64 bits while 32 are enough for us
			std::uint32_t lockedAxesMask = 0;
		};

		template <>
		struct BoxTreeStaticNode_BoxData<0>
		{
		};

		template <class TVector>
		[[nodiscard]] auto GetMaxDistanceSquared(TVector const& point, Box<TVector> const& box)
		{
			typename VectorTraits<TVector>::ScalarType result{ 0 };
			auto const center = box.Center();
			for (auto i = 0; i < int(VectorTraits<TVector>::Dimensions); ++i)
			{
				if (point[i] <= center[i])
				{
					result += Square(box.Max()[i] - point[i]);
				}
				else
				{
					result += Square(point[i] - box.Min()[i]);
				}
			}

			return result;
		}
	}

	// A static k-d tree that supports boxes. The boxes that intersect the splitting line are pushed into a new node that gets split further down by the other axes
	template <typename TElement, class TTraits = BoxTreeTraits<TElement>, class TElementAllocator = std::allocator<TElement>>
	class BoxTreeStatic
	{
	public:
		// Strategy 2 calculates the exact (tight) box of each node, strategy 1 only updates the limit on the split axis. 2 is significantly faster (both for build and query) only for Polygon dataset
		static constexpr auto NewBoxStrategy = 1;

		static_assert(std::is_move_constructible_v<TElement>&& std::is_move_assignable_v<TElement>);

		struct Node;

	private:

		using NodeAllocatorType = typename std::allocator_traits<TElementAllocator>::template rebind_alloc<Node>;

		std::vector<TElement, TElementAllocator> elements_;

		std::vector<Node, NodeAllocatorType> nodes_;

		/* [[no_unique_address]] */ TTraits traits_;

		int maxElementsPerNode_;

	public:

		static constexpr auto MaxElementsPerNode = GeoToolbox::MaxElementsPerNode;

		using ElementType = TElement;
		using SpatialKeyType = typename TTraits::SpatialKeyType;
		using IndexType = int;	// Type that covers the maximum possible number of elements. May be passed as template parameter

		using VectorType = typename TTraits::VectorType;
		using VectorTraitsType = VectorTraits<VectorType>;
		using ScalarType = typename VectorTraitsType::ScalarType;
		using BoxType = Box<VectorType>;

		class NodeIterator;
		class ElementIterator;
		class RangeQueryIterator;

		using value_type = ElementType;
		using const_iterator = ElementType const*;
		using iterator = const_iterator;

		static constexpr auto KeyIsBox = IsSpecialization<SpatialKeyType, Box>;


		explicit BoxTreeStatic(std::vector<TElement, TElementAllocator> elements, int maxElementsPerNode = 0, TTraits traits = {})
			: elements_{ TElementAllocator(elements.get_allocator()) }
			, nodes_(NodeAllocatorType(elements.get_allocator()))
			, traits_{ traits }
			, maxElementsPerNode_{ maxElementsPerNode > 0 ? maxElementsPerNode : MaxElementsPerNode }
		{
			Create(std::move(elements));
		}

		void Create(std::vector<TElement, TElementAllocator> elements);

		[[nodiscard]] bool IsEmpty() const noexcept
		{
			return elements_.empty();
		}

		[[nodiscard]] Span<ElementType const> Elements() const noexcept
		{
			return elements_;
		}

		[[nodiscard]] Span<Node const> Nodes() const noexcept
		{
			return nodes_;
		}

		[[nodiscard]] int GetMaxElementsPerNode() const noexcept
		{
			return maxElementsPerNode_;
		}

		[[nodiscard]] IndexType GetElementsCount() const noexcept
		{
			return IndexType(Size(elements_));
		}

		[[nodiscard]] int GetNodesCount() const noexcept
		{
			return int(Size(nodes_));
		}

		[[nodiscard]] TElementAllocator GetAllocator() const
		{
			return elements_.get_allocator();
		}

		[[nodiscard]] NodeIterator GetRootNode() const
		{
			return { *this, !nodes_.empty() ? 0 : -1 };
		}

		[[nodiscard]] NodeIterator BeginNodes() const
		{
			return GetRootNode();
		}

		[[nodiscard]] NodeIterator EndNodes() const
		{
			return { *this, -1 };
		}

		[[nodiscard]] RangeQueryIterator BeginRangeQuery(BoxType const& range) const;

		[[nodiscard]] RangeQueryIterator EndRangeQuery() const
		{
			return RangeQueryIterator{ EndNodes() };
		}

		template <class TVisitor>
		void QueryRange(BoxType const& range, TVisitor&& visitor) const
		{
			if (nodes_.empty())
			{
				return;
			}

			QueryRange(0, range, visitor);
		}

		std::vector<IndexType> QueryRange(BoxType const& range) const
		{
			std::vector<IndexType> result;
			QueryRange(range, [this, &result](ElementType const& elem) { result.push_back(IndexType(&elem - elements_.data())); });
			return result;
		}

		template <class TVisitor>
		void QueryRange(int nodeIndex, BoxType const& range, TVisitor&& visitor) const
		{
			DEBUG_ASSERT(nodeIndex >= 0);
			DEBUG_ASSERT(nodeIndex < int(nodes_.size()));

			while (nodeIndex >= 0)
			{
				auto const& node = nodes_[nodeIndex];
				AddQueryStats_BoxOverlapsCount();
				if (Contains(range, node.box))
				{
					VisitSubTree(nodeIndex, visitor);
					return;
				}

				if (node.IsLeaf())
				{
					AddQueryStats_VisitedNodesCount();
					for (auto elementIndex = node.elementsBegin; elementIndex < node.elementsEnd; ++elementIndex)
					{
						AddQueryStats_ObjectTestsCount();
						if (Overlap(range, traits_.GetSpatialKey(elements_[elementIndex])))
						{
							visitor(elements_[elementIndex]);
						}
					}

					return;
				}

				nodeIndex = -1;
				if (node.lowChild >= 0 && OverlapWithNode(range, node.lowChild))
				{
					nodeIndex = node.lowChild;
				}

				if constexpr (KeyIsBox)
				{
					if (node.boxData.middleChild >= 0 && OverlapWithNode(range, node.boxData.middleChild))
					{
						if (nodeIndex >= 0)
						{
							QueryRange(node.boxData.middleChild, range, visitor);
						}
						else
						{
							nodeIndex = node.boxData.middleChild;
						}
					}
				}

				if (node.highChild >= 0 && OverlapWithNode(range, node.highChild))
				{
					if (nodeIndex >= 0)
					{
						QueryRange(node.highChild, range, visitor);
					}
					else
					{
						nodeIndex = node.highChild;
					}
				}
			}
		}

		int QueryNearest(std::vector<std::pair<IndexType, ScalarType>>& result, VectorType const& targetLocation, int nearestCount = 0, ScalarType maxDistance = -1) const
		{
			if (elements_.empty())
			{
				return 0;
			}

			std::array<SearchNode, 64> buffer;
			std::pmr::monotonic_buffer_resource resource{ buffer.data(), buffer.size() };
			std::pmr::polymorphic_allocator<int> allocator{ &resource };
			std::pmr::vector<SearchNode> stack{ allocator };

			return QueryNearest_(result, targetLocation, nearestCount, maxDistance, stack);
		}

		[[nodiscard]] std::vector<std::pair<IndexType, ScalarType>> QueryNearest(VectorType const& targetLocation, int nearestCount = 0, ScalarType maxDistance = -1) const
		{
			std::vector<std::pair<IndexType, ScalarType>> result;
			QueryNearest(result, targetLocation, nearestCount, maxDistance);
			return result;
		}

		//mutable int MaxNodeStackDepth = 0;

	private:

		struct SearchNode
		{
			int nodeIndex;
			ScalarType distance2;

			[[nodiscard]] bool operator<(SearchNode const& other) const noexcept
			{
				return distance2 > other.distance2;
			}
		};

		static void InsertIntoStack(std::pmr::vector<SearchNode>& nodeStack, int nodeIndex, ScalarType distance2)
		{
			auto i = int(nodeStack.size()) - 1;
			for (; i >= 0; --i)
			{
				if (distance2 <= nodeStack[i].distance2)
				{
					break;
				}
			}

			nodeStack.insert(nodeStack.begin() + (i + 1), SearchNode{ nodeIndex, distance2 });
		}

		[[nodiscard]] int QueryNearest_(std::vector<std::pair<IndexType, ScalarType>>& result, VectorType const& targetLocation, int nearestCount, ScalarType maxDistance, std::pmr::vector<SearchNode>& nodeStack) const
		{
			if (nearestCount > 0)
			{
				result.reserve(nearestCount + 1);
			}
			else
			{
				ASSERT(maxDistance > 0);
				nearestCount = -1;
			}

			auto const distanceToFullBox = GetDistanceSquared(targetLocation, nodes_[0].box);
			auto const maxPossibleDistance2 = Detail::GetMaxDistanceSquared(targetLocation, nodes_[0].box);
			auto worstDistance2 = maxDistance <= 0 ? maxPossibleDistance2 : std::min(maxPossibleDistance2, Square(maxDistance));

			nodeStack.push_back(SearchNode{ 0, distanceToFullBox });
			while (!nodeStack.empty())
			{
				//MaxNodeStackDepth = std::max(MaxNodeStackDepth, int(nodeStack.size()));
				auto [currentNodeIndex, currentNodeDistance2] = nodeStack.back();
				nodeStack.pop_back();

				AddQueryStats_ScalarComparisonsCount();
				if (currentNodeDistance2 > worstDistance2)
				{
					break;
				}

				auto const& node = nodes_[currentNodeIndex];
				if (node.IsLeaf())
				{
					AddQueryStats_VisitedNodesCount();

					for (auto elementIndex = node.elementsBegin; elementIndex < node.elementsEnd; ++elementIndex)
					{
						AddQueryStats_ObjectTestsCount();
						auto const distance2 = GetDistanceSquared(targetLocation, traits_.GetSpatialKey(elements_[elementIndex]));
						if (distance2 > worstDistance2)
						{
							continue;
						}

						auto position = std::lower_bound(result.begin(), result.end(), distance2, [](auto const& pair, ScalarType v) { return pair.second < v; });
						result.emplace(position, elementIndex, distance2);
						if (Size(result) == nearestCount + 1)
						{
							result.erase(--result.end());
							worstDistance2 = result.back().second;
						}
					}

					continue;
				}

				ScalarType d2;
				if (node.lowChild >= 0 && (d2 = GetDistanceSquared(targetLocation, nodes_[node.lowChild].box)) <= worstDistance2)
				{
					InsertIntoStack(nodeStack, node.lowChild, d2);
				}

				if (node.highChild >= 0 && (d2 = GetDistanceSquared(targetLocation, nodes_[node.highChild].box)) <= worstDistance2)
				{
					InsertIntoStack(nodeStack, node.highChild, d2);
				}

				if constexpr (KeyIsBox)
				{
					if (node.boxData.middleChild >= 0 && (d2 = GetDistanceSquared(targetLocation, nodes_[node.boxData.middleChild].box)) <= worstDistance2)
					{
						InsertIntoStack(nodeStack, node.boxData.middleChild, d2);
					}
				}
			}

			return int(result.size());
		}

		void Build();

		bool SplitNode(int nodeIndex);

		bool SplitNode(int nodeIndex, int splitAxis, ScalarType splitPosition);

		std::pair<IndexType, IndexType> PartitionPoints(Node const&, int splitAxis, ScalarType splitPosition);

		std::pair<IndexType, IndexType> PartitionBoxes(Node const&, int splitAxis, ScalarType& splitPosition);

		std::pair<IndexType, IndexType> Partition(Node const& node, int splitAxis, ScalarType& splitPosition)
		{
			if constexpr (KeyIsBox)
			{
				return PartitionBoxes(node, splitAxis, splitPosition);
			}
			else
			{
				return PartitionPoints(node, splitAxis, splitPosition);
			}
		}

		void ReduceBoxAtLowEnd(BoxType& box, IndexType startIndex, IndexType count, int axis)
		{
			auto newLimit = box[1][axis];
			auto const end = startIndex + count;
			for (auto i = startIndex; i < end; ++i)
			{
				newLimit = std::min(newLimit, GetLowBound(traits_.GetSpatialKey(elements_[i]), axis));
			}

			auto newEnd = box[0];
			newEnd[axis] = newLimit;
			box = { newEnd, box[1] };
		}

		void ReduceBoxAtHighEnd(BoxType& box, IndexType startIndex, IndexType count, int axis)
		{
			auto newLimit = box[0][axis];
			auto const end = startIndex + count;
			for (auto i = startIndex; i < end; ++i)
			{
				newLimit = std::max(newLimit, GetHighBound(traits_.GetSpatialKey(elements_[i]), axis));
			}

			auto newEnd = box[1];
			newEnd[axis] = newLimit;
			box = { box[0], newEnd };
		}

		template <class TVisitor>
		void VisitSubTree(int rootNodeIndex, TVisitor&& visitor) const;

		[[nodiscard]] bool OverlapWithNode(BoxType const& range, int nodeIndex) const
		{
			AddQueryStats_BoxOverlapsCount();
			return Overlap(range, nodes_[nodeIndex].box);
		}

		[[nodiscard]] int GetFirstChildOverlap(int nodeIndex, BoxType const& range) const
		{
			auto const& node = nodes_[nodeIndex];
			if (node.IsLeaf())
			{
				return -1;
			}

			if (node.lowChild >= 0 && OverlapWithNode(range, node.lowChild))
			{
				return node.lowChild;
			}

			if constexpr (KeyIsBox)
			{
				if (node.boxData.middleChild >= 0 && OverlapWithNode(range, node.boxData.middleChild))
				{
					return node.boxData.middleChild;
				}
			}

			if (node.highChild >= 0 && OverlapWithNode(range, node.highChild))
			{
				return node.highChild;
			}

			return -1;
		}

		[[nodiscard]] int GetNextSiblingOverlap(int nodeIndex, BoxType const& range) const
		{
			auto const& node = nodes_[nodeIndex];
			if (node.parent < 0)
			{
				return -1;
			}

			auto const& parentNode = nodes_[node.parent];

			if constexpr (KeyIsBox)
			{
				if (nodeIndex == parentNode.lowChild && parentNode.boxData.middleChild >= 0 && OverlapWithNode(range, parentNode.boxData.middleChild))
				{
					return parentNode.boxData.middleChild;
				}
			}

			if (nodeIndex != parentNode.highChild && parentNode.highChild >= 0 && OverlapWithNode(range, parentNode.highChild))
			{
				return parentNode.highChild;
			}

			return -1;
		}

		[[nodiscard]] auto Bound(Span<TElement const> elements)
		{
			return GeoToolbox::Bound(elements, [this](TElement const& x) { return traits_.GetSpatialKey(x); });
		}
	};

	template <typename TElement, class TTraits, class TElementAllocator>
	struct BoxTreeStatic<TElement, TTraits, TElementAllocator>::Node
	{
		friend class BoxTreeStatic;

		int parent;

	private:
		union
		{
			int lowChild;
			IndexType elementsBegin;
		};
		union
		{
			int highChild;
			IndexType elementsEnd;
		};

	public:
		char splitAxis;
		/* [[no_unique_address]] */ Detail::BoxTreeStaticNode_BoxData<IsSpecialization<SpatialKeyType, Box> ? VectorTraits<VectorType>::Dimensions : 0> boxData;
		Box<VectorType> box;


		Node(IndexType elementsCount, BoxType const& box)
			: parent{ -1 }
			, splitAxis{ -1 }
			, box{ box }
		{
			elementsBegin = 0;
			elementsEnd = elementsCount;
		}

		Node(int parent, IndexType elementsBegin_, IndexType elementsEnd_, BoxType const& box)
			: parent{ parent }
			, splitAxis{ -1 }
			, box{ box }
		{
			elementsBegin = elementsBegin_;
			elementsEnd = elementsEnd_;
		}

		[[nodiscard]] bool IsLeaf() const noexcept
		{
			return splitAxis < 0;
		}

		[[nodiscard]] int GetLowChild() const noexcept
		{
			return !IsLeaf() ? lowChild : -1;
		}

		[[nodiscard]] int GetHighChild() const noexcept
		{
			return !IsLeaf() ? highChild : -1;
		}

		[[nodiscard]] bool IsAxisLocked(int axis) const noexcept
		{
			if constexpr (IsSpecialization<SpatialKeyType, Box>)
			{
				return (boxData.lockedAxesMask & (1 << axis)) != 0;
			}
			else
			{
				return false;
			}
		}

		[[nodiscard]] IndexType GetElementsCount() const noexcept
		{
			return IsLeaf() ? elementsEnd - elementsBegin : 0;
		}

		[[nodiscard]] bool HasElements() const noexcept
		{
			return GetElementsCount() > 0;
		}

		[[nodiscard]] IndexType GetFirstElement() const noexcept
		{
			return IsLeaf() ? elementsBegin : -1;
		}

		[[nodiscard]] int GetFirstChild() const
		{
			if (IsLeaf())
			{
				return -1;
			}

			if constexpr (KeyIsBox)
			{
				return lowChild >= 0 ? lowChild
					: highChild >= 0 ? highChild
					: boxData.middleChild;
			}
			else
			{
				return lowChild >= 0 ? lowChild : highChild;
			}
		}

		void Split(int axis, int lowChild_, int highChild_)
		{
			splitAxis = char(axis);
			lowChild = lowChild_;
			highChild = highChild_;
		}
	};

	template <typename TElement, class TTraits, class TElementAllocator>
	class BoxTreeStatic<TElement, TTraits, TElementAllocator>::NodeIterator
	{
	protected:
		BoxTreeStatic const* tree_;
		int nodeIndex_;
		bool down_ = true;

	public:

		using value_type = Node;
		using pointer = value_type const*;
		using reference = value_type const&;
		using iterator_category = std::forward_iterator_tag;
		using difference_type = ptrdiff_t;

		NodeIterator(BoxTreeStatic const& tree, int nodeIndex)
			: tree_{ &tree }
			, nodeIndex_{ nodeIndex }
		{
			ASSERT(nodeIndex < 0 || nodeIndex < Size(tree_->nodes_));
		}

		[[nodiscard]] bool IsValid() const noexcept
		{
			return nodeIndex_ >= 0;
		}

		[[nodiscard]] reference GetNode() const
		{
			DEBUG_ASSERT(IsValid());
			return tree_->nodes_[nodeIndex_];
		}

		[[nodiscard]] NodeIterator GetParent() const
		{
			return { *tree_, GetNode().parent };
		}

		[[nodiscard]] NodeIterator GetLowChild() const
		{
			return { *tree_, GetNode().GetLowChild() };
		}

		[[nodiscard]] NodeIterator GetMiddleChild() const
		{
			return { *tree_, GetNode().boxData.middleChild };
		}

		[[nodiscard]] NodeIterator GetHighChild() const
		{
			return { *tree_, GetNode().GetHighChild() };
		}

		[[nodiscard]] int GetFirstChild() const
		{
			DEBUG_ASSERT(IsValid());
			return GetNode().GetFirstChild();
		}

		[[nodiscard]] int GetNextSibling() const
		{
			DEBUG_ASSERT(IsValid());
			auto const& node = GetNode();
			if (node.parent < 0)
			{
				return -1;
			}

			auto const& parentNode = tree_->nodes_[node.parent];
			if constexpr (KeyIsBox)
			{
				if (nodeIndex_ == parentNode.lowChild)
				{
					return parentNode.boxData.middleChild >= 0 ? parentNode.boxData.middleChild : parentNode.highChild;
				}

				return nodeIndex_ == parentNode.boxData.middleChild ? parentNode.highChild : -1;
			}
			else
			{
				return nodeIndex_ == parentNode.lowChild ? parentNode.highChild : -1;
			}
		}

		reference operator* () const
		{
			return GetNode();
		}

		pointer operator-> () const noexcept
		{
			return tree_->nodes_.data() + nodeIndex_;
		}

		[[nodiscard]] friend bool operator==(NodeIterator const& a, NodeIterator const& b) noexcept
		{
			return a.operator->() == b.operator->();
		}

		[[nodiscard]] friend bool operator!=(NodeIterator const& a, NodeIterator const& b) noexcept
		{
			return !(a == b);
		}

		NodeIterator& operator++ ()
		{
			DEBUG_ASSERT(IsValid());
			for (;;)
			{
				auto const& node = tree_->nodes_[nodeIndex_];

				if (down_)
				{
					auto const child = GetFirstChild();
					if (child >= 0)
					{
						nodeIndex_ = child;
						return *this;
					}
				}

				auto const sibling = GetNextSibling();
				if (sibling >= 0)
				{
					nodeIndex_ = sibling;
					down_ = true;
					return *this;
				}

				nodeIndex_ = node.parent;
				down_ = false;
				if (nodeIndex_ < 0)
				{
					return *this;
				}
			}
		}
	};

	template <typename TElement, class TTraits, class TElementAllocator>
	class BoxTreeStatic<TElement, TTraits, TElementAllocator>::RangeQueryIterator : public NodeIterator
	{
		BoxType range_;
		int elementIndex_ = -1;

	public:

		using value_type = ElementType;
		using pointer = value_type const*;
		using reference = value_type const&;
		using iterator_category = std::forward_iterator_tag;
		using difference_type = ptrdiff_t;

		/*explicit(false)*/ RangeQueryIterator(NodeIterator iter, BoxType const& range = {})
			: NodeIterator{ iter }
			, range_{ range }
		{
			if (this->IsValid())
			{
				AddQueryStats_VisitedNodesCount();
				elementIndex_ = this->tree_->nodes_[this->nodeIndex_].GetFirstElement();
				MoveToNextValid();
			}
		}

		[[nodiscard]] friend bool operator==(RangeQueryIterator const& a, RangeQueryIterator const& b) noexcept
		{
			return static_cast<NodeIterator const&>(a) == static_cast<NodeIterator const&>(b)
				&& (!a.IsValid() || a.elementIndex_ == b.elementIndex_);
		}

		[[nodiscard]] friend bool operator!=(RangeQueryIterator const& a, RangeQueryIterator const& b) noexcept
		{
			return !(a == b);
		}

		reference operator*() const
		{
			return this->tree_->elements_[elementIndex_];
		}

		pointer operator->() const noexcept
		{
			return this->tree_->elements_.data() + elementIndex_;
		}

		RangeQueryIterator& operator++ ()
		{
			++elementIndex_;
			MoveToNextValid();
			return *this;
		}

	private:

		void MoveToNextValid()
		{
			for (;;)
			{
				auto const& node = this->GetNode();
				if (node.IsLeaf())
				{
					for (; elementIndex_ < node.elementsEnd; ++elementIndex_)
					{
						AddQueryStats_ObjectTestsCount();
						if (Overlap(range_, this->tree_->traits_.GetSpatialKey(operator*())))
						{
							return;
						}
					}

					this->down_ = false;
				}

				for (;;)
				{
					if (this->down_)
					{
						auto const child = this->tree_->GetFirstChildOverlap(this->nodeIndex_, range_);
						if (child >= 0)
						{
							this->nodeIndex_ = child;
							break;
						}
					}

					auto const sibling = this->tree_->GetNextSiblingOverlap(this->nodeIndex_, range_);
					if (sibling >= 0)
					{
						this->nodeIndex_ = sibling;
						this->down_ = true;
						break;
					}

					this->nodeIndex_ = this->GetNode().parent;
					this->down_ = false;
					if (this->nodeIndex_ < 0)
					{
						return;
					}
				}

				AddQueryStats_VisitedNodesCount();
				this->elementIndex_ = this->GetNode().GetFirstElement();
			}
		}
	};


	template <typename TElement, class TTraits, class TElementAllocator>
	template <class TVisitor>
	void BoxTreeStatic<TElement, TTraits, TElementAllocator>::VisitSubTree(int nodeIndex, TVisitor&& visitor) const
	{
		while (nodeIndex >= 0)
		{
			auto const& node = nodes_[nodeIndex];

			if (node.IsLeaf())
			{
				AddQueryStats_VisitedNodesCount();
				for (auto elementIndex = node.elementsBegin; elementIndex < node.elementsEnd; ++elementIndex)
				{
					visitor(elements_[elementIndex]);
				}

				return;
			}

			nodeIndex = -1;
			if (node.lowChild >= 0)
			{
				nodeIndex = node.lowChild;
			}

			if constexpr (KeyIsBox)
			{
				if (node.boxData.middleChild >= 0)
				{
					if (nodeIndex >= 0)
					{
						VisitSubTree(node.boxData.middleChild, visitor);
					}
					else
					{
						nodeIndex = node.boxData.middleChild;
					}
				}
			}

			if (node.highChild >= 0)
			{
				if (nodeIndex >= 0)
				{
					VisitSubTree(node.highChild, visitor);
				}
				else
				{
					nodeIndex = node.highChild;
				}
			}
		}
	}

	template <typename TElement, class TTraits, class TElementAllocator>
	auto BoxTreeStatic<TElement, TTraits, TElementAllocator>::BeginRangeQuery(BoxType const& range) const -> RangeQueryIterator
	{
		return RangeQueryIterator{ GetRootNode(), range };
	}

	template <typename TElement, class TTraits, class TElementAllocator>
	void BoxTreeStatic<TElement, TTraits, TElementAllocator>::Create(std::vector<TElement, TElementAllocator> elements)
	{
		elements_ = std::move(elements);
		nodes_.clear();
		nodes_.reserve(std::max(size_t(4), elements_.size() / maxElementsPerNode_ / 2));
		nodes_.emplace_back(Node{ IndexType(Size(elements_)), Bound(elements_) });
		if (!IsEmpty())
		{
			Build();
		}
	}

	template <typename TElement, class TTraits, class TElementAllocator>
	void BoxTreeStatic<TElement, TTraits, TElementAllocator>::Build()
	{
		using TempAllocator = typename std::allocator_traits<TElementAllocator>::template rebind_alloc<int>;
		std::vector<int, TempAllocator> nodeStack{ TempAllocator{ elements_.get_allocator() } };
		nodeStack.reserve(16);
		nodeStack.push_back(0);

		while (!nodeStack.empty())
		{
			auto const currentNodeIndex = nodeStack.back();
			nodeStack.pop_back();
			if (!SplitNode(currentNodeIndex))
			{
				continue;
			}

			auto const& currentNode = nodes_[currentNodeIndex];
			if (currentNode.lowChild >= 0)
			{
				nodeStack.push_back(currentNode.lowChild);
			}

			if constexpr (KeyIsBox)
			{
				if (currentNode.boxData.middleChild >= 0)
				{
					nodeStack.push_back(currentNode.boxData.middleChild);
				}
			}

			if (currentNode.highChild >= 0)
			{
				nodeStack.push_back(currentNode.highChild);
			}
		}
	}

	template <typename TElement, class TTraits, class TElementAllocator>
	bool BoxTreeStatic<TElement, TTraits, TElementAllocator>::SplitNode(int nodeIndex)
	{
		auto& node = nodes_[nodeIndex];
		auto const elementsCount = node.GetElementsCount();
		if (elementsCount <= maxElementsPerNode_)
		{
			return false;
		}

		// Pick splitting axis
		auto const sizes = node.box.Sizes();
		if constexpr (!KeyIsBox)
		{
			auto const [size, axis] = VectorTraitsType::MaximumValue(sizes);
			return SplitNode(nodeIndex, int(axis), node.box.Min()[axis] + size / 2);
		}
		else
		{
			auto splitAxis = -1;
			auto maxSize = ScalarType(0);
			for (auto axisIndex = 0; axisIndex < int(VectorTraitsType::Dimensions); ++axisIndex)
			{
				if (sizes[axisIndex] > maxSize && !node.IsAxisLocked(axisIndex))
				{
					maxSize = sizes[axisIndex];
					splitAxis = axisIndex;
				}
			}

			return splitAxis >= 0 && SplitNode(nodeIndex, splitAxis, node.box.Min()[splitAxis] + maxSize / 2);
		}
	}

	template <typename TElement, class TTraits, class TElementAllocator>
	bool BoxTreeStatic<TElement, TTraits, TElementAllocator>::SplitNode(int nodeIndex, int splitAxis, ScalarType splitPosition)
	{
		auto node = nodes_.data() + nodeIndex;
		auto const elementsCount = node->GetElementsCount();

		auto [lowCount, highCount] = Partition(*node, splitAxis, splitPosition);

		if constexpr (KeyIsBox)
		{
			if (lowCount + highCount < (elementsCount + 3) / 4)
			{
				return false;
			}
		}

		int lowChild, highChild;
		if (lowCount > 0)
		{
			BoxType newBox;
			if constexpr (NewBoxStrategy == 0)
			{
				auto highEnd = node->box.Max();
				highEnd[splitAxis] = splitPosition;
				newBox = BoxType{ node->box.Min(), highEnd };
			}
			else if constexpr (NewBoxStrategy == 1)
			{
				newBox = node->box;
				ReduceBoxAtHighEnd(newBox, node->elementsBegin, lowCount, splitAxis);
			}
			else
			{
				newBox = Bound(Span{ elements_.data() + node->elementsBegin, lowCount });
			}

			lowChild = int(nodes_.size());
			nodes_.emplace_back(Node{ nodeIndex, node->elementsBegin, node->elementsBegin + lowCount, newBox });
			node = nodes_.data() + nodeIndex;
		}
		else
		{
			lowChild = -1;
		}

		if constexpr (KeyIsBox)
		{
			auto const middleCount = elementsCount - lowCount - highCount;
			if (middleCount > 0)
			{
				BoxType newBox;
				if constexpr (NewBoxStrategy < 2)
				{
					newBox = node->box;
					ReduceBoxAtLowEnd(newBox, node->elementsBegin + lowCount, middleCount, splitAxis);
					ReduceBoxAtHighEnd(newBox, node->elementsBegin + lowCount, middleCount, splitAxis);
				}
				else
				{
					newBox = Bound(Span{ elements_.data() + node->elementsBegin + lowCount, middleCount });
				}

				node->boxData.middleChild = int(nodes_.size());
				auto& middleNode = nodes_.emplace_back(Node{ nodeIndex, node->elementsBegin + lowCount, node->elementsEnd - highCount, newBox });
				middleNode.boxData.lockedAxesMask |= 1 << splitAxis;
				node = nodes_.data() + nodeIndex;
			}
		}

		if (highCount > 0)
		{
			BoxType newBox;
			if constexpr (NewBoxStrategy == 0)
			{
				auto lowEnd = node->box.Min();
				lowEnd[splitAxis] = splitPosition;
				newBox = BoxType{ lowEnd, node->box.Max() };
			}
			else if constexpr (NewBoxStrategy == 1)
			{
				newBox = node->box;
				ReduceBoxAtLowEnd(newBox, node->elementsEnd - highCount, highCount, splitAxis);
			}
			else
			{
				newBox = Bound(Span{ elements_.data() + node->elementsEnd - highCount, highCount });
			}

			highChild = int(nodes_.size());
			nodes_.emplace_back(Node{ nodeIndex, node->elementsEnd - highCount, node->elementsEnd, newBox });
			node = nodes_.data() + nodeIndex;
		}
		else
		{
			highChild = -1;
		}

		node->Split(splitAxis, lowChild, highChild);
		return true;
	}

	template <typename TElement, class TTraits, class TElementAllocator>
	auto BoxTreeStatic<TElement, TTraits, TElementAllocator>::PartitionPoints(Node const& node, int splitAxis, ScalarType splitPosition) -> std::pair<IndexType, IndexType>
	{
		using std::swap;

		auto currentLow = node.elementsBegin;
		auto currentHigh = node.elementsEnd - 1;

		for (;;)
		{
			for (; currentLow <= currentHigh; ++currentLow)
			{
				auto const& key = traits_.GetSpatialKey(elements_[currentLow]);
				if (key[splitAxis] >= splitPosition)
				{
					break;
				}
			}

			for (; currentLow <= currentHigh; --currentHigh)
			{
				auto const& key = traits_.GetSpatialKey(elements_[currentHigh]);
				if (key[splitAxis] < splitPosition)
				{
					break;
				}
			}

			if (currentLow <= currentHigh)
			{
				swap(elements_[currentLow], elements_[currentHigh]);

				++currentLow;
				--currentHigh;
			}
			else
			{
				break;
			}
		}

		return { currentLow - node.elementsBegin, node.elementsEnd - currentLow };
	}

	template <typename TElement, class TTraits, class TElementAllocator>
	auto BoxTreeStatic<TElement, TTraits, TElementAllocator>::PartitionBoxes(Node const& node, int splitAxis, ScalarType& splitPosition) -> std::pair<IndexType, IndexType>
	{
		using std::swap;

		auto currentLow = node.elementsBegin, lowEnd = currentLow;
		auto currentHigh = node.elementsEnd - 1, highEnd = currentHigh;

		for (;;)
		{
			for (; currentLow <= currentHigh; ++currentLow)
			{
				auto const& box = traits_.GetSpatialKey(elements_[currentLow]);
				if (box.Min()[splitAxis] >= splitPosition)
				{
					break;
				}

				if (box.Max()[splitAxis] < splitPosition)
				{
					if (lowEnd < currentLow)
					{
						swap(elements_[lowEnd], elements_[currentLow]);
					}

					++lowEnd;
				}
			}

			// either one is true: element at currentLow is H, or currentLow > currentHigh
			for (; currentLow < currentHigh; --currentHigh)
			{
				auto const& box = traits_.GetSpatialKey(elements_[currentHigh]);
				if (box.Max()[splitAxis] < splitPosition)
				{
					break;
				}

				if (box.Min()[splitAxis] >= splitPosition)
				{
					if (currentHigh < highEnd)
					{
						swap(elements_[currentHigh], elements_[highEnd]);
					}

					--highEnd;
				}
			}

			if (currentLow < currentHigh)
			{
				if (lowEnd < currentLow)
				{
					if (currentHigh < highEnd)
					{
						// ... L M ... M (H) ... (L) M .. M H ...
						swap(elements_[lowEnd], elements_[currentHigh]);
						swap(elements_[currentLow], elements_[highEnd]);
					}
					else
					{
						// ... L M ... M (H) ... (L) H ...
						swap(elements_[lowEnd], elements_[currentLow]);
						swap(elements_[lowEnd], elements_[highEnd]);
					}
				}
				else
				{
					if (currentHigh < highEnd)
					{
						// ... L (H) ... (L) M .. M H ...
						swap(elements_[currentHigh], elements_[highEnd]);
						swap(elements_[lowEnd], elements_[highEnd]);
					}
					else
					{
						// ... L (H) ... (L) H ...
						swap(elements_[currentLow], elements_[currentHigh]);
					}
				}

				++lowEnd; ++currentLow;
				--highEnd; --currentHigh;
			}
			else
			{
				if (currentLow == currentHigh)
				{
					if (currentHigh < highEnd)
					{
						swap(elements_[currentLow], elements_[highEnd - 1]);
					}

					--highEnd;
				}

				break;
			}
		}

		return { lowEnd - node.elementsBegin, node.elementsEnd - 1 - highEnd };
	}


	struct BoxTreeStats
	{
		int maxHeight;
		AggregateStats<int> elementsPerNode;
		AggregateStats<double> middlePercent;
		AggregateStats<int> heightBalance;
		AggregateStats<int> elementsCountBalance;
	};

	template <typename TElement, class TTraits>
	BoxTreeStats CalcStats(BoxTreeStatic<TElement, TTraits> const& tree)
	{
		constexpr auto spatialKeyIsBox = SpatialKeyIsBox<typename BoxTreeStatic<TElement, TTraits>::SpatialKeyType>;

		struct NodeStats
		{
			int depth = 0, height = 1, totalElements = 0;
		};

		BoxTreeStats result;

		std::vector<NodeStats> stats(tree.GetNodesCount());
		auto nodeIndex = 0;
		auto const nodes = tree.Nodes();
		for (auto const& node : nodes)
		{
			stats[nodeIndex].depth = node.parent < 0 ? 1 : stats[node.parent].depth + 1;
			++nodeIndex;
		}

		nodeIndex = int(nodes.size());
		std::array<int, 3> children{};
		auto childrenCount = 0;
		auto const addChild = [&](int childIndex)
			{
				if (childIndex >= 0)
				{
					children[childrenCount++] = childIndex;
				}
			};

		for (auto const& node : ReverseIterable(nodes))
		{
			--nodeIndex;
			auto& s = stats[nodeIndex];
			childrenCount = 0;
			addChild(node.GetLowChild());
			addChild(node.GetHighChild());
			if constexpr (spatialKeyIsBox)
			{
				addChild(node.boxData.middleChild);
			}

			s.height = 1;
			auto middleCount = s.totalElements = node.GetElementsCount();
			if (childrenCount == 0) // For elementsPerNode only take into account leaf nodes
			{
				result.elementsPerNode.AddValue(s.totalElements);
			}

			for (auto i = 0; i < childrenCount; ++i)
			{
				auto const& childStats = stats[children[i]];
				s.height = std::max(s.height, childStats.height + 1);
				s.totalElements += childStats.totalElements;
			}

			if (childrenCount > 0) // For balances and middlePercent only take into account non-leaf nodes
			{
				auto const lowChild = node.GetLowChild();
				auto const highChild = node.GetHighChild();
				auto const lowHeight = lowChild >= 0 ? stats[lowChild].height : 0;
				auto const lowCount = lowChild >= 0 ? stats[lowChild].totalElements : 0;
				auto const highHeight = highChild >= 0 ? stats[highChild].height : 0;
				auto const highCount = highChild >= 0 ? stats[highChild].totalElements : 0;
				result.heightBalance.AddValue(lowHeight - highHeight);
				result.elementsCountBalance.AddValue(lowCount - highCount);
				if constexpr (spatialKeyIsBox)
				{
					if (node.boxData.middleChild >= 0)
					{
						middleCount += stats[node.boxData.middleChild].totalElements;
					}

					result.middlePercent.AddValue(middleCount * 100.0 / s.totalElements);
				}
			}
		}

		result.maxHeight = stats.front().height;
		return result;
	}
}
