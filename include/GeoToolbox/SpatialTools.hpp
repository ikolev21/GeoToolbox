// Copyright 2024-2026 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include "GeoToolbox/GeometryTools.hpp"
#include "GeoToolbox/Iterators.hpp"
#include "GeoToolbox/StlExtensions.hpp"

#include <random>
#include <sstream>

namespace GeoToolbox
{
	// A common constant that may be used by tree-based spatial indices. This cannot be determined at runtime because at least one index (Boost R-Tree) needs it as a template parameter
	static constexpr auto MaxElementsPerNode = 32;

	enum class SpatialKeyKind
	{
		Undefined,

		Point,
		Box,
	};

	inline SpatialKeyKind SpatialKeyKindFromString(std::string_view name)
	{
		return name == "point" ? SpatialKeyKind::Point : name == "box" ? SpatialKeyKind::Box : SpatialKeyKind::Undefined;
	}

	constexpr std::string_view ToString(SpatialKeyKind key)
	{
		switch (key)
		{
		case SpatialKeyKind::Point: return "point";
		case SpatialKeyKind::Box: return "box";
		default: throw std::out_of_range("SpatialKeyKind value cannot be converted to string");
		}
	}

	inline std::ostream& operator<<(std::ostream& out, SpatialKeyKind kind)
	{
		return out << ToString(kind);
	}

	inline std::istream& operator>>(std::istream& in, SpatialKeyKind& kind)
	{
		std::string kindText;
		in >> kindText;
		kind = SpatialKeyKindFromString(kindText);
		return in;
	}


	template <class TVector, SpatialKeyKind NSpatialKeyKind>
	struct SpatialKeyTraitsDefaults
	{
		static_assert(IsVector<TVector>, "Type is not a vector");

		using VectorType = TVector;

		using VectorTraitsType = VectorTraits<TVector>;

		static constexpr auto Dimensions = VectorTraitsType::Dimensions;

		using ScalarType = typename VectorTraitsType::ScalarType;

		using BoxType = Box<VectorType>;

		using ArrayType = typename VectorTraitsType::ArrayType;

		static constexpr auto Kind = NSpatialKeyKind;

		static constexpr auto Name = ToString(Kind);

		static std::string const& GetName()
		{
			static auto name = std::string(ToString(Kind)) + "_" + std::string(VectorTraitsType::Name);
			return name;
		}
	};

	template <class TVector>
	struct SpatialKeyTraits : SpatialKeyTraitsDefaults<TVector, SpatialKeyKind::Point>
	{
		using SpatialKeyArrayType = typename SpatialKeyTraitsDefaults<TVector, SpatialKeyKind::Point>::ArrayType;

		static TVector GetCenter(TVector const& key)
		{
			return key;
		}
	};

	template <typename TVector>
	struct SpatialKeyTraits<Box<TVector>> : SpatialKeyTraitsDefaults<TVector, SpatialKeyKind::Box>
	{
		using SpatialKeyArrayType = Box<typename SpatialKeyTraitsDefaults<TVector, SpatialKeyKind::Point>::ArrayType>;

		static TVector GetCenter(Box<TVector> const& key)
		{
			return key.Center();
		}
	};

	template <class TSpatialKey>
	constexpr bool SpatialKeyIsPoint = SpatialKeyTraits<TSpatialKey>::Kind == SpatialKeyKind::Point;

	template <class TSpatialKey>
	constexpr bool SpatialKeyIsBox = SpatialKeyTraits<TSpatialKey>::Kind == SpatialKeyKind::Box;


	using FeatureId = std::intptr_t;

	template <typename TSpatialKey>
	struct Feature
	{
		FeatureId id = 0;
		TSpatialKey spatialKey{};

		friend bool operator==(Feature const& a, Feature const& b)
		{
			return a.id == b.id;
		}
	};


	template <typename TSpatialKey>
	struct Features
	{
		std::vector<FeatureId> ids;
		std::vector<TSpatialKey> spatialKeys;

		Feature<TSpatialKey> operator[](int index)
		{
			return { ids[index], spatialKeys[index] };
		}
	};


	template <class TVector, class TRandomGenerator>
	[[nodiscard]] Box<TVector> MakeRandomBox(
		TRandomGenerator& randomGenerator,
		TVector const& center,
		Box<TVector> const& boundingBox,
		std::uniform_real_distribution<typename VectorTraits<TVector>::ScalarType>& heightDistribution,
		std::uniform_real_distribution<typename VectorTraits<TVector>::ScalarType>& aspectDistribution)
	{
		auto const halfHeight = heightDistribution(randomGenerator) / 2;
		auto const halfWidth = halfHeight * aspectDistribution(randomGenerator);
		auto v = TVector{ halfWidth, halfHeight };
		if constexpr (VectorTraits<TVector>::Dimensions == 3)
		{
			v[2] = v[1];
		}

		return Intersect(Box<TVector>{ center - v, center + v }, boundingBox);
	}

	template <class TRandomGenerator, std::size_t... Indices>
	constexpr auto MakeRandomArray(TRandomGenerator const& randomGenerator, std::index_sequence<Indices...>)
	{
		return std::array{ ((void)Indices, randomGenerator())... };
	}

	template <class TSpatialKey, class TRandomGenerator>
	[[nodiscard]] std::vector<Feature<TSpatialKey>> MakeRandomSpatialKeys(
		TRandomGenerator& randomGenerator,
		int datasetSize,
		Box<typename SpatialKeyTraits<TSpatialKey>::VectorType> const& boundingBox,
		Interval<typename SpatialKeyTraits<TSpatialKey>::ScalarType> heightMinMax,
		typename SpatialKeyTraits<TSpatialKey>::ScalarType skewPower = 0,
		typename SpatialKeyTraits<TSpatialKey>::ScalarType averageBoxAspect = 1,
		int islandsCount = 1)
	{
		using ScalarType = typename SpatialKeyTraits<TSpatialKey>::ScalarType;
		using VectorType = typename SpatialKeyTraits<TSpatialKey>::VectorType;

		ASSERT(datasetSize > 0);
		ASSERT(averageBoxAspect >= 1);

		std::uniform_real_distribution positionDistribution{ ScalarType{ 0 }, ScalarType{ 1 } };
		std::uniform_real_distribution heightDistribution{ heightMinMax.min, heightMinMax.max };
		std::uniform_real_distribution aspectDistribution{ ScalarType{ 1 }, 2 * averageBoxAspect - 1 };
		auto islandsIndex = 0;

		std::vector<Feature<TSpatialKey>> data{ size_t(datasetSize) };
		islandsCount = std::max(islandsCount, 1);
		auto const islandSizes = boundingBox.Sizes() / ScalarType(islandsCount);
		for (auto i = 0; i < datasetSize; ++i)
		{
			auto randomArray = MakeRandomArray([&]() { return positionDistribution(randomGenerator); }, std::make_index_sequence<VectorTraits<VectorType>::Dimensions>());
			auto randomPoint = VectorTraits<VectorType>::FromArray(randomArray);
			if (skewPower > 1)
			{
				randomPoint[0] = ScalarType(pow(double(randomPoint[0]), double(skewPower)));
			}

			auto center = boundingBox.Min() + ComponentMultiply(islandSizes, randomPoint);
			if (islandsCount > 1)
			{
				center += ScalarType(islandsIndex) * islandSizes;
				islandsIndex = (islandsIndex + 1) % islandsCount;
			}

			if constexpr (SpatialKeyIsPoint<TSpatialKey>)
			{
				data[i] = { i, center };
			}
			else
			{
				data[i] = { i, MakeRandomBox<VectorType>(randomGenerator, center, boundingBox, heightDistribution, aspectDistribution) };
			}
		}

		return data;
	}

	// Generates query boxes in a grid over a provided box extent. The first and last row/column in the grid are outside the extent. Query sizes are cycled over a provided list.
	template <class TVector>
	class QueryIterator
	{
		using ScalarType = typename VectorTraits<TVector>::ScalarType;
		static constexpr auto Dimensions = int(VectorTraits<TVector>::Dimensions);


		std::array<int, Dimensions> index_{ -1 };

		int samplesPerAxis_ = 10;

		int sizeIndex_ = 0;

		Box<TVector> datasetBounds_;

		TVector datasetSizes_;

		std::vector<ScalarType> sizes_;

		Box<TVector> box_;

	public:

		using value_type = Box<TVector>;
		using pointer = value_type const*;
		using reference = value_type const&;
		using iterator_category = std::forward_iterator_tag;
		using difference_type = ptrdiff_t;

		QueryIterator() = default;

		QueryIterator(TVector datasetSample, Box<TVector> const& datasetBounds, int samplesPerAxis, std::vector<ScalarType> sizes)
			: samplesPerAxis_{ samplesPerAxis }
			, datasetBounds_{ datasetBounds.GetScaled( ScalarType( 1.125 ) ) }
			, datasetSizes_{ datasetBounds_.Sizes() }
			, sizes_{ std::move(sizes) }
			// Start with a query that coincides with an element of the dataset, to test this scenario, and to guarantee we always find at least one element
			, box_{ Box<TVector>::FromCenterAndSize(datasetSample, sizes_.back()) }
		{
			index_[0] = 0;
			index_[Dimensions - 1] = -1;
		}

		[[nodiscard]] Iterable<QueryIterator> MakeRange() const
		{
			return { *this, QueryIterator{} };
		}

		[[nodiscard]] QueryIterator SetSize(ScalarType newSize) const
		{
			auto result = *this;
			result.size_ = newSize;
			return result;
		}

		reference operator* () const
		{
			return box_;
		}

		pointer operator-> () const noexcept
		{
			return &box_;
		}

		QueryIterator& operator++()
		{
			if (index_[0] >= 0)
			{
				for (auto dim = Dimensions - 1; dim >= 0; --dim)
				{
					++index_[dim];
					if (index_[dim] < samplesPerAxis_)
					{
						for (auto rest = dim + 1; rest < Dimensions; ++rest)
						{
							index_[rest] = 0;
						}

						break;
					}

					if (dim == 0)
					{
						index_ = { -1 };
						box_ = {};
						return *this;
					}
				}
			}

			if (samplesPerAxis_ < 2)
			{
				box_ = Box<TVector>::FromCenterAndSize(datasetBounds_.Center(), sizes_[sizeIndex_]);
			}
			else
			{
				TVector corner{};
				for (auto dim = 0; dim < Dimensions; ++dim)
				{
					corner[dim] = datasetBounds_.Min()[dim] + index_[dim] * datasetSizes_[dim] / (samplesPerAxis_ - 1);
				}

				box_ = Box<TVector>::FromCenterAndSize(corner, sizes_[sizeIndex_]);
			}

			sizeIndex_ = (sizeIndex_ + 1) % sizes_.size();
			return *this;
		}

		[[nodiscard]] friend bool operator==(QueryIterator const& a, QueryIterator const& b)
		{
			return a.index_[0] < 0 && b.index_[0] < 0 || a.index_ == b.index_;
		}

		[[nodiscard]] friend bool operator!=(QueryIterator const& a, QueryIterator const& b)
		{
			return !(a == b);
		}
	};


#define ENABLE_QUERYSTATS

	extern struct QueryStats
	{
		int ScalarComparisonsCount = 0;
		int BoxOverlapsCount = 0;
		int ObjectTestsCount = 0;
		int VisitedNodesCount = 0;
		int QueryCount = 0;

		[[nodiscard]] bool IsEmpty() const noexcept;

		void Clear() noexcept;

		[[nodiscard]] auto AsTuple() const noexcept
		{
			return std::make_tuple(ScalarComparisonsCount, BoxOverlapsCount, ObjectTestsCount, VisitedNodesCount);
		}

		QueryStats& operator+=(QueryStats const& other)
		{
			ScalarComparisonsCount += other.ScalarComparisonsCount;
			BoxOverlapsCount += other.BoxOverlapsCount;
			ObjectTestsCount += other.ObjectTestsCount;
			VisitedNodesCount += other.VisitedNodesCount;
			++QueryCount;
			return *this;
		}

		friend bool operator<(QueryStats const& left, QueryStats const& right) noexcept
		{
			return left.AsTuple() < right.AsTuple();
		}

		std::string DebugPrint() const
		{
			if (IsEmpty())
			{
				return {};
			}

			std::ostringstream stream;
			auto count = 1.0;
			if (QueryCount > 0)
			{
				stream << '[' << QueryCount << "] ";
				count = QueryCount;
			}

			stream << "Comp:" << ScalarComparisonsCount / count << " BoxComp:" << BoxOverlapsCount / count << " ObjTest:" << ObjectTestsCount / count << " Nodes:" << VisitedNodesCount / count;
			return stream.str();
		}
	} TheQueryStats;

	inline std::ostream& operator<<(std::ostream& stream, QueryStats const& stats)
	{
		if (stats.IsEmpty())
		{
			stream << -1;
		}
		else
		{
			stream << stats.ScalarComparisonsCount << ' ' << stats.BoxOverlapsCount << ' ' << stats.ObjectTestsCount << ' ' << stats.VisitedNodesCount;
		}

		return stream;
	}

	inline std::istream& operator>>(std::istream& stream, QueryStats& stats)
	{
		int scalar;
		stream >> scalar;
		if (scalar > 0)
		{
			stats.ScalarComparisonsCount = scalar;
			stream >> stats.BoxOverlapsCount;
			stream >> stats.ObjectTestsCount;
			stream >> stats.VisitedNodesCount;
		}

		return stream;
	}

#ifdef ENABLE_QUERYSTATS

	inline bool QueryStats::IsEmpty() const noexcept
	{
		return ScalarComparisonsCount == 0 && BoxOverlapsCount == 0 && ObjectTestsCount == 0 && VisitedNodesCount == 0;
	}

	inline void QueryStats::Clear() noexcept
	{
		ScalarComparisonsCount = 0;
		BoxOverlapsCount = 0;
		ObjectTestsCount = 0;
		VisitedNodesCount = 0;
		QueryCount = 0;
	}

	inline void AddQueryStats_ScalarComparisonsCount()
	{
		++TheQueryStats.ScalarComparisonsCount;
	}

	inline void AddQueryStats_BoxOverlapsCount()
	{
		++TheQueryStats.BoxOverlapsCount;
	}

	inline void AddQueryStats_ObjectTestsCount()
	{
		++TheQueryStats.ObjectTestsCount;
	}

	inline void AddQueryStats_VisitedNodesCount()
	{
		++TheQueryStats.VisitedNodesCount;
	}
#else

	inline bool QueryStats::IsEmpty() const noexcept
	{
		return true;
	}

	inline void QueryStats::Clear() noexcept
	{
	}

	inline void AddQueryStats_ScalarComparisonsCount()
	{
	}

	inline void AddQueryStats_BoxOverlapsCount()
	{
	}

	inline void AddQueryStats_ObjectTestsCount()
	{
	}

	inline void AddQueryStats_VisitedNodesCount()
	{
	}
#endif
}

template <typename TSpatialKey>
struct std::hash<GeoToolbox::Feature<TSpatialKey>>
{
	constexpr std::size_t operator()(GeoToolbox::Feature<TSpatialKey> const& f) const noexcept
	{
		return std::hash<GeoToolbox::FeatureId>()(f.id);
	}
};
