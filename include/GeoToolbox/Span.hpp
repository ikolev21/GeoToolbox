// Copyright 2024-2025 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include "Asserts.hpp"

#include <array>
#include <memory>

namespace GeoToolbox
{
	// A much simpler variant of C++20's std::span<T, std::dynamic_extent>
	template <typename T>
	struct Span
	{
		using element_type = T;
		using value_type = std::remove_cv_t<T>;
		using size_type = std::ptrdiff_t;
		using index_type = size_type;
		using pointer = T*;
		using reference = T&;

		using iterator = T*;
		using const_iterator = T*;

	private:

		T* data_ = nullptr;
		size_type count_ = 0;

	public:

		constexpr Span() = default;

		constexpr Span(T* data, size_type count) noexcept(IsReleaseBuild)
			: data_{ data }
			, count_{ count }
		{
			DEBUG_ASSERT(data == nullptr ? size() == 0 : size() >= 0);
		}

		constexpr Span(T* begin, T* end) noexcept(IsReleaseBuild)
			: data_{ begin }
			, count_(size_type(end - begin))
		{
			DEBUG_ASSERT(begin == nullptr && end == nullptr || begin != nullptr && end != nullptr);
			DEBUG_ASSERT(count_ >= 0);
		}

		template <std::size_t N>
		/*explicit(false)*/ constexpr Span(element_type(&array)[N]) noexcept
			: data_{ std::addressof(array[0]) }
			, count_{ size_type(N) }
		{
		}

		template <std::size_t N, class ArrayElementType = std::remove_const_t<element_type>>
		/*explicit(false)*/ constexpr Span(std::array<ArrayElementType, N>& array) noexcept
			: data_{ array.data() }
			, count_{ size_type(N) }
		{
		}

		template <
			class Container,
			class = std::enable_if_t<
			std::is_convertible_v<typename Container::pointer, pointer>
			&& std::is_convertible_v<typename Container::pointer, decltype(std::declval<Container>().data())>
			&& sizeof(*static_cast<typename Container::pointer>(nullptr)) == sizeof(*static_cast<pointer>(nullptr))
			>>
			/*explicit(false)*/ constexpr Span(Container& cont) noexcept
			: Span {
			cont.size() > 0 ? &cont[0] : nullptr, size_type(cont.size())
		}
		{
		}

		template <
			class Container,
			class = std::enable_if_t<
			std::is_convertible_v<typename Container::pointer, pointer>
			&& std::is_convertible_v<typename Container::pointer, decltype(std::declval<Container>().data())>
			&& sizeof(*static_cast<typename Container::pointer>(nullptr)) == sizeof(*static_cast<pointer>(nullptr))
			>>
			/*explicit(false)*/ constexpr Span(Container const& cont) noexcept
			: Span {
			cont.size() > 0 ? &cont[0] : nullptr, size_type(cont.size())
		}
		{
			static_assert(std::is_const_v<element_type>, "Span element type must be const");
		}

		[[nodiscard]] constexpr size_type size() const noexcept
		{
			return count_;
		}

		[[nodiscard]] constexpr size_type size_bytes() const noexcept
		{
			return size() * sizeof(element_type);
		}

		[[nodiscard]] constexpr bool empty() const noexcept
		{
			return count_ == 0;
		}

		[[nodiscard]] constexpr iterator begin() const noexcept
		{
			return data_;
		}

		[[nodiscard]] constexpr iterator end() const noexcept
		{
			return data_ + count_;
		}

		[[nodiscard]] constexpr std::reverse_iterator<T const*> rbegin() const noexcept
		{
			return std::reverse_iterator<T const*>{end()};
		}

		[[nodiscard]] constexpr std::reverse_iterator<T const*> rend() const noexcept
		{
			return std::reverse_iterator<T const*>{begin()};
		}

		[[nodiscard]] constexpr pointer data() const noexcept
		{
			return data_;
		}

		constexpr reference operator[](size_type index) const
		{
			DEBUG_ASSERT(0 <= index);
			DEBUG_ASSERT(index < size());
			return data_[index];
		}

		[[nodiscard]] constexpr reference at(size_type index) const
		{
			if (index < 0 || index >= size())
			{
				throw std::out_of_range("Span::at() index out of range");
			}

			return data_[index];
		}

		[[nodiscard]] constexpr reference front() const
		{
			DEBUG_ASSERT(count_ > 0);
			return data_[0];
		}

		[[nodiscard]] constexpr reference back() const
		{
			DEBUG_ASSERT(count_ > 0);
			return data_[count_ - 1];
		}

		[[nodiscard]] constexpr Span first(size_type count) const
		{
			DEBUG_ASSERT(count >= 0);
			DEBUG_ASSERT(count <= size());
			return { data_, data_ + count };
		}

		[[nodiscard]] constexpr Span last(size_type count) const
		{
			DEBUG_ASSERT(count >= 0);
			DEBUG_ASSERT(count <= size());
			return { data_ + size() - count, end() };
		}

		[[nodiscard]] constexpr Span subspan(size_type offset, size_type count = -1) const
		{
			DEBUG_ASSERT(offset >= 0);
			DEBUG_ASSERT(offset <= size());

			if (count < 0)
			{
				return { data() + offset, size() - offset };
			}

			DEBUG_ASSERT(size() - offset >= count);
			return { data() + offset, count };
		}
	};

	//template <typename T, class C>
	//bool operator==( Span<T> const& a, C const& b )
	//{
	//	return a.size() == int( b.size() )
	//		&& ( a.empty() || std::equal( a.begin(), a.end(), b.begin() ) );
	//}
}
