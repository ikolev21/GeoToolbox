// Copyright 2024-2025 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "GeoToolbox/ShapeFile.hpp"

#include <shapelib-1.5.0/shapefil.h>

#include <stdexcept>
#include <string>

namespace GeoToolbox
{
	void ShapeFileDeleter::operator()(void* shapeFile) const
	{
		SHPClose(static_cast<SHPHandle>(shapeFile));
	}

	void ShapeObjectDeleter::operator()(SHPObject* obj) const
	{
		SHPDestroyObject(obj);
	}

	ShapeFile::ShapeFile(std::filesystem::path filePath)
		: filePath_{ std::move( filePath ) }
		, shapeFile_{ SHPOpen(filePath_.string().c_str(), "rb") }
	{
		if (shapeFile_ != nullptr)
		{
			auto shapeType = SHPT_NULL;
			SHPGetInfo(static_cast<SHPHandle>(shapeFile_.get()), &objectCount_, &shapeType, minBounds_.data(), maxBounds_.data());
			shapeType_ = ShapeType(shapeType);
		}
	}

	[[nodiscard]] auto ShapeFile::GetObject(int index) const -> ShapeObjectPtr
	{
		return index < objectCount_ ? ShapeObjectPtr(SHPReadObject(static_cast<SHPHandle>(shapeFile_.get()), index)) : nullptr;
	}

	[[nodiscard]] double* ShapeFile::GetCoordinates(tagSHPObject const& object, int axis)
	{
		switch (axis)
		{
		case 0: return object.padfX;
		case 1: return object.padfY;
		case 2: return object.padfZ;
		default: throw std::out_of_range{ "axis" };
		}
	}

	[[nodiscard]] Interval<double> ShapeFile::GetBounds(tagSHPObject const& object, int axis)
	{
		switch (axis)
		{
		case 0: return { object.dfXMin, object.dfXMax };
		case 1: return { object.dfYMin, object.dfYMax };
		case 2: return { object.dfZMin, object.dfZMax };
		default: throw std::out_of_range{ "axis" };
		}
	}

	[[nodiscard]] std::vector<Segment2> ShapeFile::GetSegments() const
	{
		std::vector<Segment2> result;
		//limit = limit < 0 ? objectCount_ : std::min(objectCount_, limit);
		for (auto index = 0; index < objectCount_; ++index)
		{
			auto const object = GetObject(index);
			if (object == nullptr)
			{
				continue;
			}

			for (auto partIndex = 0; partIndex < object->nParts; ++partIndex)
			{
				auto const end = partIndex == object->nParts - 1 ? object->nVertices : object->panPartStart[partIndex + 1];
				for (auto i = object->panPartStart[partIndex] + 1; i < end; ++i)
				{
					result.push_back({ { object->padfX[i - 1], object->padfY[i - 1] }, { object->padfX[i], object->padfY[i] } });
				}
			}
		}

		return result;
	}

	bool ShapeFile::Write(std::filesystem::path const& filePath, Span<Vector2 const> points)
	{
		ShapeFilePtr const shapeFile{ SHPCreate(filePath.string().c_str(), SHPT_POINT) };
		if (shapeFile == nullptr)
		{
			return false;
		}

		for (auto const& p : points)
		{
			ShapeObjectPtr const obj{ SHPCreateSimpleObject(SHPT_POINT, 1, &p[0], &p[1], nullptr) };
			if (obj == nullptr)
			{
				return false;
			}

			SHPWriteObject(static_cast<SHPInfo*>(shapeFile.get()), -1, obj.get());
		}

		return true;
	}

	bool ShapeFile::Write(std::filesystem::path const& filePath, Span<Box2 const> boxes)
	{
		ShapeFilePtr const shapeFile{ SHPCreate(filePath.string().c_str(), SHPT_POLYGON ) };
		if (shapeFile == nullptr)
		{
			return false;
		}

		std::array<double, 4> x{}, y{};
		for (auto const& box : boxes)
		{
			x[0] = box.Min()[0];
			y[0] = box.Min()[1];
			x[1] = box.Max()[0];
			y[1] = box.Min()[1];
			x[2] = box.Max()[0];
			y[2] = box.Max()[1];
			x[3] = box.Min()[0];
			y[3] = box.Max()[1];
			ShapeObjectPtr const obj{ SHPCreateSimpleObject(SHPT_POLYGON, 4, x.data(), y.data(), nullptr) };
			if (obj == nullptr)
			{
				return false;
			}

			SHPWriteObject(static_cast<SHPInfo*>(shapeFile.get()), -1, obj.get());
		}

		return true;
	}
}
