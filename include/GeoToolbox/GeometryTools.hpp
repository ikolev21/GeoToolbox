// Copyright 2024-2026 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include "Asserts.hpp"
#include "StlExtensions.hpp"

#if defined( ENABLE_EIGEN )
#	include <Eigen/Dense>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>

namespace GeoToolbox
{
	constexpr auto Pi = 3.141592653589793;

	// Vector interface

	// In addition to the operations in VectorTraits, all of which have default implementations in VectorTraitsDefault, the following operators must be implemented:
	// vector + vector, vector - vector, vector * scalar, scalar * vector, vector / scalar, scalar / vector (with common implementations also available in VectorTraitsDefault)
	template <class T>
	struct VectorTraits
	{
		static constexpr size_t Dimensions = 0;

		using ScalarType = std::int8_t;

		//static constexpr std::string_view Name = "...";

		//static constexpr auto IsConstexpr = true / false;

		//using ArrayType = std::array<ScalarType, Dimensions>;

		//using VectorType = T;

		//template <typename TScalar2, size_t NDimensions2>
		//using Reconfigure = T<TScalar2, NDimensions2>;


		// The rest are the static methods of VectorTraitsDefault
	};

	template <typename T> using HasSubscriptOperator = decltype( std::declval<T>()[1] );

	template <class T>
	constexpr bool IsVector =
		VectorTraits<T>::Dimensions > 0
		&& sizeof( T ) == VectorTraits<T>::Dimensions * sizeof( typename VectorTraits<T>::ScalarType )
		&& HasMember<T, HasSubscriptOperator>;

	// Default traits implementation

	namespace Detail
	{
		template <class TVector, std::size_t... Indices>
		constexpr void FillImpl(TVector& vector, typename VectorTraits<TVector>::ScalarType value, std::index_sequence<Indices...>)
		{
			((vector[Indices] = value), ...);
		}

		template <class TVector, std::size_t... Indices>
		constexpr TVector FlatImpl(typename VectorTraits<TVector>::ScalarType value, std::index_sequence<Indices...>)
		{
			return TVector{ ( (void)Indices, value )... };
		}

		template <class TResult, class TVector, class TUnaryOp, std::size_t... Indices>
		constexpr TResult ComponentApplyImpl(TVector const& a, TUnaryOp op, std::index_sequence<Indices...>)
		{
			using std::get;
			return TResult{ op(VectorTraits<TVector>::template Get<Indices>(a)) ... };
		}

		template <class TVector, class TBinaryOp, std::size_t... Indices>
		constexpr TVector ComponentApplyImpl(TVector const& a, TVector const& b, TBinaryOp binaryOp, std::index_sequence<Indices...>)
		{
			using Traits = VectorTraits<TVector>;
			return TVector{ binaryOp(Traits::template Get<Indices>(a), Traits::template Get<Indices>(b)) ... };
		}
	}

	template <class TVector, typename TScalar, size_t NDimensions>
	struct VectorTraitsDefault
	{
		static constexpr auto Dimensions = std::is_arithmetic_v<TScalar> ? NDimensions : 0;

		using ScalarType = TScalar;

		using ArrayType = std::array<ScalarType, Dimensions>;

		using VectorType = TVector;


		template <size_t I>
		static constexpr ScalarType Get(VectorType const& v)
		{
			return v[I];
		}

		static constexpr TVector FromArray(ArrayType const& v)
		{
			return v;
		}

		static constexpr ArrayType ToArray(TVector const& v)
		{
			return v;
		}

		static constexpr void Fill(TVector& vector, ScalarType value)
		{
			Detail::FillImpl(vector, value, std::make_index_sequence<VectorTraits<TVector>::Dimensions>());
		}

		static constexpr TVector Zero()
		{
			return Detail::FlatImpl<TVector>(0, std::make_index_sequence<VectorTraits<TVector>::Dimensions>());
		}

		static constexpr TVector Flat(ScalarType value)
		{
			return Detail::FlatImpl<TVector>(value, std::make_index_sequence<VectorTraits<TVector>::Dimensions>());
		}

		static constexpr TVector OperatorAdd(TVector const& a, TVector const& b)
		{
			/*
				constexpr auto N = VectorTraits<TVector>::Dimensions;
				TVector result;
				for (auto i = 0; i < N; ++i)	// Should be auto-vectorized by the compiler
				{
					result[i] = a[i] + b[i];
				}

				return result;
			*/
			return ComponentApply(a, b, [](auto x, auto y) { return x + y; });
		}

		static constexpr TVector OperatorSub(TVector const& a, TVector const& b)
		{
			return ComponentApply(a, b, [](auto x, auto y) { return x - y; });
		}

		static constexpr TVector OperatorMultiply(TVector const& a, TVector const& b)
		{
			return ComponentApply(a, b, [](auto x, auto y) { return x * y; });
		}

		static constexpr TVector OperatorDivide(TVector const& a, TVector const& b)
		{
			return ComponentApply(a, b, [](auto x, auto y) { return x / y; });
		}

		static constexpr TVector OperatorMultiplyByScalar(TVector const& v, ScalarType s)
		{
			/*
				constexpr auto N = VectorTraits<TVector>::Dimensions;
				TVector result;
				for (auto i = 0; i < N; ++i)	// Should be auto-vectorized by the compiler
				{
					result[i] = v[i] * s;
				}

				return result;
			*/
			return ComponentApply(v, [s](auto x) { return x * s; });
		}

		static constexpr TVector OperatorDivideByScalar(TVector const& v, ScalarType s)
		{
			return ComponentApply(v, [s](auto x) { return x / s; });
		}

		static constexpr TVector Min(TVector const& a, TVector const& b)
		{
			return ComponentApply(a, b, [](auto x, auto y) { return std::min(x, y); });
		}

		static constexpr TVector Max(TVector const& a, TVector const& b)
		{
			return ComponentApply(a, b, [](auto x, auto y) { return std::max(x, y); });
		}

		static constexpr auto MinimumValue(TVector const& a)
		{
			auto const position = std::min_element(&a[0], &a[0] + VectorTraits<TVector>::Dimensions);
			return std::pair{ *position, position - &a[0] };
		}

		static constexpr auto MaximumValue(TVector const& a)
		{
			auto const position = std::max_element(&a[0], &a[0] + VectorTraits<TVector>::Dimensions);
			return std::pair{ *position, position - &a[0] };
		}

		static constexpr auto Sum(TVector const& a)
		{
			return std::accumulate(&a[0], &a[0] + VectorTraits<TVector>::Dimensions, ScalarType(0));
		}

		static constexpr auto DotProduct(TVector const& a, TVector const& b)
		{
			auto const ab = ComponentApply(a, b, [](auto x, auto y) { return x * y; });
			return Accumulate(ab);
		}

		static constexpr auto LengthSquared(TVector const& a)
		{
			return DotProduct(a, a);
		}

		static ScalarType GetDistanceSquared(TVector const& a, TVector const& b)
		{
			// This may be an elegant one-liner, but VC++ won't vectorize it, in contrast to the loop below
			//return std::inner_product(a.begin(), a.end(), b.begin(), ScalarType(0), std::plus<ScalarType>{}, [](ScalarType a, ScalarType b) { return Square(a - b); });

			ScalarType result = 0;
			for (auto i = 0; i < VectorTraits<TVector>::Dimensions; ++i)
			{
				result += Square(a[i] - b[i]);
			}

			return result;
		}
	};

	// Vector implementation using std::array

	template <typename TScalar, size_t NDimensions>
	using Vector = std::array<TScalar, NDimensions>;

	template <class T>
	constexpr auto IsArrayVector = false;

	template <typename TScalar, size_t NDimensions>
	constexpr bool IsArrayVector<std::array<TScalar, NDimensions>> = VectorTraits<std::array<TScalar, NDimensions>>::Dimensions > 0;

	template <typename T>
	constexpr char GetFloatTypeCode()
	{
		if constexpr (std::is_same_v<T, float>)
		{
			return 'f';
		}
		else
		{
			static_assert(std::is_same_v<T, double>);
			return 'd';
		}
	}

	template <typename TScalar, size_t NDimensions>
	struct VectorTraits<std::array<TScalar, NDimensions>> : VectorTraitsDefault<std::array<TScalar, NDimensions>, TScalar, NDimensions>
	{
		static constexpr std::array NameArray = { 'a', 'r', 'r', 'a', 'y', char('0' + NDimensions), GetFloatTypeCode<TScalar>(), char(0) };
		static constexpr std::string_view Name{ NameArray.data() };

		static constexpr auto IsConstexpr = true;

		template <typename TScalar2, size_t NDimensions2>
		using Reconfigure = std::array<TScalar2, NDimensions2>;
	};

	using Vector2 = Vector<double, 2>;
	using Vector3 = Vector<double, 3>;
	using Vector4 = Vector<double, 4>;
	using Vector2f = Vector<float, 2>;
	using Vector3f = Vector<float, 3>;
	using Vector4f = Vector<float, 4>;

	template <class TVector>
	constexpr std::enable_if_t<IsArrayVector<TVector>, TVector> operator+(TVector const& a, TVector const& b)
	{
		return VectorTraits<TVector>::OperatorAdd(a, b);
	}

	template <class TVector>
	constexpr std::enable_if_t<IsArrayVector<TVector>, TVector&> operator+=(TVector& a, TVector const& b)
	{
		a = a + b;
		return a;
	}

	template <class TVector>
	constexpr std::enable_if_t<IsArrayVector<TVector>, TVector> operator-(TVector const& a, TVector const& b)
	{
		return VectorTraits<TVector>::OperatorSub(a, b);
	}

	template <class TVector>
	constexpr std::enable_if_t<IsArrayVector<TVector>, TVector&> operator-=(TVector& a, TVector const& b)
	{
		a = a - b;
		return a;
	}

	template <class TVector>
	constexpr std::enable_if_t<IsArrayVector<TVector>, TVector> operator*(TVector const& v, typename VectorTraits<TVector>::ScalarType s)
	{
		return VectorTraits<TVector>::OperatorMultiplyByScalar(v, s);
	}

	template <class TVector>
	constexpr std::enable_if_t<IsArrayVector<TVector>, TVector&> operator*=(TVector& v, typename VectorTraits<TVector>::ScalarType s)
	{
		v = v * s;
		return v;
	}

	template <class TVector>
	constexpr std::enable_if_t<IsArrayVector<TVector>, TVector> operator*(typename VectorTraits<TVector>::ScalarType s, TVector const& v)
	{
		return VectorTraits<TVector>::OperatorMultiplyByScalar(v, s);
	}

	template <class TVector>
	constexpr std::enable_if_t<IsArrayVector<TVector>, TVector> operator/(TVector const& v, typename VectorTraits<TVector>::ScalarType s)
	{
		return VectorTraits<TVector>::OperatorDivideByScalar(v, s);
	}

	template <class TVector>
	constexpr std::enable_if_t<IsArrayVector<TVector>, TVector&> operator/=(TVector& v, typename VectorTraits<TVector>::ScalarType s)
	{
		v = v / s;
		return v;
	}


	// Vector implementation using Eigen

#if defined( ENABLE_EIGEN )
	template <typename TScalar, size_t NDimensions>
	struct VectorTraits<Eigen::Vector<TScalar, NDimensions>> : VectorTraitsDefault<Eigen::Vector<TScalar, NDimensions>, TScalar, NDimensions>
	{
		static constexpr std::array NameArray = { 'E', 'i', 'g', 'e', 'n', char('0' + NDimensions), GetFloatTypeCode<TScalar>(), char(0) };
		static constexpr std::string_view Name{ NameArray.data() };

		static constexpr auto IsConstexpr = false;

		static constexpr auto Dimensions = NDimensions;

		using ScalarType = TScalar;

		using ArrayType = std::array<TScalar, NDimensions>;

		using VectorType = Eigen::Vector<TScalar, NDimensions>;

		template <typename TScalar2, size_t NDimensions2>
		using Reconfigure = Eigen::Vector<TScalar2, NDimensions2>;


		static constexpr VectorType FromArray(std::array<TScalar, NDimensions> const& v)
		{
			return VectorType(v);
		}

		static constexpr std::array<TScalar, NDimensions> ToArray(VectorType const& v)
		{
			static_assert(sizeof(VectorType) == sizeof(std::array<TScalar, NDimensions>));
			return *reinterpret_cast<std::array<TScalar, NDimensions> const*>(&v);
		}

		static constexpr auto LengthSquared(VectorType const& a)
		{
			return a.squaredNorm();
		}

		static TScalar GetDistanceSquared(VectorType const& a, VectorType const& b)
		{
			return (a - b).squaredNorm();
		}
	};

	using EVector2 = Eigen::Vector<double, 2>;
#endif

	// Operations with generic implementations, based on VectorTraits

	template <class TVector>
	constexpr TVector Zero()
	{
		return VectorTraits<TVector>::Zero();
	}

	template <class TVector>
	constexpr TVector Flat(typename VectorTraits<TVector>::ScalarType value)
	{
		return VectorTraits<TVector>::Flat(value);
	}

	template <class TVector>
	constexpr TVector NaN()
	{
		return Flat<TVector>(std::numeric_limits<typename VectorTraits<TVector>::ScalarType>::quiet_NaN());
	}

	template <class TVector>
	constexpr void Fill(TVector& vector, typename VectorTraits<TVector>::ScalarType value)
	{
		VectorTraits<TVector>::Fill(vector, value);
	}

	template <class TVector, class TUnaryOp>
	constexpr TVector ComponentApply(TVector const& a, TUnaryOp op)
	{
		return Detail::ComponentApplyImpl<TVector>(a, std::forward<TUnaryOp>(op), std::make_index_sequence<VectorTraits<TVector>::Dimensions>());
	}

	template <class TOtherVector, class TVector>
	constexpr TOtherVector Convert(TVector const& a)
	{
		using ScalarType = typename VectorTraits<TVector>::ScalarType;
		using OtherScalarType = typename VectorTraits<TOtherVector>::ScalarType;
		return Detail::ComponentApplyImpl<TOtherVector>(a, [](ScalarType x) { return OtherScalarType(x); }, std::make_index_sequence<VectorTraits<TVector>::Dimensions>());
	}

	template <class TVector, class TBinaryOp>
	constexpr TVector ComponentApply(TVector const& a, TVector const& b, TBinaryOp binaryOp)
	{
		return Detail::ComponentApplyImpl(a, b, std::forward<TBinaryOp>(binaryOp), std::make_index_sequence<VectorTraits<TVector>::Dimensions>());
	}

	template <class TVector>
	constexpr TVector Min(TVector const& a, TVector const& b)
	{
		return VectorTraits<TVector>::Min(a, b);
	}

	template <class TVector>
	constexpr TVector Max(TVector const& a, TVector const& b)
	{
		return VectorTraits<TVector>::Max(a, b);
	}

	template <class TVector>
	constexpr auto DotProduct(TVector const& a, TVector const& b)
	{
		return VectorTraits<TVector>::DotProduct(a, b);
	}

	template <class TVector>
	constexpr TVector ComponentMultiply(TVector const& a, TVector const& b)
	{
		return VectorTraits<TVector>::OperatorMultiply(a, b);
	}

	template <class TVector>
	constexpr TVector ComponentDivide(TVector const& a, TVector const& b)
	{
		return VectorTraits<TVector>::OperatorDivide(a, b);
	}

	template <class TVector>
	constexpr std::enable_if_t<IsArrayVector<TVector>, TVector> operator/(typename VectorTraits<TVector>::ScalarType s, TVector const& v)
	{
		return ComponentDivide(VectorTraits<TVector>::Flat(s), v);
	}

	template <class TVector>
	constexpr auto LengthSquared(TVector const& a)
	{
		return VectorTraits<TVector>::LengthSquared(a);
	}

	template <class TVector>
	constexpr auto MinimumValue(TVector const& a)
	{
		return VectorTraits<TVector>::MinimumValue(a);
	}

	template <class TVector>
	constexpr auto MaximumValue(TVector const& a)
	{
		return VectorTraits<TVector>::MaximumValue(a);
	}

	template <class TVector>
	constexpr auto Sum(TVector const& a)
	{
		return VectorTraits<TVector>::Sum(a);
	}

	// Segment, Interval, Box

	template <class TVector>
	using Segment = std::pair<TVector, TVector>;

	using Segment2 = Segment<Vector2>;

	template <typename T>
	struct Interval
	{
		T min;
		T max = min;
	};

	template <typename T>
	T LinearInterpolate(Interval<T> const& interval, T t)
	{
		DEBUG_ASSERT(interval.min <= interval.max);
		return t <= 0 ? interval.min : t >= 1 ? interval.max : interval.min + t * (interval.max - interval.min);
	}

	template <typename T>
	T ReInterpolate(T x, Interval<T> from, Interval<T> to)
	{
		DEBUG_ASSERT(from.min < from.max);
		auto const t = (x - from.min) / (from.max - from.min);
		return LinearInterpolate(to, t);
	}


	template <class TVector>
	struct Box
	{
		static_assert(IsVector<TVector>, "Type is not a vector");

		using VectorType = TVector;
		using VectorTraitsType = VectorTraits<VectorType>;
		using ScalarType = typename VectorTraitsType::ScalarType;
		static constexpr auto Dimensions = VectorTraitsType::Dimensions;

	private:

		std::array<VectorType, 2> ends_;

	public:

		constexpr Box() noexcept
			: ends_{ NaN<VectorType>(), NaN<VectorType>() }
		{
		}

		explicit constexpr Box(VectorType point) noexcept
			: ends_{ point, point }
		{
		}

		constexpr Box(VectorType min, VectorType max)
			: ends_{ min, max }
		{
			DEBUG_ASSERT(AllOf(ends_[0], ends_[1], std::less_equal<>()));
		}

		template <class TBox>
		static Box Convert( TBox const& other )
		{
			return { GeoToolbox::Convert<VectorType>( other.Min() ), GeoToolbox::Convert<VectorType>( other.Max() ) };
		}

		static constexpr Box Bound(VectorType a, VectorType b)
		{
			return Box{ GeoToolbox::Min(a, b), GeoToolbox::Max(a, b) };
		}

		static constexpr Box FromMinAndSizes(VectorType min, VectorType sizes)
		{
			return Box{ min, min + sizes };
		}

		static constexpr Box FromMinAndSize(VectorType min, ScalarType size)
		{
			return FromMinAndSizes(min, Flat<VectorType>(size));
		}

		static constexpr Box FromCenterAndSizes(VectorType center, VectorType sizes)
		{
			auto const radii = sizes / ScalarType(2);
			return Box{ center - radii, center + radii };
		}

		static constexpr Box FromCenterAndSize(VectorType center, ScalarType size)
		{
			return FromCenterAndSizes(center, Flat<VectorType>(size));
		}

		static constexpr Box Square(ScalarType size)
		{
			return Box{ Zero<VectorType>(), Flat<VectorType>(size) };
		}

		[[nodiscard]] constexpr bool IsEmpty() const noexcept
		{
			// std::isnan turned constexpr only in C++23
			return ends_[0][0] != ends_[0][0];
		}

		[[nodiscard]] constexpr VectorType const& Min() const noexcept
		{
			return ends_[0];
		}

		[[nodiscard]] constexpr VectorType const& Max() const noexcept
		{
			return ends_[1];
		}

		[[nodiscard]] constexpr VectorType const& operator[](int i) const
		{
			DEBUG_ASSERT(i == 0 || i == 1);
			return ends_[i];
		}

		[[nodiscard]] constexpr VectorType Center() const noexcept
		{
			return (ends_[0] + ends_[1]) * 0.5;
		}

		[[nodiscard]] constexpr VectorType Sizes() const noexcept
		{
			return ends_[1] - ends_[0];
		}

		[[nodiscard]] constexpr ScalarType Size(int axis) const noexcept
		{
			return ends_[1][axis] - ends_[0][axis];
		}

		[[nodiscard]] constexpr ScalarType Width() const noexcept
		{
			return Size(0);
		}

		[[nodiscard]] constexpr ScalarType Height() const noexcept
		{
			return Size(1);
		}

		Box& Add(VectorType const& point)
		{
			// *this may be NaN, point may not
			DEBUG_ASSERT(AllOf(point, [](auto x) { return !std::isnan(x); }));

			// Can't use std::min/max here, this must work for empty boxes that use nan coordinates, hence the weird comparisons
			ends_[0] = ComponentApply(ends_[0], point, [](auto x, auto y) { return !(x <= y) ? y : x; });
			ends_[1] = ComponentApply(ends_[1], point, [](auto x, auto y) { return !(x >= y) ? y : x; });
			return *this;
		}

		Box& Add(Box const& other)
		{
			if (!other.IsEmpty())
			{
				Add(other.Min());
				Add(other.Max());
			}

			return *this;
		}

		Box& Move(VectorType const& point)
		{
			if (!IsEmpty())
			{
				ends_[0] += point;
				ends_[1] += point;
			}

			return *this;
		}

		[[nodiscard]] constexpr Box GetReducedFromAbove(int axis, ScalarType newMaximum) const
		{
			auto newMax = ends_[1];
			newMax[axis] = newMaximum;
			return { ends_[0], newMax };
		}

		[[nodiscard]] constexpr Box GetReducedFromBelow(int axis, ScalarType newMinimum) const
		{
			auto newMin = ends_[0];
			newMin[axis] = newMinimum;
			return { newMin, ends_[1] };
		}

		constexpr Box GetScaled(ScalarType factor) const
		{
			return factor > 0 ? FromCenterAndSizes(Center(), Sizes() * factor) : *this;
		}

		friend constexpr Box operator+(Box const& box, VectorType const& point)
		{
			// box may be empty, point may not be NaN
			DEBUG_ASSERT(AllOf(point, [](auto x) { return !std::isnan(x); }));
			return Box{
				ComponentApply(box.ends_[0], point, [](auto x, auto y) { return !(x <= y) ? y : x; }),
				ComponentApply(box.ends_[1], point, [](auto x, auto y) { return !(x >= y) ? y : x; }) };
		}

		friend constexpr bool operator==(Box const& a, Box const& b)
		{
			// std::array::operator== is constexpr only in C++20
			return a.IsEmpty() && b.IsEmpty()
				|| AllOf(a.ends_[0], b.ends_[0], std::equal_to<>()) && AllOf(a.ends_[1], b.ends_[1], std::equal_to<>());
		}
	};

	using Box2 = Box<Vector2>;
	using Box3 = Box<Vector3>;
	using Box2f = Box<Vector2f>;
	using Box3f = Box<Vector3f>;


	template <typename T>
	struct GetVectorType
	{
		using type = T;
	};

	template <typename T>
	struct GetVectorType<Box<T>>
	{
		using type = T;
	};

	template <typename T>
	[[nodiscard]] auto GetLowBound(T const& key)
	{
		if constexpr (IsSpecialization<T, Box>)
		{
			return key[0];
		}
		else
		{
			return key;
		}
	}

	template <typename T>
	[[nodiscard]] auto GetHighBound(T const& key)
	{
		if constexpr (IsSpecialization<T, Box>)
		{
			return key[1];
		}
		else
		{
			return key;
		}
	}

	template <typename T>
	[[nodiscard]] auto GetLowBound(T const& key, int axis)
	{
		if constexpr (IsSpecialization<T, Box>)
		{
			return key[0][axis];
		}
		else
		{
			return key[axis];
		}
	}

	template <typename T>
	auto GetHighBound(T const& key, int axis)
	{
		if constexpr (IsSpecialization<T, Box>)
		{
			return key[1][axis];
		}
		else
		{
			return key[axis];
		}
	}

	template <class TVector>
	[[nodiscard]] bool Overlap(Box<TVector> const& a, Box<TVector> const& b) noexcept
	{
		for (auto i = 0; i < int(VectorTraits<TVector>::Dimensions); ++i)
		{
			if (a.Max()[i] < b.Min()[i] || a.Min()[i] > b.Max()[i])
			{
				return false;
			}
		}

		return true;
	}

	template <class TVector>
	[[nodiscard]] bool Contains(Box<TVector> const& a, Box<TVector> const& b) noexcept
	{
		for (auto i = 0; i < int(VectorTraits<TVector>::Dimensions); ++i)
		{
			if (a.Min()[i] > b.Min()[i] || a.Max()[i] < b.Max()[i])
			{
				return false;
			}
		}

		return true;
	}

	template <class TVector>
	[[nodiscard]] bool Overlap(Box<TVector> const& box, TVector const& point) noexcept
	{
		for (auto i = 0; i < int(VectorTraits<TVector>::Dimensions); ++i)
		{
			if (point[i] < box.Min()[i] || point[i] > box.Max()[i])
			{
				return false;
			}
		}

		return true;
	}

	template <class TVector>
	[[nodiscard]] Box<TVector> Intersect(Box<TVector> const& a, Box<TVector> const& b) noexcept
	{
		auto min = a.Min();
		auto max = a.Max();
		for (auto i = 0; i < int(VectorTraits<TVector>::Dimensions); ++i)
		{
			if (min[i] > b.Max()[i] || max[i] < b.Min()[i])
			{
				return {};
			}

			if (min[i] < b.Min()[i])
			{
				min[i] = b.Min()[i];
			}

			if (max[i] > b.Max()[i])
			{
				max[i] = b.Max()[i];
			}
		}

		return { min, max };
	}

	template <class TVector>
	[[nodiscard]] typename Box<TVector>::VectorType GetClosestPointOnBox(Box<TVector> const& box, typename Box<TVector>::VectorType targetPoint)
	{
		TVector closest{} /* [[indeterminate]] */;
		for (auto i = 0; i < int(VectorTraits<TVector>::Dimensions); ++i)
		{
			closest[i] = std::clamp(targetPoint[i], box.Min()[i], box.Max()[i]);
		}

		return closest;
	}

	template <class TVector>
	[[nodiscard]] auto GetDistanceSquared(TVector const& a, TVector const& b)
	{
		return VectorTraits<TVector>::GetDistanceSquared(a, b);
	}

	template <class TVector>
	[[nodiscard]] auto GetDistance(TVector const& a, TVector const& b)
	{
		return std::sqrt(GetDistanceSquared(a, b));
	}

	template <class TVector>
	[[nodiscard]] auto GetDistanceSquared(TVector const& point, Box<TVector> const& box)
	{
		typename VectorTraits<TVector>::ScalarType result{ 0 };
		for (auto i = 0; i < int(VectorTraits<TVector>::Dimensions); ++i)
		{
			if (point[i] < box.Min()[i])
			{
				result += Square(box.Min()[i] - point[i]);
			}
			else if (point[i] > box.Max()[i])
			{
				result += Square(point[i] - box.Max()[i]);
			}
		}

		return result;
	}

	template <class TVector>
	[[nodiscard]] auto GetDistanceSquared(TVector const& point, Box<TVector> const& box, int axisIndex) -> typename VectorTraits<TVector>::ScalarType
	{
		if (point[axisIndex] < box.Min()[axisIndex])
		{
			return Square(box.Min()[axisIndex] - point[axisIndex]);
		}
		else if (point[axisIndex] > box.Max()[axisIndex])
		{
			return Square(point[axisIndex] - box.Max()[axisIndex]);
		}

		return 0;
	}

	template <class TVector>
	[[nodiscard]] auto GetDistance(TVector const& a, Box<TVector> const& b)
	{
		return std::sqrt(GetDistanceSquared(a, b));
	}


	template <class TIterable, class TGetBoxFunc>
	[[nodiscard]] auto Bound(TIterable const& elements, TGetBoxFunc getBoxFunc)
	{
		// TODO: parallel reduce
		std::decay_t<decltype(Box{ getBoxFunc(*elements.begin()) }) > result;
		for (auto const& element : elements)
		{
			result.Add(getBoxFunc(element));
		}

		return result;
	}

	template <class TIterable>
	[[nodiscard]] auto Bound(TIterable const& elements)
	{
		return Bound(elements, Identity{});
	}

	inline std::ostream& operator<<(std::ostream& stream, Vector2 const& point)
	{
		stream << point[0] << ' ' << point[1];
		return stream;
	}

	template <class TVector>
	TVector ReInterpolate(TVector const& p, Box2 const& fromRange, Box2 const& toRange)
	{
		return
		{
			ReInterpolate(p[0], { fromRange.Min()[0], fromRange.Max()[0] }, { toRange.Min()[0], toRange.Max()[0] }),
			ReInterpolate(p[1], { fromRange.Min()[1], fromRange.Max()[1] }, { toRange.Min()[1], toRange.Max()[1] })
		};
	}


	template <class TVector, class TIterator>
	void MakeCircle(TIterator output, typename VectorTraits<TVector>::ScalarType radius, int vertexCount)
	{
		using ScalarType = typename VectorTraits<TVector>::ScalarType;

		for (auto i = 0; i < vertexCount; ++i)
		{
			auto const angle = i * 2 * Pi / vertexCount;
			*output = radius * TVector{ ScalarType(std::cos(angle)), ScalarType(std::sin(angle)) };
			++output;
		}
	}

	template <class TVector>
	std::vector<TVector> MakeCircle(typename VectorTraits<TVector>::ScalarType radius, int vertexCount)
	{
		std::vector<TVector> result;
		result.reserve(vertexCount);
		MakeCircle(std::back_inserter(result), radius, vertexCount);
		return result;
	}
}
