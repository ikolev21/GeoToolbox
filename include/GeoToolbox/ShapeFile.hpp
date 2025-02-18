// Copyright 2024-2025 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include "Span.hpp"
#include "SpatialTools.hpp"

#include <filesystem>
#include <vector>

struct tagSHPObject;

namespace GeoToolbox
{
	struct ShapeFileDeleter
	{
		void operator()(void* shapeFile) const;
	};

	struct ShapeObjectDeleter
	{
		void operator()(tagSHPObject* obj) const;
	};

	enum class ShapeType
	{
		Null = 0,
		Point = 1,
		Arc = 3,
		Polygon = 5,
		MultiPoint = 8,

		PointZ = 11,
		ArcZ = 13,
		PolygonZ = 15,
		MultiPointZ = 18,

		PointM = 21,
		ArcM = 23,
		PolygonM = 25,
		MultiPointM = 28,

		MultiPatch = 31,
	};

	class ShapeFile
	{
		using ShapeFilePtr = std::unique_ptr<void, ShapeFileDeleter>;
		using ShapeObjectPtr = std::unique_ptr<tagSHPObject, ShapeObjectDeleter>;

		std::filesystem::path filePath_;
		ShapeFilePtr shapeFile_;
		int objectCount_ = 0;
		ShapeType shapeType_ = ShapeType::Null;
		std::array<double, 4> minBounds_{};
		std::array<double, 4> maxBounds_{};

	public:

		explicit ShapeFile(std::filesystem::path filePath);

		static bool Write(std::filesystem::path const& filePath, Span<Vector2 const> );

		static bool Write(std::filesystem::path const& filePath, Span<Box2 const> );

		[[nodiscard]] std::filesystem::path const& GetFilePath() const noexcept
		{
			return filePath_;
		}

		[[nodiscard]] int GetObjectCount() const noexcept
		{
			return objectCount_;
		}

		[[nodiscard]] ShapeType GetShapeType() const noexcept
		{
			return ShapeType(shapeType_);
		}

		template <typename TSpatialKey>
		[[nodiscard]] bool Supports() const noexcept
		{
			static_assert(SpatialKeyTraits<TSpatialKey>::Dimensions <= 3);

			if constexpr (SpatialKeyIsPoint<TSpatialKey>)
			{
				return Contains(
					std::array{ ShapeType::Point, ShapeType::PointM, ShapeType::PointZ, ShapeType::MultiPoint, ShapeType::MultiPointM, ShapeType::MultiPointZ },
					GetShapeType());
			}
			else if constexpr (SpatialKeyIsBox<TSpatialKey>)
			{
				return Contains(
					std::array{ ShapeType::Arc, ShapeType::ArcM, ShapeType::ArcZ, ShapeType::Polygon, ShapeType::PolygonM, ShapeType::PolygonZ },
					GetShapeType());
			}
			else
			{
				return false;
			}
		}

		[[nodiscard]] ShapeObjectPtr GetObject(int index) const;

		[[nodiscard]] static double* GetCoordinates(tagSHPObject const& object, int axis);

		[[nodiscard]] static Interval<double> GetBounds(tagSHPObject const& object, int axis);

		template <typename TSpatialKey>
		[[nodiscard]] TSpatialKey GetKey(tagSHPObject const& object) const noexcept
		{
			static_assert(SpatialKeyTraits<TSpatialKey>::Dimensions <= 3);

			if constexpr (SpatialKeyIsPoint<TSpatialKey>)
			{
				return { GetCoordinates(object, 0)[0], GetCoordinates(object, 1)[0] };
			}
			else
			{
				static_assert(SpatialKeyIsBox<TSpatialKey>, "Not implemented for this type");
				return { { GetBounds(object, 0).min, GetBounds(object, 1).min }, { GetBounds(object, 0).max, GetBounds(object, 1).max } };
			}
		}

		template <typename TSpatialKey>
		[[nodiscard]] std::vector<TSpatialKey> GetKeys(int limit = -1) const
		{
			std::vector<TSpatialKey> result;
			limit = limit < 0 ? objectCount_ : std::min(objectCount_, limit);
			for (auto index = 0; index < limit; ++index)
			{
				auto const object = GetObject(index);
				if (object == nullptr)
				{
					continue;
				}

				result.push_back(GetKey<TSpatialKey>(*object));
			}

			return result;
		}

		[[nodiscard]] std::vector<Segment2> GetSegments() const;
	};
}
