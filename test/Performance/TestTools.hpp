#pragma once

#include "GeoToolbox/Config.hpp"
#include "GeoToolbox/DescribeStruct.hpp"
#include "GeoToolbox/Span.hpp"
#include "GeoToolbox/SpatialTools.hpp"

#include "catch2/generators/catch_generators.hpp"

#include <filesystem>
#include <map>
#include <utility>

namespace GeoToolbox
{
	class Image;
}

static constexpr auto Kilobyte = 1024;


template <typename T = int64_t>
T GetMicroseconds()
{
	using namespace std::chrono;
	return static_cast<T>(duration_cast<microseconds>(high_resolution_clock::now().time_since_epoch()).count());
}

std::filesystem::path GetRootPath();

std::filesystem::path GetOutputPath();

GeoToolbox::Config& GetConfig();

std::string GetCatchTestName();

bool IsSelected(char const* envVarName, std::string_view currentValue, int printMessageWithIndent = -1);


std::pair<int, int> GetDatasetSizeRange();

inline int GetDatasetSizeFromOrder(int order)
{
	return static_cast<int>(pow(10, order));
}

template <typename TSpatialKey>
class Dataset
{
public:

	using BoxType = typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::BoxType;

	Dataset() = default;

	Dataset(std::string name, std::vector<GeoToolbox::Feature<TSpatialKey>> data)
		: name_{ std::move(name) }
		, data_{ std::move(data) }
		, size_{ int(data_.size()) }
	{
	}

	Dataset(std::string name, std::vector<TSpatialKey> const& keys);

	[[nodiscard]] std::string const& GetName() const noexcept
	{
		return name_;
	}

	[[nodiscard]] bool IsEmpty() const noexcept
	{
		return data_.empty();
	}

	[[nodiscard]] int GetSize() const noexcept
	{
		return size_;
	}

	[[nodiscard]] int GetAvailableSize() const noexcept
	{
		return int(data_.size());
	}

	void SetSize(int newSize);

	[[nodiscard]] GeoToolbox::Span<GeoToolbox::Feature<TSpatialKey> const> GetData() const noexcept(GeoToolbox::IsReleaseBuild)
	{
		return { data_.data(), size_ };
	}

	[[nodiscard]] std::vector<typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::SpatialKeyArrayType> GetKeys() const
	{
		using VectorTraits = typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::VectorTraitsType;

		if constexpr (GeoToolbox::SpatialKeyIsPoint<TSpatialKey>)
		{
			return GeoToolbox::Transform(data_, [](auto const& feature)
				{
					return VectorTraits::ToArray(feature.spatialKey);
				});
		}
		else
		{
			static_assert(GeoToolbox::SpatialKeyIsBox<TSpatialKey>);
			return GeoToolbox::Transform(data_, [](auto const& feature)
				{
					return GeoToolbox::Box<typename VectorTraits::ArrayType>{ VectorTraits::ToArray(feature.spatialKey.Min()), VectorTraits::ToArray(feature.spatialKey.Max()) };
				});
		}
	}

	BoxType GetBoundingBox() const;

	void Clear();

private:
	std::string name_;

	std::vector<GeoToolbox::Feature<TSpatialKey>> data_{};

	int size_ = 0;

	mutable BoxType boundingBox_;
};

template <typename TSpatialKeyArray>
void DrawSpatialKeys(GeoToolbox::Image& image, std::vector<TSpatialKeyArray> const&, GeoToolbox::Box2 const& boundingBox);

template <typename TSpatialKey>
void Draw(GeoToolbox::Image& image, Dataset<TSpatialKey> const& set)
{
	auto const toArray = GeoToolbox::SpatialKeyTraits<TSpatialKey>::VectorTraitsType::ToArray;

	auto keys = set.GetKeys();
	auto const& origBox = set.GetBoundingBox();
	auto const box = GeoToolbox::Box2{ toArray(origBox.Min()), toArray(origBox.Max()) };
	DrawSpatialKeys(image, keys, box);
}


struct PerfRecord
{
	static constexpr auto Version = 1;

	struct Entry
	{
		std::string_view spatialIndexName;
		GeoToolbox::SpatialKeyKind spatialKeyKind = GeoToolbox::SpatialKeyKind::Undefined;
		int dimensions = 0;
		std::string_view vectorTraits;
		std::string_view scenario;
		std::string_view operation;
		std::string_view datasetName;
		int64_t datasetSize = 0;


		static constexpr auto DescribeStruct()
		{
			using GeoToolbox::Field;

			return std::make_tuple(
				Field{ &Entry::scenario, "Scenario" },
				Field{ &Entry::operation, "Operation" },
				Field{ &Entry::spatialKeyKind, "Spatial Key" },
				Field{ &Entry::dimensions, "Dimensions" },
				Field{ &Entry::vectorTraits, "Vector Impl" },
				Field{ &Entry::datasetName, "Dataset Name" },
				Field{ &Entry::datasetSize, "Dataset Size" },
				Field{ &Entry::spatialIndexName, "Spatial Index" });
		}

		friend bool operator<(Entry const& left, Entry const& right) noexcept
		{
			return GeoToolbox::AsTuple(left) < GeoToolbox::AsTuple(right);
		}
	};

private:

	struct Stats
	{
		int64_t bestTime = std::numeric_limits<int64_t>::max();
		int64_t memoryDelta = std::numeric_limits<int64_t>::max();
		bool failed = false;
	};

	std::string name_;
	std::string environmentId_;
	std::filesystem::path filepath_;

	std::map<Entry, Stats> entries_;

	GeoToolbox::StringStorage stringStorage_;

	bool modified_ = false;

public:

	explicit PerfRecord(std::string name);

	void Save() const;

	void Load();

	template <typename TSpatialKey>
	Entry MakeEntry(Dataset<TSpatialKey> const& dataset, std::string_view spatialIndexName, std::string_view scenario, std::string_view operation)
	{
		return Entry{
			stringStorage_.GetOrAddString(spatialIndexName),
			GeoToolbox::SpatialKeyTraits<TSpatialKey>::Kind,
			GeoToolbox::SpatialKeyTraits<TSpatialKey>::Dimensions,
			GeoToolbox::SpatialKeyTraits<TSpatialKey>::VectorTraitsType::Name,
			stringStorage_.GetOrAddString(scenario),
			stringStorage_.GetOrAddString(operation),
			stringStorage_.GetOrAddString(dataset.GetName()),
			dataset.GetSize()
		};
	}

	void MergeEntry(Entry const& entry, int64_t time, int64_t memoryDelta = 0, bool failed = false);

	void SetEntry(Entry const& entry, int64_t time, int64_t memoryDelta = 0, bool failed = false);
};
