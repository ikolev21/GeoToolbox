#pragma once

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <execution>
#include <iterator>
#include <memory>
#include <numeric>
#include <string>
#include <unordered_set>
#include <vector>

namespace GeoToolbox
{
	// Traits

	// Usage:
	// template <typename T> using Has_SomeMethod = decltype( std::declval<T>().SomeMethod( 1, "asd" ) );
	// static_assert( HasMember<SomeType, Has_SomeMethod>, "SomeType must have SomeMethod( int, char* )" );
	template <typename T, template <typename> class TMemberTester, class = void>
	constexpr bool HasMember = false;

	template <typename T, template <typename> class TMemberTester>
	constexpr bool HasMember<T, TMemberTester, std::void_t<TMemberTester<T>>> = true;

	// Algorithm wrappers

	template <class TContainer, typename T>
	[[nodiscard]] constexpr auto Find(TContainer& container, T const& value)
	{
		using std::begin;
		using std::end;
		return std::find(begin(container), end(container), value);
	}

	template <class TContainer, typename T>
	[[nodiscard]] constexpr bool Contains(TContainer const& container, T const& value)
	{
		using std::end;
		return Find(container, value) != end(container);
	}

	template <class TContainer, class TPredicate>
	[[nodiscard]] constexpr bool AllOf(TContainer& container, TPredicate predicate)
	{
		// all_of is constexpr only in C++20
		//return std::all_of(container.begin(), container.end(), predicate);
		for (auto&& element : container)
		{
			if (!predicate(element))
			{
				return false;
			}
		}

		return true;
	}

	template <class TContainerA, class TContainerB, class TPredicate>
	[[nodiscard]] constexpr bool AllOf(TContainerA& containerA, TContainerB& containerB, TPredicate predicate)
	{
		using std::begin;
		using std::end;
		for (
			auto iteratorA = begin(containerA), iteratorB = begin(containerB);
			iteratorA != end(containerA) && iteratorB != end(containerB);
			++iteratorA, ++iteratorB)
		{
			if (!predicate(*iteratorA, *iteratorB))
			{
				return false;
			}
		}

		return true;
	}

	// C++20 std::ssize
	template <class TContainer>
	[[nodiscard]] std::ptrdiff_t Size(TContainer const& container)
	{
		return static_cast<std::ptrdiff_t>(std::size(container));
	}

	template<typename TContainer>
	typename TContainer::value_type Accumulate(TContainer const& container)
	{
		using std::begin;
		using std::end;
		return std::accumulate(begin(container), end(container), typename TContainer::value_type(0));
	}

	template <typename TContainer, typename TDestContainer, typename TFunctor>
	void Transform(TContainer const& container, TDestContainer& result, TFunctor functor, bool appendResult = false)
	{
		auto const previousResultSize = appendResult ? result.size() : 0;

		if constexpr (std::is_default_constructible_v<typename TDestContainer::value_type>)
		{
			result.resize(previousResultSize + container.size());
			std::transform(container.begin(), container.end(), result.begin() + previousResultSize, std::move(functor));
		}
		else
		{
			if constexpr (std::is_same_v<TContainer, TDestContainer>)
			{
				ASSERT(&container != &result && "Cannot transform a container of non-default-constructible elements into itself");
			}

			if (!appendResult)
			{
				result.clear();
			}

			result.reserve(previousResultSize + container.size());
			std::transform(container.begin(), container.end(), std::back_inserter(result), std::move(functor));
		}
	}

	template <typename TContainer, typename TFunctor>
	[[nodiscard]] auto Transform(TContainer&& container, TFunctor functor) -> std::vector<decltype(functor(*std::declval<TContainer>().begin()))>
	{
		using ResultElementType = decltype(functor(*std::declval<TContainer>().begin()));
		std::vector<ResultElementType> result;
		Transform(std::forward<TContainer>(container), result, std::move(functor));
		return result;
	}

	template <typename TContainer, typename TFunctor>
	void TransformInPlace(TContainer& container, TFunctor functor)
	{
		Transform(container, container, std::move(functor));
	}

	template <typename C>
	struct CaseInsensitiveCharTraits : std::char_traits<C>
	{
		static bool eq(C c1, C c2)
		{
			return std::toupper(c1) == std::toupper(c2);
		}
	};

	template <typename C, class TTraits>
	std::ptrdiff_t FindStringImpl(std::basic_string_view<C> target, std::basic_string_view<C> searchedItem)
	{
		if (target.empty() || searchedItem.empty())
		{
			return -1;
		}

		auto iter = std::search(target.begin(), target.end(), searchedItem.begin(), searchedItem.end(), TTraits::eq);
		return iter != target.end() ? iter - target.begin() : -1;
	}

	template <template <typename> class TTraits = std::char_traits>
	std::ptrdiff_t FindString(std::string_view target, std::string_view searchedItem)
	{
		return FindStringImpl<char, TTraits<char>>(target, searchedItem);
	}

	template <template <typename> class TTraits = std::char_traits>
	std::ptrdiff_t FindString(std::wstring_view target, std::wstring_view searchedItem)
	{
		return FindStringImpl<wchar_t, TTraits<wchar_t>>(target, searchedItem);
	}

	template <typename C, class TTraits>
	constexpr bool StartsWithImpl(std::basic_string_view<C> const& text, std::basic_string_view<C> const& prefix)
	{
		return text.size() >= prefix.size() && TTraits::compare(text.data(), prefix.data(), prefix.size()) == 0;
	}

	template <typename C, class TTraits>
	constexpr bool EndsWithImpl(std::basic_string_view<C> const& text, std::basic_string_view<C> const& suffix)
	{
		return text.size() >= suffix.size() && TTraits::compare(text.data() + (text.size() - suffix.size()), suffix.data(), suffix.size()) == 0;
	}

	template <template <typename> class TTraits = std::char_traits>
	constexpr bool StartsWith(std::string_view const& text, std::string_view const& prefix)
	{
		return StartsWithImpl<char, TTraits<char>>(text, prefix);
	}

	template <template <typename> class TTraits = std::char_traits>
	constexpr bool StartsWith(std::wstring_view const& text, std::wstring_view const& prefix)
	{
		return StartsWithImpl<wchar_t, TTraits<wchar_t>>(text, prefix);
	}

	template <template <typename> class TTraits = std::char_traits>
	constexpr bool EndsWith(std::string_view const& text, std::string_view const& suffix)
	{
		return EndsWithImpl<char, TTraits<char>>(text, suffix);
	}

	template <template <typename> class TTraits = std::char_traits>
	constexpr bool EndsWith(std::wstring_view const& text, std::wstring_view const& suffix)
	{
		return EndsWithImpl<wchar_t, TTraits<wchar_t>>(text, suffix);
	}


	template <class TContainer, typename T>
	[[nodiscard]] constexpr auto ParallelFind(TContainer const& container, T const& value)
	{
		using std::begin;
		using std::end;
		return std::find(std::execution::par, begin(container), end(container), value);
	}

	template <class TContainer, class TPredicate>
	[[nodiscard]] constexpr auto ParallelFindIf(TContainer const& container, TPredicate predicate)
	{
		using std::begin;
		using std::end;
		return std::find_if(std::execution::par, begin(container), end(container), predicate);
	}

	template <class TContainer, class TPredicate>
	[[nodiscard]] int CountIf(TContainer const& container, TPredicate predicate)
	{
		using std::begin;
		using std::end;
		return int(std::count_if(begin(container), end(container), predicate));
	}

	template <class TContainer, class TPredicate>
	[[nodiscard]] int ParallelCountIf(TContainer const& container, TPredicate predicate)
	{
		using std::begin;
		using std::end;
		return int(std::count_if(std::execution::par, begin(container), end(container), predicate));
	}

	// Type lists

	template <typename... T>
	struct TypeList
	{
	};

	namespace Detail
	{
		template <int Index, class... T>
		struct TypePackElementImpl;

		template <int Index>
		struct TypePackElementImpl<Index>
		{
			static_assert(Index == 0, "Index out of range");
		};

		template <class T0, class... TRest>
		struct TypePackElementImpl<0, T0, TRest...>
		{
			using type = T0;
		};

		template <int Index, class T0, class... TTail>
		struct TypePackElementImpl<Index, T0, TTail...> : TypePackElementImpl<Index - 1, TTail...>
		{
		};

		template <typename V, class L>
		struct TypeListContainsImpl
		{
			static_assert(sizeof(L) == 0, "The first argument to TypeListContains is not a list");
		};

		template <typename V, template <class...> class L>
		struct TypeListContainsImpl<V, L<>>
		{
			static constexpr auto value = false;
		};

		template <typename V, template <class...> class L, class THead, class... TRest>
		struct TypeListContainsImpl<V, L<THead, TRest...>>
		{
			static constexpr bool value = std::is_same_v<V, THead> || TypeListContainsImpl<V, L<TRest...>>::value;
		};
	}

	// An error here means Index is out of range
	template <int Index, class... T>
	using TypePackElement = typename Detail::TypePackElementImpl<Index, T...>::type;

	namespace Detail
	{
		template <class L>
		struct TypeListSizeImpl
		{
			static_assert(sizeof(L) == 0, "The argument to TypeListSize is not a list");
		};

		template <template <class...> class L, class... T>
		struct TypeListSizeImpl<L<T...>>
		{
			static constexpr auto value = sizeof...(T);
		};

		template <int Index, class L>
		struct TypeListAtImpl
		{
			static_assert(sizeof(L) == 0, "The argument to TypeListAt is not a list");
		};

		template <int Index, template <class...> class L, class... T>
		struct TypeListAtImpl<Index, L<T...>>
		{
			using type = TypePackElement<Index, T...>;
		};

		template <class L>
		struct TypeListForEachImpl
		{
		};

		template <template <typename...> class L, typename...T>
		struct TypeListForEachImpl<L<T...>>
		{
			template <typename F>
			static constexpr void DoIt(F function)
			{
				(function(T{}), ...);
			}
		};

		template <class L, typename... T>
		struct TypeListPushFrontImpl
		{
		};

		template <template <typename...> class L, typename... U, typename... T>
		struct TypeListPushFrontImpl<L<U...>, T...>
		{
			using type = L<T..., U...>;
		};


		template <template <class> class Predicate, typename...T>
		struct MakeFilteredTypeListImpl;

		template <template <class> class Predicate>
		struct MakeFilteredTypeListImpl<Predicate>
		{
			using type = TypeList<>;
		};

		template <template <class> class Predicate, typename THead, typename...T>
		struct MakeFilteredTypeListImpl<Predicate, THead, T...>
		{
			using Rest = typename MakeFilteredTypeListImpl<Predicate, T...>::type;
			using type = std::conditional_t<Predicate<THead>::value, typename TypeListPushFrontImpl<Rest, THead>::type, Rest>;
		};

		template <class L, template <class> class F>
		struct TypeListMapImpl
		{
			static_assert(sizeof(L) == 0, "The first argument to TypeListMap is not a list");
		};

		template <template <typename...> class L, typename... T, template <typename> class F>
		struct TypeListMapImpl<L<T...>, F>
		{
			using type = L<typename F<T>::type...>;
		};


		template <typename T, typename F, std::size_t... Indices>
		constexpr void TupleForEachImpl(T&& tuple, F function, std::index_sequence<Indices...>)
		{
			(function(std::get<Indices>(tuple)), ...);
		}
	}

	template <class L> constexpr auto TypeListSize = Detail::TypeListSizeImpl<L>::value;

	template <int Index, class L>
	using TypeListAt = typename Detail::TypeListAtImpl<Index, L>::type;

	template <class L, typename V>
	inline constexpr bool TypeListContains = Detail::TypeListContainsImpl<V, L>::value;

	template <template <class> class Predicate, typename...T>
	using MakeFilteredTypeList = typename Detail::MakeFilteredTypeListImpl<Predicate, T...>::type;

	template <class L, template <class> class F>
	using TypeListMap = typename Detail::TypeListMapImpl<L, F>::type;

	// F will be called with T() for each T in L, T must be default-constructible
	template <class L, typename F>
	constexpr void TypeListForEach(F function)
	{
		Detail::TypeListForEachImpl<L>::DoIt(function);
	}


	template <typename T, typename F>
	constexpr void TupleForEach(T&& tuple, F function)
	{
		Detail::TupleForEachImpl(std::forward<T>(tuple), function, std::make_index_sequence<std::tuple_size_v<std::decay_t<T>>>());
	}

	// Iterators

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

		template<typename T> OutputIteratorFunction& operator=(T const& value)
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

		template<typename T> CountingOutputIterator& operator=(T const&)
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

	// A primitive "range", a pair of iterators with begin()/end()
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
			: begin_(begin),
			end_(end)
		{
		}

		TIterator begin() const
		{
			return begin_;
		}

		TIterator end() const
		{
			return end_;
		}

		[[nodiscard]] int size() const
		{
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

	// Useful not just to iterate over an integer range (which is maybe still slower than a plain index loop),
	// but also to convert a contiguous range of objects into a range of pointers to them
	template <typename T>
	struct SelfIterator
	{
		using iterator_category = std::random_access_iterator_tag;
		using value_type = T;
		using difference_type = std::ptrdiff_t;
		using pointer = void;
		using reference = T;

		SelfIterator() = default;

		explicit SelfIterator(value_type value)
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

		[[nodiscard]] friend bool operator==(SelfIterator const& a, SelfIterator const& b)
		{
			return a.value_ == b.value_;
		}

		[[nodiscard]] friend bool operator!=(SelfIterator const& a, SelfIterator const& b)
		{
			return a.value_ != b.value_;
		}

		[[nodiscard]] friend difference_type operator-(SelfIterator const& a, SelfIterator const& b)
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

	template <typename T>
	Iterable<SelfIterator<T>> MakeRange(T first, T last)
	{
		return Iterable{ SelfIterator{ first }, SelfIterator{ last } };
	}

	// Allocations

	using SharedAllocatedSize = std::shared_ptr<std::atomic<std::int64_t>>;

	// Adapted from boost/container/new_allocator.hpp
	template <typename T, class TAllocator = std::allocator<T>>
	class ProfileAllocator : public TAllocator
	{
		SharedAllocatedSize stats_;

	public:

		using value_type = typename TAllocator::value_type;
		using pointer = typename std::allocator_traits<TAllocator>::pointer;
		using const_pointer = typename std::allocator_traits<TAllocator>::const_pointer;
		//using reference = typename TAllocator::reference;
		//using const_reference = typename TAllocator::const_reference;
		using size_type = std::size_t;
		using difference_type = std::ptrdiff_t;

		template <class T2>
		struct rebind
		{
			using TAllocator2 = typename std::allocator_traits<TAllocator>::template rebind_alloc<T2>;
			using other = ProfileAllocator<T2, TAllocator2>;
		};

		~ProfileAllocator() = default;

		ProfileAllocator() noexcept
			: stats_(std::make_shared<std::atomic<std::int64_t>>())
		{
		}

		explicit ProfileAllocator(SharedAllocatedSize stats) noexcept
			: stats_(std::move(stats))
		{
			if (stats_ == nullptr)
			{
				stats_ = std::make_shared<std::atomic<std::int64_t>>();
			}
		}

		ProfileAllocator(ProfileAllocator const& other) noexcept = default;

		ProfileAllocator(ProfileAllocator&& other) noexcept
			: TAllocator(std::move(other))
			, stats_(other.stats_)
		{
		}

		ProfileAllocator& operator=(ProfileAllocator const& other) noexcept = default;

		ProfileAllocator& operator=(ProfileAllocator&& other) noexcept = delete;

		template <class T2, class TAllocator2>
		/*explicit( false )*/ ProfileAllocator(ProfileAllocator<T2, TAllocator2> const& other) noexcept
			: TAllocator(other),
			stats_(other.state())
		{
		}

		pointer allocate(size_type count)
		{
			auto const max_count = std::size_t(-1) / (2 * sizeof(value_type));
			if (count > max_count)
			{
				throw std::bad_alloc();
			}

			auto const totalSize = count * sizeof(value_type);
			*stats_ += totalSize;
			return TAllocator::allocate(count);
		}

		void deallocate(pointer ptr, size_type count) noexcept
		{
			auto const totalSize = count * sizeof(value_type);
			*stats_ -= std::int64_t(totalSize);
			TAllocator::deallocate(ptr, count);
		}

		friend void swap(ProfileAllocator& a, ProfileAllocator& b) noexcept
		{
			std::swap(a.stats_, b.stats_);
		}

		friend bool operator==(ProfileAllocator const& a, ProfileAllocator const& b) noexcept
		{
			//return static_cast<TAllocator const&>( a ) == static_cast<TAllocator const&>( b );
			return a.stats_ == b.stats_;
		}

		friend bool operator!=(ProfileAllocator const& a, ProfileAllocator const& b) noexcept
		{
			return !(a == b);
		}


		[[nodiscard]] SharedAllocatedSize const& state() const
		{
			return stats_;
		}

		[[nodiscard]] size_type totalAllocated() const
		{
			return *stats_;
		}
	};


	class StringStorage
	{
		std::unordered_set<std::string> storage_;

	public:

		std::string_view GetOrAddString(std::string const& text)
		{
			auto const [location, _] = storage_.insert(text);
			return *location;
		}

		std::string_view GetOrAddString(std::string_view text)
		{
			return GetOrAddString(std::string(text));
		}
	};


	// Space-efficient discriminated union of T* and int64_t, supports just half the range of int64_t
	template <typename T>
	class PointerOrInt
	{
		static_assert(alignof(T) >= 2);

		std::int64_t storage_;

	public:

		PointerOrInt()
			: storage_{ 0 }
		{
		}

		/*explicit(false)*/ PointerOrInt(T* pointer)
			: storage_(std::int64_t(pointer))
		{
		}

		PointerOrInt(std::int64_t value)
		{
			*this = value;
		}

		[[nodiscard]] bool isInt() const
		{
			return (storage_ & 1) == 1;
		}

		[[nodiscard]] bool isPointer() const
		{
			return (storage_ & 1) == 0;
		}

		[[nodiscard]] std::int64_t getInt() const
		{
			DEBUG_ASSERT(isInt(), std::invalid_argument);
			return storage_ / 2;
		}

		[[nodiscard]] T* get() const
		{
			DEBUG_ASSERT(isPointer());
			return reinterpret_cast<T*>(storage_);
		}

		[[nodiscard]] T* operator->() const
		{
			DEBUG_ASSERT(*this != nullptr, std::invalid_argument);
			return get();
		}

		[[nodiscard]] T& operator*() const
		{
			DEBUG_ASSERT(*this != nullptr, std::invalid_argument);
			return *get();
		}

		PointerOrInt& operator=(T* pointer)
		{
			storage_ = std::int64_t(pointer);
			return *this;
		}

		PointerOrInt& operator=(std::int64_t value)
		{
			storage_ = value * 2 + 1;
			DEBUG_ASSERT(value >= 0 == storage_ >= 0, std::overflow_error);
			return *this;
		}

		[[nodiscard]] bool operator==(std::nullptr_t) const
		{
			DEBUG_ASSERT(isPointer());
			return storage_ == 0;
		}

		[[nodiscard]] bool operator!=(std::nullptr_t) const
		{
			DEBUG_ASSERT(isPointer());
			return !(*this == nullptr);
		}
	};


	// System

	inline std::string GetEnvironmentVariable(char const* name, std::string const& fallback = {})
	{
		char const* value = std::getenv(name);
		return value != nullptr ? std::string(value) : fallback;
	}

	inline int GetEnvironmentVariable(char const* name, int fallback)
	{
		auto const text = GetEnvironmentVariable(name);
		if (!text.empty())
		{
			std::from_chars(text.data(), text.data() + text.length(), fallback);
		}

		return fallback;
	}
}
