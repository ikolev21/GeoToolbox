#pragma once

#include "GeoToolbox/GeometryTools.hpp"
#include "GeoToolbox/StlExtensions.hpp"

#include <cstdint>

namespace GeoToolbox
{
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
	};

	template <typename TVector>
	struct SpatialKeyTraits<Box<TVector>> : SpatialKeyTraitsDefaults<TVector, SpatialKeyKind::Box>
	{
		using SpatialKeyArrayType = Box<typename SpatialKeyTraitsDefaults<TVector, SpatialKeyKind::Point>::ArrayType>;
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
	struct BoxBoundTraits<Feature<TSpatialKey>>
	{
		using BoxType = typename SpatialKeyTraits<TSpatialKey>::BoxType;

		static BoxType GetBox(Feature<TSpatialKey> const& feature)
		{
			return BoxType(feature.spatialKey);
		}
	};

	using SpatialKeyTypes = TypeList<
		Vector2, Box2
#if defined( ENABLE_EIGEN )
		, EVector2, Box<EVector2>
#endif
		/*, Vector3, Box3*/>;
}

template <typename TSpatialKey>
struct std::hash<GeoToolbox::Feature<TSpatialKey>>
{
	constexpr std::size_t operator()(GeoToolbox::Feature<TSpatialKey> const& f) const noexcept
	{
		return std::hash<GeoToolbox::FeatureId>()(f.id);
	}
};
