// Copyright 2024-2025 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include "GeoToolbox/GeometryTools.hpp"
#include "GeoToolbox/Span.hpp"
#include "GeoToolbox/SpatialTools.hpp"

#include <bitset>

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

	namespace Detail
	{
		template <int NDimensions>
		struct BoxTreeStaticNode_BoxData
		{
			int middleChild = -1;
			std::bitset<NDimensions> lockedAxesMask{};
		};

		template <>
		struct BoxTreeStaticNode_BoxData<0>
		{
		};
	}

	// A static k-d tree that supports boxes. The boxes that intersect the splitting line are pushed into a new node that gets split further down by the other axes
	template <typename TElement, class TTraits = BoxTreeTraits<TElement>, class TElementAllocator = std::allocator<TElement>>
	class BoxTreeStatic
	{
		static_assert(std::is_move_constructible_v<TElement>&& std::is_move_assignable_v<TElement>);

		struct Node;

		using NodeAllocatorType = typename std::allocator_traits<TElementAllocator>::template rebind_alloc<Node>;

		std::vector<TElement, TElementAllocator> elements_;

		std::vector<Node, NodeAllocatorType> nodes_;

		int maxElementsPerNode_;

	public:

		static constexpr auto MaxElementsPerNode = 64;

		using ElementType = TElement;
		using SpatialKeyType = typename TTraits::SpatialKeyType;
		using IndexType = int;

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


		BoxTreeStatic(int maxElementsPerNode = 0, TElementAllocator const& elementAllocator = TElementAllocator{})
			: elements_(elementAllocator)
			, nodes_(NodeAllocatorType(elementAllocator))
			, maxElementsPerNode_{ maxElementsPerNode > 0 ? maxElementsPerNode : MaxElementsPerNode }
		{
		}

		explicit BoxTreeStatic(std::vector<TElement, TElementAllocator> elements, int maxElementsPerNode = 0)
			: BoxTreeStatic{ maxElementsPerNode, elements.get_allocator() }
		{
			Create(std::move(elements));
		}

		void Create(std::vector<TElement, TElementAllocator> elements);

		[[nodiscard]] bool IsEmpty() const noexcept
		{
			return elements_.empty();
		}

		[[nodiscard]] Span<ElementType const> Elements() const
		{
			return elements_;
		}

		[[nodiscard]] int GetMaxElementsPerNode() const noexcept
		{
			return maxElementsPerNode_;
		}

		[[nodiscard]] int GetNodesCount() const noexcept
		{
			return int(Size(nodes_));
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

		[[nodiscard]] RangeQueryIterator BeginRangeQuery(BoxType const& range) const
		{
			return BeginRangeQuery(GetRootNode(), range);
		}

		[[nodiscard]] RangeQueryIterator BeginRangeQuery(NodeIterator startRoot, BoxType const& range) const;

		[[nodiscard]] RangeQueryIterator EndRangeQuery() const
		{
			return RangeQueryIterator{ EndNodes() };
		}

		[[nodiscard]] std::vector<std::pair<IndexType, ScalarType>> QueryNearest(VectorType const& targetLocation, int nearestCount = 0, ScalarType maxDistance = -1) const;

	private:

		void Build();

		void SplitNode(int nodeIndex);

		IndexType PartitionPoints(Node const&, int splitAxis, ScalarType splitPosition);

		std::pair<IndexType, IndexType> PartitionBoxes(Node const&, int splitAxis, ScalarType& splitPosition);

		void ReduceBoxLeft(BoxType& box, IndexType startIndex, IndexType count, int axis)
		{
			auto newLimit = box[1][axis];
			auto const end = startIndex + count;
			for (auto i = startIndex; i < end; ++i)
			{
				newLimit = std::min(newLimit, GetLowBound(TTraits::GetSpatialKey(elements_[i]), axis));
			}

			auto newEnd = box[0];
			newEnd[axis] = newLimit;
			box = { newEnd, box[1] };
		}

		void ReduceBoxRight(BoxType& box, IndexType startIndex, IndexType count, int axis)
		{
			auto newLimit = box[0][axis];
			auto const end = startIndex + count;
			for (auto i = startIndex; i < end; ++i)
			{
				newLimit = std::max(newLimit, GetHighBound(TTraits::GetSpatialKey(elements_[i]), axis));
			}

			auto newEnd = box[1];
			newEnd[axis] = newLimit;
			box = { box[0], newEnd };
		}

		[[nodiscard]] bool OverlapWithNode(BoxType const& range, int nodeIndex) const
		{
			AddQueryStats_BoxOverlapsCount();
			return Overlap(range, nodes_[nodeIndex].box);
		}

		[[nodiscard]] int GetFirstChildOverlap(int nodeIndex, BoxType const& range) const
		{
			auto const& node = nodes_[nodeIndex];
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

		[[nodiscard]] int GetLeftOrRightNear(Node const& node, VectorType const& location, ScalarType worstDistance2) const
		{
			if (location[node.splitAxis] < node.splitPosition)
			{
				if (node.lowChild >= 0)
				{
					return node.lowChild;
				}

				return node.highChild >= 0 && Square(node.splitPosition - location[node.splitAxis]) < worstDistance2 ? node.highChild : -1;
			}

			if (node.highChild >= 0)
			{
				return node.highChild;
			}
			
			return node.lowChild >= 0 && Square(location[node.splitAxis] - node.splitPosition) < worstDistance2 ? node.lowChild : -1;
		}

		[[nodiscard]] int GetFirstChildNear(int nodeIndex, VectorType const& location, ScalarType worstDistance2) const
		{
			auto const& node = nodes_[nodeIndex];
			if (node.splitAxis < 0)
			{
				return -1;
			}

			if constexpr (KeyIsBox)
			{
				if (node.boxData.middleChild >= 0)
				{
					return node.boxData.middleChild;
				}
			}

			return GetLeftOrRightNear(node, location, worstDistance2);
		}

		[[nodiscard]] int GetNextSiblingNear(int nodeIndex, VectorType const& location, ScalarType worstDistance2) const
		{
			if (nodes_[nodeIndex].parent < 0)
			{
				return -1;
			}

			auto const& parentNode = nodes_[nodes_[nodeIndex].parent];

			if constexpr (KeyIsBox)
			{
				if (nodeIndex == parentNode.boxData.middleChild)
				{
					return GetLeftOrRightNear(parentNode, location, worstDistance2);
				}
			}

			if (nodeIndex == parentNode.lowChild)
			{
				if (location[parentNode.splitAxis] >= parentNode.splitPosition)
				{
					return -1;
				}

				return parentNode.highChild >= 0 && Square(parentNode.splitPosition - location[parentNode.splitAxis]) < worstDistance2 ? parentNode.highChild : -1;
			}

			DEBUG_ASSERT(nodeIndex == parentNode.highChild);
			if (location[parentNode.splitAxis] < parentNode.splitPosition)
			{
				return -1;
			}

			return parentNode.lowChild >= 0 && Square(location[parentNode.splitAxis] - parentNode.splitPosition) < worstDistance2 ? parentNode.lowChild : -1;
		}
	};

	template <typename TElement, class TTraits, class TElementAllocator>
	struct BoxTreeStatic<TElement, TTraits, TElementAllocator>::Node
	{
		int parent;
		int lowChild = -1;
		int highChild = -1;
		IndexType elementsBegin = -1, elementsEnd = -1;
		Box<VectorType> box{};
		ScalarType splitPosition = 0;
		char splitAxis = -1;
		/* [[no_unique_address]] */ Detail::BoxTreeStaticNode_BoxData<IsSpecialization<SpatialKeyType, Box> ? VectorTraits<VectorType>::Dimensions : 0> boxData{};

		[[nodiscard]] bool IsAxisLocked(int axis) const noexcept(!IsSpecialization<SpatialKeyType, Box>)
		{
			if constexpr (IsSpecialization<SpatialKeyType, Box>)
			{
				return boxData.lockedAxesMask.test(axis);
			}
			else
			{
				return false;
			}
		}

		[[nodiscard]] IndexType GetElementCount() const noexcept
		{
			return elementsEnd - elementsBegin;
		}

		[[nodiscard]] bool HasElements() const noexcept
		{
			return GetElementCount() > 0;
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
			return { *tree_, GetNode().lowChild };
		}

		[[nodiscard]] NodeIterator GetMiddleChild() const
		{
			return { *tree_, GetNode().boxData.middleChild };
		}

		[[nodiscard]] NodeIterator GetHighChild() const
		{
			return { *tree_, GetNode().highChild };
		}

		[[nodiscard]] int GetFirstChild() const
		{
			DEBUG_ASSERT(IsValid());
			auto const& node = GetNode();
			if constexpr (KeyIsBox)
			{
				return node.lowChild >= 0 ? node.lowChild
					: node.highChild >= 0 ? node.highChild
					: node.boxData.middleChild;
			}
			else
			{
				return node.lowChild >= 0 ? node.lowChild : node.highChild;
			}
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
				elementIndex_ = this->tree_->nodes_[this->nodeIndex_].elementsBegin;
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
				for (; elementIndex_ < this->GetNode().elementsEnd; ++elementIndex_)
				{
					AddQueryStats_ObjectOverlapsCount();
					if (Overlap(range_, TTraits::GetSpatialKey(operator*())))
					{
						return;
					}
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

				this->elementIndex_ = this->GetNode().elementsBegin;
			}
		}
	};


	template <typename TElement, class TTraits, class TElementAllocator>
	auto BoxTreeStatic<TElement, TTraits, TElementAllocator>::BeginRangeQuery(NodeIterator startRoot, BoxType const& range) const -> RangeQueryIterator
	{
		return RangeQueryIterator{ startRoot, range };
	}

	template <typename TElement, class TTraits, class TElementAllocator>
	void BoxTreeStatic<TElement, TTraits, TElementAllocator>::Create(std::vector<TElement, TElementAllocator> elements)
	{
		elements_ = std::move(elements);
		nodes_.clear();
		nodes_.reserve(std::max(size_t(4), elements_.size() / maxElementsPerNode_ / 2));
		nodes_.emplace_back(Node{ -1, -1, -1, 0, int(Size(elements_)), Bound(elements_, TTraits::GetSpatialKey) });
		if (!IsEmpty())
		{
			Build();
		}
	}

	template <typename TElement, class TTraits, class TElementAllocator>
	void BoxTreeStatic<TElement, TTraits, TElementAllocator>::Build()
	{
		using TempAllocator = typename std::allocator_traits<TElementAllocator>::template rebind_alloc<int>;
		std::vector<int, TempAllocator> nodeQueue{ TempAllocator{ elements_.get_allocator() } };
		nodeQueue.reserve(16);
		nodeQueue.push_back(0);

		while (!nodeQueue.empty())
		{
			auto const currentNodeIndex = nodeQueue.back();
			nodeQueue.pop_back();
			SplitNode(currentNodeIndex);
			auto const& currentNode = nodes_[currentNodeIndex];
			if (currentNode.lowChild >= 0)
			{
				nodeQueue.push_back(currentNode.lowChild);
			}

			if constexpr (KeyIsBox)
			{
				if (currentNode.boxData.middleChild >= 0)
				{
					nodeQueue.push_back(currentNode.boxData.middleChild);
				}
			}

			if (currentNode.highChild >= 0)
			{
				nodeQueue.push_back(currentNode.highChild);
			}
		}
	}

	template <typename TElement, class TTraits, class TElementAllocator>
	void BoxTreeStatic<TElement, TTraits, TElementAllocator>::SplitNode(int nodeIndex)
	{
		auto node = nodes_.data() + nodeIndex;
		auto const elementCount = node->GetElementCount();
		if (elementCount <= maxElementsPerNode_)
		{
			return;
		}

		// Pick splitting axis
		auto sizes = VectorTraitsType::ToArray(node->box.Sizes());
		auto maxSize = ScalarType(0);
		auto splitAxis = -1;
		for (auto axisIndex = 0; axisIndex < int(VectorTraitsType::Dimensions); ++axisIndex)
		{
			if (sizes[axisIndex] > maxSize && !node->IsAxisLocked(axisIndex))
			{
				maxSize = sizes[axisIndex];
				splitAxis = axisIndex;
			}
		}

		if (splitAxis < 0)
		{
			return;
		}

		auto splitPosition = node->box.Min()[splitAxis] + maxSize / 2;
		auto lowCount = 0, highCount = 0;
		if constexpr (KeyIsBox)
		{
			std::tie(lowCount, highCount) = PartitionBoxes(*node, splitAxis, splitPosition);
		}
		else
		{
			lowCount = PartitionPoints(*node, splitAxis, splitPosition);
			highCount = elementCount - lowCount;
		}

		if constexpr (KeyIsBox)
		{
			if (lowCount + highCount < (elementCount + 3) / 4)
			{
				return;
			}
		}

		node->splitAxis = char(splitAxis);
		node->splitPosition = splitPosition;
		if (lowCount > 0)
		{
			//auto const& box = Bound(Span{ elements_.data() + node->elementsBegin, lowCount }, TTraits::GetSpatialKey);
			auto box = node->box;
			ReduceBoxRight(box, node->elementsBegin, lowCount, splitAxis);
			//auto highEnd = node->box.Max();
			//highEnd[splitAxis] = splitPosition;
			//auto const box = BoxType{ node->box.Min(), highEnd };
			node->lowChild = int(nodes_.size());
			nodes_.emplace_back(Node{ nodeIndex, -1, -1, node->elementsBegin, node->elementsBegin + lowCount, box });
			node = nodes_.data() + nodeIndex;
		}

		if (highCount > 0)
		{
			//auto const& box = Bound(Span{ elements_.data() + node->elementsEnd - highCount, highCount }, TTraits::GetSpatialKey);
			auto box = node->box;
			ReduceBoxLeft(box, node->elementsEnd - highCount, highCount, splitAxis);
			//auto lowEnd = node->box.Min();
			//lowEnd[splitAxis] = splitPosition;
			//auto const box = BoxType{ lowEnd, node->box.Max() };
			node->highChild = int(nodes_.size());
			nodes_.emplace_back(Node{ nodeIndex, -1, -1, node->elementsEnd - highCount, node->elementsEnd, box });
			node = nodes_.data() + nodeIndex;
		}

		if constexpr (KeyIsBox)
		{
			auto const middleCount = elementCount - lowCount - highCount;
			if (middleCount > 0 && middleCount <= maxElementsPerNode_)
			{
				node->elementsBegin += lowCount;
				node->elementsEnd -= highCount;
				//node->box = Bound(Span{ elements_.data() + node->elementsBegin, middleCount }, TTraits::GetSpatialKey);
			}
			else
			{
				//node->box = {};
				if (middleCount > 0)
				{
					node->boxData.middleChild = int(nodes_.size());
					//auto const& box = Bound(Span{ elements_.data() + node->elementsBegin + lowCount, middleCount }, TTraits::GetSpatialKey);
					auto box = node->box;
					ReduceBoxLeft(box, node->elementsBegin + lowCount, middleCount, splitAxis);
					ReduceBoxRight(box, node->elementsBegin + lowCount, middleCount, splitAxis);
					auto& middleNode = nodes_.emplace_back(Node{ nodeIndex, -1, -1, node->elementsBegin + lowCount, node->elementsEnd - highCount, box });
					middleNode.boxData.lockedAxesMask.set(splitAxis);
					node = nodes_.data() + nodeIndex;
				}

				node->elementsBegin = node->elementsEnd = -1;
			}
		}
		else
		{
			node->elementsBegin = node->elementsEnd = -1;
		}
	}

	template <typename TElement, class TTraits, class TElementAllocator>
	auto BoxTreeStatic<TElement, TTraits, TElementAllocator>::PartitionPoints(Node const& node, int splitAxis, ScalarType splitPosition) -> IndexType
	{
		using std::swap;

		auto currentLow = node.elementsBegin;
		auto currentHigh = node.elementsEnd - 1;

		for (;;)
		{
			for (; currentLow <= currentHigh; ++currentLow)
			{
				auto const& key = TTraits::GetSpatialKey(elements_[currentLow]);
				if (key[splitAxis] >= splitPosition)
				{
					break;
				}
			}

			for (; currentLow <= currentHigh; --currentHigh)
			{
				auto const& key = TTraits::GetSpatialKey(elements_[currentHigh]);
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

		return currentLow - node.elementsBegin;
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
				auto const& box = TTraits::GetSpatialKey(elements_[currentLow]);
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
				auto const& box = TTraits::GetSpatialKey(elements_[currentHigh]);
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

	template <typename TElement, class TTraits, class TElementAllocator>
	auto BoxTreeStatic<TElement, TTraits, TElementAllocator>::QueryNearest(VectorType const& targetLocation, int nearestCount, ScalarType maxDistance) const -> std::vector<std::pair<IndexType, ScalarType>>
	{
		ASSERT(nearestCount > 0 || maxDistance > 0);

		std::vector<std::pair<IndexType, ScalarType>> result;
		if (nearestCount > 0)
		{
			result.reserve(nearestCount);
		}

		auto worstDistance2 = maxDistance > 0 ? Square(maxDistance) : std::numeric_limits<ScalarType>::max();
		auto nodeIndex = !nodes_.empty() ? 0 : -1;
		auto down = true;

		while (nodeIndex >= 0)
		{
			auto const end = nodes_[nodeIndex].elementsEnd;
			for (auto elementIndex = nodes_[nodeIndex].elementsBegin; elementIndex < end; ++elementIndex)
			{
				auto const distance2 = GetDistanceSquared(targetLocation, TTraits::GetSpatialKey(elements_[elementIndex]));
				if (distance2 <= worstDistance2)
				{
					if (nearestCount > 0 && Size(result) == nearestCount)
					{
						result.erase(--result.end());
					}

					auto position = std::lower_bound(result.begin(), result.end(), distance2, [](auto const& pair, ScalarType v) { return pair.second < v; });
					result.emplace(position, elementIndex, distance2);
					if (nearestCount > 0 && Size(result) == nearestCount)
					{
						worstDistance2 = result.back().second;
					}
				}
			}

			for (;;)
			{
				if (down)
				{
					auto const child = GetFirstChildNear(nodeIndex, targetLocation, worstDistance2);
					if (child >= 0)
					{
						nodeIndex = child;
						break;
					}
				}

				auto const sibling = GetNextSiblingNear(nodeIndex, targetLocation, worstDistance2);
				if (sibling >= 0)
				{
					nodeIndex = sibling;
					down = true;
					break;
				}

				nodeIndex = nodes_[nodeIndex].parent;
				if (nodeIndex < 0)
				{
					break;
				}

				down = false;
			}
		}

		return result;
	}
}
