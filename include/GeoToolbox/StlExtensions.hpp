// Copyright 2024-2025 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <execution>
#include <iterator>
#include <memory>
#include <memory_resource>
#include <numeric>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace GeoToolbox
{
	// Traits

	struct Identity
	{
		template <class T>
		constexpr T&& operator()(T&& t) const noexcept
		{
			return std::forward<T>(t);
		}
	};

	// Usage:
	// using VectorType = std::vector<int>;
	// static_assert( IsSpecialization<VectorType, std::vector> );
	// static_assert( !IsSpecialization<int, std::vector> );
	template <typename TTest, template <typename...> class Ref>
	inline constexpr auto IsSpecialization = false;

	template <template <typename...> class Ref, typename... Args>
	inline constexpr auto IsSpecialization<Ref<Args...>, Ref> = true;

	// Usage:
	// template <typename T> using Has_SomeMethod = decltype( std::declval<T>().SomeMethod( 1, "asd" ) );
	// static_assert( HasMember<SomeType, Has_SomeMethod>, "SomeType must have SomeMethod( int, char* )" );
	template <typename T, template <typename> class TMemberTester, class = void>
	constexpr auto HasMember = false;

	template <typename T, template <typename> class TMemberTester>
	constexpr auto HasMember<T, TMemberTester, std::void_t<TMemberTester<T>>> = true;


	// Algorithm wrappers

	template <typename T>
	T Square(T x)
	{
		return x * x;
	}

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

	template <class TContainer, class TPredicate>
	[[nodiscard]] constexpr bool AnyOf(TContainer& container, TPredicate predicate)
	{
		for (auto&& element : container)
		{
			if (predicate(element))
			{
				return true;
			}
		}

		return false;
	}

	// C++20 std::ssize
	template <class TContainer>
	[[nodiscard]] std::ptrdiff_t Size(TContainer const& container) noexcept
	{
		return static_cast<std::ptrdiff_t>(std::size(container));
	}

	template <typename TContainer>
	typename TContainer::value_type Accumulate(TContainer const& container)
	{
		using std::begin;
		using std::end;
		return std::accumulate(begin(container), end(container), typename TContainer::value_type(0));
	}

	template <typename TContainer, typename T, class TBinaryOp>
	auto Accumulate(TContainer const& container, T init, TBinaryOp op)
	{
		using std::begin;
		using std::end;
		return std::accumulate(begin(container), end(container), init, op);
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

	template <typename TContainer>
	[[nodiscard]] auto ToVector(TContainer&& container)
	{
		using ResultElementType = std::decay_t<decltype(*container.begin())>;
		std::vector<ResultElementType> result;
		std::copy(container.begin(), container.end(), std::back_inserter(result));
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

	template <typename ... T, typename F>
	constexpr void ForEachType(F function)
	{
		(function(T{}), ...);
	}


	template <typename T, typename F>
	constexpr void TupleForEach(T&& tuple, F function)
	{
		Detail::TupleForEachImpl(std::forward<T>(tuple), function, std::make_index_sequence<std::tuple_size_v<std::decay_t<T>>>());
	}

	template <typename T, typename F, std::size_t... Indices>
	constexpr int TupleForIndexImpl(T&& tuple, int index, F&& function, std::index_sequence<Indices...>)
	{
		return ((Indices == index ? (function(std::get<Indices>(std::forward<T>(tuple))), 1) : 0) + ...);
	}

	template <typename T, typename F>
	constexpr bool TupleForIndex(T&& tuple, int index, F&& function)
	{
		return TupleForIndexImpl(std::forward<T>(tuple), index, std::forward<F>(function), std::make_index_sequence<std::tuple_size_v<std::decay_t<T>>>()) == 1;
	}


	// Allocations

	template <class TElement, class TPoolResource = std::pmr::synchronized_pool_resource, std::size_t NMaxBlockSize = sizeof(TElement), std::size_t NMaxBlocksPerChunk = 0>
	class PoolAllocator : public std::pmr::polymorphic_allocator<TElement>
	{
		using BaseType = std::pmr::polymorphic_allocator<TElement>;

		std::shared_ptr<TPoolResource> resource_;

		template <class T2, class TPoolResource2, std::size_t, std::size_t>
		friend class PoolAllocator;

	public:

		template <class T2>
		struct rebind
		{
			using other = PoolAllocator<T2, TPoolResource>;
		};

		~PoolAllocator() = default;
		PoolAllocator(PoolAllocator const&) = default;
		PoolAllocator& operator=(PoolAllocator const&) = delete;
		PoolAllocator& operator=(PoolAllocator&&) = delete;

		explicit PoolAllocator(std::size_t maxBlockSize = NMaxBlockSize, std::size_t maxBlocksPerChunk = NMaxBlocksPerChunk)
			: PoolAllocator{ nullptr, maxBlockSize, maxBlocksPerChunk }
		{
		}

		explicit PoolAllocator(std::pmr::memory_resource* upstream, std::size_t maxBlockSize = NMaxBlockSize, std::size_t maxBlocksPerChunk = NMaxBlocksPerChunk)
			: BaseType{ new TPoolResource(std::pmr::pool_options{ maxBlocksPerChunk, maxBlockSize }, upstream != nullptr ? upstream : std::pmr::get_default_resource()) }
			, resource_{ static_cast<TPoolResource*>( BaseType::resource() ) }
		{
		}

		template <class T2, std::size_t NMaxBlockSizeOther, std::size_t NMaxBlocksPerChunkOther>
		explicit PoolAllocator( PoolAllocator<T2, TPoolResource, NMaxBlockSizeOther, NMaxBlocksPerChunkOther> const& other )
			: BaseType{ other.resource() }
			, resource_{ other.resource_ }
		{
		}
	};

	template <typename TKey>
	using ListPoolAllocatorSync = GeoToolbox::PoolAllocator<TKey, std::pmr::synchronized_pool_resource, sizeof(TKey) + 2 * sizeof(void*)>;


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

		[[nodiscard]] bool IsInt() const
		{
			return (storage_ & 1) == 1;
		}

		[[nodiscard]] bool IsPointer() const
		{
			return (storage_ & 1) == 0;
		}

		[[nodiscard]] std::int64_t GetInt() const
		{
			DEBUG_ASSERT(IsInt(), std::invalid_argument);
			return storage_ / 2;
		}

		[[nodiscard]] T* get() const
		{
			DEBUG_ASSERT(IsPointer());
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
			DEBUG_ASSERT(IsPointer());
			return storage_ == 0;
		}

		[[nodiscard]] bool operator!=(std::nullptr_t) const
		{
			DEBUG_ASSERT(IsPointer());
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
