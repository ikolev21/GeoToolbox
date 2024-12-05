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
}
