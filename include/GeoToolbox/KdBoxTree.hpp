// Copyright 2024-2026 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include "GeoToolbox/GeometryTools.hpp"
#include "GeoToolbox/Span.hpp"
#include "GeoToolbox/SpatialTools.hpp"

namespace GeoToolbox
{
	// The default implementation assumes that the tree element type TElement coincides with one of the supported spatial key types, that's either a vector or a Box<Vector>
	template <typename TElement>
	struct KdBoxTreeTraits
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
	struct KdBoxTreeTraits<Feature<TSpatialKey> const*>
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
		struct KdBoxTreeNode_BoxData
		{
			int middleChild = -1;
			// An std::bitset would serve better, but unfortunately it's size cannot be controlled, and GCC uses 64 bits while 32 are enough for us
			std::uint32_t lockedAxesMask = 0;
		};

		template <>
		struct KdBoxTreeNode_BoxData<0>
		{
		};

		template <class TVector>
		[[nodiscard]] auto GetMaxDistanceSquared(TVector const& point, Box<TVector> const& box)
		{
			typename VectorTraits<TVector>::ScalarType result{ 0 };
			auto const center = box.Center();
			for (auto i = 0; i < VectorTraits<TVector>::Dimensions; ++i)
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

	// A static k-d tree that supports boxes. The boxes that intersect the splitting plane are pushed into a new node that gets split further down by the other axes.
	// StoreSpatialKeys makes the tree keep its own copy of the elements' spatial keys, which speeds up both the build and the queries at the cost of the extra memory
	template <typename TElement, bool StoreSpatialKeys = false, class TTraits = KdBoxTreeTraits<TElement>, class TElementAllocator = std::allocator<TElement>>
	class KdBoxTree
	{
	public:

		static_assert(std::is_move_constructible_v<TElement>&& std::is_move_assignable_v<TElement>);

		struct Node;

	private:

		using NodeAllocatorType = typename std::allocator_traits<TElementAllocator>::template rebind_alloc<Node>;

		using KeyAllocatorType = typename std::allocator_traits<TElementAllocator>::template rebind_alloc<typename TTraits::SpatialKeyType>;

		std::vector<TElement, TElementAllocator> elements_;

		// The spatial keys of elements_, in the same order. Left empty unless StoreSpatialKeys - then every key access reads it instead of chasing the element
		std::vector<typename TTraits::SpatialKeyType, KeyAllocatorType> keys_;

		std::vector<Node, NodeAllocatorType> nodes_;

		/* C++20: [[no_unique_address]] */ TTraits traits_;

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


		explicit KdBoxTree(std::vector<TElement, TElementAllocator> elements, int maxElementsPerNode = 0, TTraits traits = {})
			: elements_{ TElementAllocator(elements.get_allocator()) }
			, keys_(KeyAllocatorType(elements.get_allocator()))
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
			// An empty tree's single root node has an empty box, which Contains() below rejects; nothing to visit anyway
			if (elements_.empty())
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
						if (Overlap(range, GetSpatialKey(elementIndex)))
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

			if (nearestCount > 0)
			{
				result.reserve(nearestCount + 1);
			}
			else
			{
				ASSERT(maxDistance > 0);
				nearestCount = -1;
				result.reserve(32);
			}

			auto const maxPossibleDistance2 = Detail::GetMaxDistanceSquared(targetLocation, nodes_[0].box);

			NearQueryData data{ targetLocation, nearestCount, maxDistance <= 0 ? maxPossibleDistance2 : std::min(maxPossibleDistance2, Square(maxDistance)), result };
			auto const distanceToFullBox = GetDistanceSquared(targetLocation, nodes_[0].box);
			if (distanceToFullBox <= data.worstDistance2)
			{
				QueryNearest_(data, nodes_[0]);
			}

			return int(result.size());
		}

		[[nodiscard]] std::vector<std::pair<IndexType, ScalarType>> QueryNearest(VectorType const& targetLocation, int nearestCount = 0, ScalarType maxDistance = -1) const
		{
			std::vector<std::pair<IndexType, ScalarType>> result;
			QueryNearest(result, targetLocation, nearestCount, maxDistance);
			return result;
		}

	private:

		[[nodiscard]] decltype(auto) GetSpatialKey(IndexType elementIndex) const
		{
			if constexpr (StoreSpatialKeys)
			{
				return keys_[elementIndex];
			}
			else
			{
				return traits_.GetSpatialKey(elements_[elementIndex]);
			}
		}

		// The keys must stay in the elements' order, so every reordering during the build goes through here
		void SwapElements(IndexType a, IndexType b)
		{
			using std::swap;
			swap(elements_[a], elements_[b]);
			if constexpr (StoreSpatialKeys)
			{
				swap(keys_[a], keys_[b]);
			}
		}

		struct NearQueryData
		{
			VectorType const& targetLocation;
			int nearestCount;
			ScalarType worstDistance2;
			std::vector<std::pair<IndexType, ScalarType>>& result{};
		};

		void QueryNearest_VisitLeaf( NearQueryData& data, Node const& currentNode ) const
		{
			AddQueryStats_VisitedNodesCount();

			for ( auto elementIndex = currentNode.elementsBegin; elementIndex < currentNode.elementsEnd; ++elementIndex )
			{
				AddQueryStats_ObjectTestsCount();
				auto const distance2 = GetDistanceSquared( data.targetLocation, GetSpatialKey( elementIndex ) );
				if ( distance2 > data.worstDistance2 )
				{
					continue;
				}

				auto position = std::lower_bound( data.result.begin(), data.result.end(), distance2, []( auto const& pair, ScalarType v )
				{
					return pair.second < v;
				} );
				data.result.emplace( position, elementIndex, distance2 );
				if ( Size( data.result ) == data.nearestCount + 1 )
				{
					data.result.erase( --data.result.end() );
					data.worstDistance2 = data.result.back().second;
				}
			}
		}

		void QueryNearest_(NearQueryData& data, Node const& currentNode) const
		{
			if (currentNode.IsLeaf())
			{
				QueryNearest_VisitLeaf( data, currentNode );

				return;
			}

			if constexpr (!KeyIsBox)
			{
				std::pair<int, ScalarType> lowCandidate{ currentNode.lowChild, GetDistanceSquared(data.targetLocation, nodes_[currentNode.lowChild].box) };
				std::pair<int, ScalarType> highCandidate{ currentNode.highChild, GetDistanceSquared(data.targetLocation, nodes_[currentNode.highChild].box) };
				auto* closer = &lowCandidate;
				auto* farther = &highCandidate;
				if (lowCandidate.second > highCandidate.second)
				{
					std::swap(closer, farther);
				}

				if (closer->second > data.worstDistance2)
				{
					return;
				}

				QueryNearest_(data, nodes_[closer->first]);
				if (farther->second <= data.worstDistance2)
				{
					QueryNearest_(data, nodes_[farther->first]);
				}
			}
			else
			{
				// The three children (low, straddling middle, high) are visited nearest-first so the closest one shrinks worstDistance2 before the others are tested for pruning.
				// The middle child's box straddles the split plane and is often not the nearest, so ordering it by distance rather than always visiting it first prunes more.
				std::array<std::pair<int, ScalarType>, 3> candidates;
				auto count = 0;
				for (auto child : { currentNode.lowChild, currentNode.boxData.middleChild, currentNode.highChild })
				{
					if (child >= 0)
					{
						candidates[count++] = { child, GetDistanceSquared(data.targetLocation, nodes_[child].box) };
					}
				}

				for (auto i = 1; i < count; ++i)
				{
					auto const candidate = candidates[i];
					auto j = i;
					for (; j > 0 && candidates[j - 1].second > candidate.second; --j)
					{
						candidates[j] = candidates[j - 1];
					}

					candidates[j] = candidate;
				}

				for (auto i = 0; i < count && candidates[i].second <= data.worstDistance2; ++i)
				{
					QueryNearest_(data, nodes_[candidates[i].first]);
				}
			}
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
				newLimit = std::min(newLimit, GetLowBound(GetSpatialKey(i), axis));
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
				newLimit = std::max(newLimit, GetHighBound(GetSpatialKey(i), axis));
			}

			auto newEnd = box[1];
			newEnd[axis] = newLimit;
			box = { box[0], newEnd };
		}

		template <class TVisitor>
		void VisitSubTree(int rootNodeIndex, TVisitor&& visitor) const;

		[[nodiscard]] bool OverlapWithNode(BoxType const& range, int nodeIndex) const
		{
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

		[[nodiscard]] auto Bound(IndexType startIndex, IndexType count) const
		{
			if constexpr (StoreSpatialKeys)
			{
				return GeoToolbox::Bound(Span{ keys_.data() + startIndex, count });
			}
			else
			{
				return GeoToolbox::Bound(Span{ elements_.data() + startIndex, count }, [this](TElement const& x) { return traits_.GetSpatialKey(x); });
			}
		}
	};

	template <typename TElement, bool StoreSpatialKeys, class TTraits, class TElementAllocator>
	struct KdBoxTree<TElement, StoreSpatialKeys, TTraits, TElementAllocator>::Node
	{
		friend class KdBoxTree;

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
		int8_t splitAxis;
		Detail::KdBoxTreeNode_BoxData<IsSpecialization<SpatialKeyType, Box> ? VectorTraits<VectorType>::Dimensions : 0> boxData;
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
				// Must match the low -> middle -> high order that NodeIterator::GetNextSibling walks
				return lowChild >= 0 ? lowChild
					: boxData.middleChild >= 0 ? boxData.middleChild
					: highChild;
			}
			else
			{
				return lowChild >= 0 ? lowChild : highChild;
			}
		}

		void Split(int axis, int lowChild_, int highChild_)
		{
			splitAxis = int8_t(axis);
			lowChild = lowChild_;
			highChild = highChild_;
		}
	};

	template <typename TElement, bool StoreSpatialKeys, class TTraits, class TElementAllocator>
	class KdBoxTree<TElement, StoreSpatialKeys, TTraits, TElementAllocator>::NodeIterator
	{
	protected:
		KdBoxTree const* tree_;
		int nodeIndex_;
		bool down_ = true;

	public:

		using value_type = Node;
		using pointer = value_type const*;
		using reference = value_type const&;
		using iterator_category = std::forward_iterator_tag;
		using difference_type = ptrdiff_t;

		NodeIterator(KdBoxTree const& tree, int nodeIndex)
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
					// Must match the low -> middle -> high order that Node::GetFirstChild walks
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

	template <typename TElement, bool StoreSpatialKeys, class TTraits, class TElementAllocator>
	class KdBoxTree<TElement, StoreSpatialKeys, TTraits, TElementAllocator>::RangeQueryIterator : public NodeIterator
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
						if (Overlap(range_, this->tree_->GetSpatialKey(elementIndex_)))
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


	template <typename TElement, bool StoreSpatialKeys, class TTraits, class TElementAllocator>
	template <class TVisitor>
	void KdBoxTree<TElement, StoreSpatialKeys, TTraits, TElementAllocator>::VisitSubTree(int nodeIndex, TVisitor&& visitor) const
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

	template <typename TElement, bool StoreSpatialKeys, class TTraits, class TElementAllocator>
	auto KdBoxTree<TElement, StoreSpatialKeys, TTraits, TElementAllocator>::BeginRangeQuery(BoxType const& range) const -> RangeQueryIterator
	{
		return RangeQueryIterator{ GetRootNode(), range };
	}

	template <typename TElement, bool StoreSpatialKeys, class TTraits, class TElementAllocator>
	void KdBoxTree<TElement, StoreSpatialKeys, TTraits, TElementAllocator>::Create(std::vector<TElement, TElementAllocator> elements)
	{
		elements_ = std::move(elements);
		if constexpr (StoreSpatialKeys)
		{
			keys_.clear();
			keys_.reserve(elements_.size());
			for (auto const& element : elements_)
			{
				keys_.push_back(traits_.GetSpatialKey(element));
			}
		}

		nodes_.clear();
		nodes_.reserve(std::max(size_t(4), elements_.size() / maxElementsPerNode_ / 2));
		nodes_.emplace_back(Node{ IndexType(Size(elements_)), Bound(0, IndexType(Size(elements_))) });
		if (!IsEmpty())
		{
			Build();
		}
	}

	template <typename TElement, bool StoreSpatialKeys, class TTraits, class TElementAllocator>
	void KdBoxTree<TElement, StoreSpatialKeys, TTraits, TElementAllocator>::Build()
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

	template <typename TElement, bool StoreSpatialKeys, class TTraits, class TElementAllocator>
	bool KdBoxTree<TElement, StoreSpatialKeys, TTraits, TElementAllocator>::SplitNode(int nodeIndex)
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
			for (auto axisIndex = 0; axisIndex < VectorTraitsType::Dimensions; ++axisIndex)
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

	template <typename TElement, bool StoreSpatialKeys, class TTraits, class TElementAllocator>
	bool KdBoxTree<TElement, StoreSpatialKeys, TTraits, TElementAllocator>::SplitNode(int nodeIndex, int splitAxis, ScalarType splitPosition)
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
		else
		{
			// This may happen with coincident points (zero-size box), or if the midpoint rounds back to an endpoint
			if (lowCount == 0 || highCount == 0)
			{
				return false;
			}
		}

		int lowChild, highChild;
		if (lowCount > 0)
		{
			auto const newBox = Bound(node->elementsBegin, lowCount);
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
				auto const newBox = Bound(node->elementsBegin + lowCount, middleCount);
				node->boxData.middleChild = int(nodes_.size());
				auto& middleNode = nodes_.emplace_back(Node{ nodeIndex, node->elementsBegin + lowCount, node->elementsEnd - highCount, newBox });
				middleNode.boxData.lockedAxesMask |= 1 << splitAxis;
				node = nodes_.data() + nodeIndex;
			}
		}

		if (highCount > 0)
		{
			auto const newBox = Bound(node->elementsEnd - highCount, highCount);
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

	template <typename TElement, bool StoreSpatialKeys, class TTraits, class TElementAllocator>
	auto KdBoxTree<TElement, StoreSpatialKeys, TTraits, TElementAllocator>::PartitionPoints(Node const& node, int splitAxis, ScalarType splitPosition) -> std::pair<IndexType, IndexType>
	{
		auto currentLow = node.elementsBegin;
		auto currentHigh = node.elementsEnd - 1;

		for (;;)
		{
			for (; currentLow <= currentHigh; ++currentLow)
			{
				auto const& key = GetSpatialKey(currentLow);
				if (key[splitAxis] >= splitPosition)
				{
					break;
				}
			}

			for (; currentLow <= currentHigh; --currentHigh)
			{
				auto const& key = GetSpatialKey(currentHigh);
				if (key[splitAxis] < splitPosition)
				{
					break;
				}
			}

			if (currentLow <= currentHigh)
			{
				SwapElements(currentLow, currentHigh);

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

	template <typename TElement, bool StoreSpatialKeys, class TTraits, class TElementAllocator>
	auto KdBoxTree<TElement, StoreSpatialKeys, TTraits, TElementAllocator>::PartitionBoxes(Node const& node, int splitAxis, ScalarType& splitPosition) -> std::pair<IndexType, IndexType>
	{
		// lowEnd - the first non-low element, highEnd - the last non-high element
		auto currentLow = node.elementsBegin, lowEnd = currentLow;
		auto currentHigh = node.elementsEnd - 1, highEnd = currentHigh;

		for (;;)
		{
			for (; currentLow <= currentHigh; ++currentLow)
			{
				auto const& box = GetSpatialKey(currentLow);
				if (box.Min()[splitAxis] >= splitPosition)
				{
					break;
				}

				if (box.Max()[splitAxis] < splitPosition)
				{
					if (lowEnd < currentLow)
					{
						SwapElements(lowEnd, currentLow);
					}

					++lowEnd;
				}
			}

			// either one is true: element at currentLow is H, or currentLow > currentHigh
			for (; currentLow < currentHigh; --currentHigh)
			{
				auto const& box = GetSpatialKey(currentHigh);
				if (box.Max()[splitAxis] < splitPosition)
				{
					break;
				}

				if (box.Min()[splitAxis] >= splitPosition)
				{
					if (currentHigh < highEnd)
					{
						SwapElements(currentHigh, highEnd);
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
						SwapElements(lowEnd, currentHigh);
						SwapElements(currentLow, highEnd);
					}
					else
					{
						// ... L M ... M (H) ... (L) H ...
						SwapElements(lowEnd, currentLow);
						SwapElements(lowEnd, highEnd);
					}
				}
				else
				{
					if (currentHigh < highEnd)
					{
						// ... L (H) ... (L) M .. M H ...
						SwapElements(currentHigh, highEnd);
						SwapElements(lowEnd, highEnd);
					}
					else
					{
						// ... L (H) ... (L) H ...
						SwapElements(currentLow, currentHigh);
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
						// ... L ((H)) M .. M H ...
						SwapElements(currentLow, highEnd);
					}

					--highEnd;
				}

				break;
			}
		}

		return { lowEnd - node.elementsBegin, node.elementsEnd - 1 - highEnd };
	}
}
