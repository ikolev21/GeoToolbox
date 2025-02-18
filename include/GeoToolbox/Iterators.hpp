// Copyright 2024-2025 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include "GeoToolbox/Asserts.hpp"
#include "GeoToolbox/StlExtensions.hpp"

#include <array>
#include <iterator>
#include <utility>
#include <variant>
#include <vector>

namespace GeoToolbox
{
	template <class F>
	class OutputIteratorFunction
	{
		F action_;

	public:

		using iterator_category = std::output_iterator_tag;
		using value_type = void;
		using difference_type = void;
		using pointer = void;
		using reference = void;

		explicit OutputIteratorFunction(F action)
			: action_(std::move(action))
		{
		}

		template <typename T>
		OutputIteratorFunction& operator=(T const& value)
		{
			action_(value);
			return *this;
		}

		OutputIteratorFunction& operator*()
		{
			return *this;
		}

		OutputIteratorFunction& operator++()
		{
			return *this;
		}
	};

	class CountingOutputIterator
	{
		int* count_;

	public:
		using iterator_category = std::output_iterator_tag;
		using value_type = void;
		using difference_type = void;
		using pointer = void;
		using reference = void;

		explicit CountingOutputIterator(int& count) noexcept
			: count_(&count)
		{
		}

		template <typename T>
		CountingOutputIterator& operator=(T const&)
		{
			return *this;
		}

		CountingOutputIterator& operator*() noexcept
		{
			return *this;
		}

		CountingOutputIterator& operator++() noexcept
		{
			++*count_;
			return *this;
		}
	};

	// Useful not just to iterate over an integer range (which is maybe still slower than a plain 'for i' loop),
	// but also to convert a contiguous range of objects into a range of pointers to them
	template <typename T>
	struct ValueIterator
	{
		using iterator_category = std::random_access_iterator_tag;
		using value_type = T;
		using difference_type = std::ptrdiff_t;
		using pointer = void;
		using reference = T;

		ValueIterator() = default;

		explicit ValueIterator(value_type value)
			: value_(value)
		{
		}

		void operator++()
		{
			++value_;
		}

		void operator++(int)
		{
			++value_;
		}

		void operator--()
		{
			--value_;
		}

		[[nodiscard]] friend bool operator==(ValueIterator const& a, ValueIterator const& b)
		{
			return a.value_ == b.value_;
		}

		[[nodiscard]] friend bool operator!=(ValueIterator const& a, ValueIterator const& b)
		{
			return a.value_ != b.value_;
		}

		[[nodiscard]] friend difference_type operator-(ValueIterator const& a, ValueIterator const& b)
		{
			return a.value_ - b.value_;
		}

		reference operator*() const
		{
			return value_;
		}

	private:
		value_type value_{};
	};


	// Iterable

	// A primitive "range", a pair of begin()/end() iterators
	template <class TIterator>
	class Iterable
	{
		TIterator begin_, end_;

	public:

		using value_type = typename std::iterator_traits<TIterator>::value_type;
		using iterator = TIterator;
		using const_iterator = TIterator;

		Iterable()
			: end_(begin_)
		{
		}

		Iterable(TIterator begin, TIterator end)
			: begin_{ std::move( begin ) }
			, end_{ end }
		{
		}

		[[nodiscard]] TIterator begin() const noexcept(std::is_nothrow_assignable_v<TIterator, TIterator>)
		{
			return begin_;
		}

		[[nodiscard]] TIterator end() const noexcept(std::is_nothrow_assignable_v<TIterator, TIterator>)
		{
			return end_;
		}

		[[nodiscard]] int size() const
		{
			static_assert(std::is_same_v<typename std::iterator_traits<TIterator>::iterator_category, std::random_access_iterator_tag>);

			return int(std::distance(begin_, end_));
		}
	};

	template <class TIterator>
	Iterable<TIterator> MakeIterable(TIterator begin, int count)
	{
		return Iterable<TIterator>(begin, begin + count);
	}

	template <class TIterator>
	Iterable<std::reverse_iterator<TIterator>> ReverseIterable(TIterator begin, TIterator end)
	{
		return Iterable<std::reverse_iterator<TIterator>>(std::reverse_iterator<TIterator>(end), std::reverse_iterator<TIterator>(begin));
	}

	template <class TContainer>
	auto ReverseIterable(TContainer&& container)
	{
		return ReverseIterable(std::forward<TContainer>(container).begin(), std::forward<TContainer>(container).end());
	}

	template <typename T>
	Iterable<ValueIterator<T>> MakeRange(T first, T last)
	{
		return Iterable{ ValueIterator{ first }, ValueIterator{ last } };
	}

	template <class T>
	auto MakePointersRange(T& container)
	{
		return Iterable{ ValueIterator{ std::data(container) }, ValueIterator{ std::data(container) + std::size(container) } };
	}

	template <class TValue>
	auto MakePointersVector(TValue* begin, TValue* end)
	{
		return std::vector(ValueIterator{ begin }, ValueIterator{ end });
	}

	template <class TValue, class TAllocator>
	auto MakePointersVector(TValue* begin, TValue* end, TAllocator const& allocator)
	{
		return std::vector(ValueIterator{ begin }, ValueIterator{ end }, allocator);
	}

	template <class TContainer>
	auto MakePointersVector(TContainer& container)
	{
		return std::vector(ValueIterator{ std::data(container) }, ValueIterator{ std::data(container) + std::size(container) });
	}

	template <class TContainer, class TAllocator>
	auto MakePointersVector(TContainer& container, TAllocator const& allocator)
	{
		using T = decltype(std::data(container));
		static_assert(std::is_same_v<typename TAllocator::value_type, T>);
		return std::vector(ValueIterator{ std::data(container) }, ValueIterator{ std::data(container) + std::size(container) }, allocator);
	}


	// Concatenate Iterable's

	template <class... TIterator>
	struct Iterables
	{
		using Types = TypeList<TIterator...>;

		static constexpr auto Count = int(sizeof...(TIterator));

		using value_type = typename TypePackElement<0, TIterator...>::value_type;
		using pointer = typename TypePackElement<0, TIterator...>::pointer;
		using reference = typename TypePackElement<0, TIterator...>::reference;

		struct iterator;
		using const_iterator = iterator;

		static_assert((std::is_same_v<reference, typename std::iterator_traits<TIterator>::reference> && ...));


		std::tuple<Iterable<TIterator>...> iterables;


		explicit Iterables(Iterable<TIterator>... i)
			: iterables{ std::move(i)... }
		{
		}

		[[nodiscard]] iterator begin() const;

		[[nodiscard]] iterator end() const;
	};

	template <class... TIterator>
	struct Iterables<TIterator...>::iterator
	{
		using iterator_category = std::forward_iterator_tag;// typename TypePackElement<0, TIterator...>::iterator_category;
		using value_type = Iterables<TIterator...>::value_type;
		using difference_type = std::ptrdiff_t;
		using pointer = Iterables<TIterator...>::pointer;
		using reference = Iterables<TIterator...>::reference;

		using StorageType = std::variant<TIterator...>;

		Iterables const* owner;
		StorageType current;
		StorageType end;

		explicit iterator(Iterables const* owner /*end*/)
			: owner{ owner }
			, current{ std::in_place_index<Count - 1>, std::get<Count - 1>(owner->iterables).end() }
			, end{ std::in_place_index<Count - 1>, std::get<Count - 1>(owner->iterables).end() }
		{
		}

		iterator(Iterables const* owner, bool /*begin*/)
			: owner{ owner }
			, current{ std::in_place_index<0>, std::get<0>(owner->iterables).begin() }
			, end{ std::in_place_index<0>, std::get<0>(owner->iterables).end() }
		{
		}

		reference operator*() const
		{
			return std::visit([](auto& iter) -> decltype(auto) { return *iter; }, current);
		}

		iterator& operator++()
		{
			std::visit([](auto& iter) { ++iter; }, current);
			MoveToNextValid();
			return *this;
		}

		friend bool operator==(iterator const& left, iterator const& right)
		{
			return left.current == right.current;
		}

		friend bool operator!=(iterator const& left, iterator const& right)
		{
			return !(left == right);
		}

	private:

		template <std::size_t Index>
		void Init()
		{
			current = StorageType{ std::in_place_index<Index>, std::get<Index>(owner->iterables).begin() };
			end = StorageType{ std::in_place_index<Index>, std::get<Index>(owner->iterables).end() };
		}

		template <std::size_t... Indices>
		void Init(int index, std::index_sequence<Indices...>)
		{
			[[maybe_unused]] auto const _ = { (Indices == index ? (Init<Indices>(), 1) : 0)... };
		}

		void MoveToNextValid()
		{
			while (current == end)
			{
				auto i = int(current.index());
				++i;
				if (i >= Count)
				{
					return;
				}

				Init(i, std::make_index_sequence<Count>());
			}
		}
	};

	template <class... TIterator>
	auto Iterables<TIterator...>::begin() const -> iterator
	{
		return iterator(this, true);
	}

	template <class... TIterator>
	auto Iterables<TIterator...>::end() const -> iterator
	{
		return iterator(this);
	}

	template <class... TContainer>
	auto Concat(TContainer&... c)
	{
		constexpr bool anyConst = (std::is_const_v<std::remove_reference_t<decltype(*std::declval<TContainer>().begin())>> || ...);
		if constexpr (anyConst)
		{
			return Iterables{ Iterable{ std::as_const(c).begin(), std::as_const(c).end() }... };
		}
		else
		{
			return Iterables{ Iterable{ c.begin(), c.end() }... };
		}
	}

	template <class... TIterator>
	auto Concat(Iterable<TIterator>... iterable)
	{
		return Iterables{ iterable... };
	}


	// Generators

	namespace Generators
	{
		constexpr auto Stage_Canceled = -2;
		constexpr auto Stage_Done = -1;
		constexpr auto Stage_Start = 0;

		constexpr bool IsFinished(int stage) noexcept
		{
			return stage < 0;
		}

		class StateBase
		{
			int stage_;

		public:

			explicit StateBase(int s = Stage_Start)
				: stage_{ s }
			{
			}

			[[nodiscard]] int CurrentStage() const noexcept
			{
				//DEBUG_ASSERT( int(stage_) >= 0 );
				return int(stage_);
			}

			int Next() noexcept(IsReleaseBuild)
			{
				DEBUG_ASSERT(int(stage_) >= 0);
				return stage_ = stage_ + 1;
			}

			int Cancel() noexcept
			{
				return stage_ = Stage_Canceled;
			}

			int Finish() noexcept
			{
				return stage_ = Stage_Done;
			}
		};

		template <typename T>
		struct State : StateBase
		{
			std::optional<T> value{};


			using value_type = T;

			State() = default;

			State(T newValue, int stage = 0)
				: StateBase{ stage }
				, value{ std::move(newValue) }
			{
			}

			//State(State const&) = delete;
			//State(State&&) = default;
			//State& operator=(State const&) = delete;
			//State& operator=(State&&) = default;

			[[nodiscard]] bool HasValue() const noexcept
			{
				return value.has_value();
			}

			T& operator*()
			{
				return *value;
			}

			T* operator->()
			{
				return value.operator->();
			}

			using StateBase::Next;

			int Initialize(T newValue)
			{
				value = std::move(newValue);
				DEBUG_ASSERT(!IsFinished(CurrentStage()));
				return CurrentStage();
			}

			int Next(T newValue)
			{
				value = std::move(newValue);
				DEBUG_ASSERT(!IsFinished(CurrentStage()));
				return Next();
			}

			int Cancel() noexcept
			{
				value = {};
				return StateBase::Cancel();
			}

			int Finish() noexcept
			{
				value = {};
				return StateBase::Finish();
			}
		};

		template <typename TState>
		using FunctionType = int(*)(TState&);

		template <typename TState>
		struct Iterator
		{
			static_assert(std::is_base_of_v<StateBase, TState>);

			using iterator_category = std::output_iterator_tag;
			using value_type = typename TState::value_type;
			using difference_type = std::ptrdiff_t;
			using pointer = value_type*;
			using reference = value_type&;

		private:

			FunctionType<TState> func_{};

			TState* state_ = nullptr;

			int stage_ = Stage_Done;

		public:

			Iterator() = default;

			Iterator(FunctionType<TState> func, TState& state)
				: func_{ std::move(func) }
				, state_{ &state }
				, stage_{ Stage_Start }
			{
				if (!state_->HasValue())
				{
					++*this;
				}
			}

			reference operator*() const
			{
				DEBUG_ASSERT(!IsFinished(stage_));
				return **state_;
			}

			Iterator& operator++()
			{
				if (IsFinished(stage_))
				{
					return *this;
				}

				do
				{
					stage_ = func_(*state_);
				} while (!IsFinished(stage_) && !state_->HasValue());

				return *this;
			}

			friend bool operator==(Iterator const& left, Iterator const& right) noexcept
			{
				return IsFinished(left.stage_) ? IsFinished(right.stage_) : left.stage_ == right.stage_;
			}

			friend bool operator!=(Iterator const& left, Iterator const& right) noexcept
			{
				return !(left == right);
			}
		};

		template <typename TState>
		struct Generator
		{
			static_assert(std::is_base_of_v<StateBase, TState>);

			using value_type = typename TState::value_type;
			using iterator = Iterator<TState>;
			using const_iterator = Iterator<TState>;

			Generator()
				: run{ Run }
			{
			}

			explicit Generator(TState initialState)
				: run{ Run }
				, state(std::move(initialState))
			{
			}

			explicit Generator(FunctionType<TState> run)
				: run{ run }
			{
			}

			Generator(FunctionType<TState> run, TState initialState)
				: run{ run }
				, state(std::move(initialState))
			{
			}

			[[nodiscard]] iterator begin()
			{
				return { run, state };
			}

			[[nodiscard]] iterator end() noexcept
			{
				return {};
			}

			static int Run(TState& state)
			{
				return state.Run();
			}


			FunctionType<TState> run;

			TState state{};
		};

		template <typename TState>
		auto MakeGenerator(FunctionType<TState> run)
		{
			return Generator<TState>{ run };
		}

		template <typename TState, typename... TArg>
		auto MakeGenerator(FunctionType<TState> run, TArg... args)
		{
			return Generator<TState>{ run, TState{ args... } };
		}

		template <class TState>
		auto MakeGenerator(TState state)
		{
			return Generator<TState>{ std::move(state) };
		}
	}


	// USage: for( auto const element : SplitIterator{ text, ',' } ) /* use element */
	// for ( SplitIterator iterator{ text, ',' }; !iterator.done(); ++iterator ) /* use *iterator */
	// auto const elements = SplitIterator{ text, ',' }.toArray();
	template <typename C>
	struct TSplitIterator
	{
		using iterator_category = std::forward_iterator_tag;
		using value_type = std::basic_string_view<C>;
		using pointer = std::basic_string_view<C> const*;
		using reference = std::basic_string_view<C> const&;
		using difference_type = std::ptrdiff_t;
		using const_iterator = TSplitIterator;
		using iterator = TSplitIterator;

		static constexpr C DefaultDelimiter = C(' ');

		TSplitIterator()
			: curPos_{ Delimiters.data() + 1 }
			, nextPos_{ Delimiters.data() + 1 }
			, end_{ Delimiters.data() + 1 }
			, delimiter_{ DefaultDelimiter }
		{
		}

		explicit TSplitIterator(std::basic_string_view<C> str, C delimiter = DefaultDelimiter)
			: curPos_{ str.data() }
			, nextPos_{ str.data() }
			, end_{ curPos_ + str.size() }
			, delimiter_{ delimiter }
		{
			findNextDelimiter();
		}

		TSplitIterator& operator++()
		{
			if (nextPos_ != end_)
			{
				curPos_ = nextPos_ + 1;
				findNextDelimiter();
			}
			else
			{
				curPos_ = end_;
			}

			return *this;
		}

		std::basic_string_view<C> operator*() const
		{
			return std::basic_string_view<C>(curPos_, nextPos_ - curPos_);
		}

		[[nodiscard]] std::basic_string_view<C> remaining() const
		{
			return std::basic_string_view<C>(curPos_, end_ - curPos_);
		}

		bool operator==(TSplitIterator const& other) const
		{
			return curPos_ == end_ ? other.curPos_ == other.end_ : curPos_ == other.curPos_;
		}

		bool operator!=(TSplitIterator const& other) const
		{
			return !(*this == other);
		}

		[[nodiscard]] bool done() const
		{
			return curPos_ == end_;
		}

		[[nodiscard]] TSplitIterator begin() const
		{
			return *this;
		}

		[[nodiscard]] TSplitIterator end() const
		{
			auto result = *this;
			result.curPos_ = result.nextPos_ = end_;
			return result;
		}

		[[nodiscard]] std::vector<std::basic_string<C>> toArray() const
		{
			std::vector<std::basic_string<C>> result;
			for (TSplitIterator iter(*this); !iter.done(); ++iter)
			{
				result.emplace_back(*iter);
			}

			return result;
		}

	private:

		void findNextDelimiter()
		{
			nextPos_ = std::char_traits<C>::find(curPos_, end_ - curPos_, delimiter_);
			if (nextPos_ == nullptr)
			{
				nextPos_ = end_;
			}
		}

		static inline std::array<C, 2> const Delimiters = { C(' '), 0 };

		C const* curPos_;

		C const* nextPos_;

		C const* end_;

		C delimiter_;
	};

	using SplitIterator = TSplitIterator<char>;
	using WSplitIterator = TSplitIterator<wchar_t>;
}
