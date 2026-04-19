// Copyright 2024-2026 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include "GeoToolbox/Config.hpp"
#include "GeoToolbox/DescribeStruct.hpp"
#include "GeoToolbox/Profiling.hpp"
#include "GeoToolbox/Span.hpp"
#include "GeoToolbox/SpatialTools.hpp"
#include "GeoToolbox/TestTools.hpp"

#include <filesystem>
#include <iostream>
#include <map>
#include <utility>

namespace GeoToolbox
{
	class Image;
}

static constexpr auto Kilobyte = 1024;

static constexpr auto SetColorRed = "\033[31m";
static constexpr auto ResetColor = "\033[0m";


template <typename T = int64_t>
T GetMicroseconds()
{
	using namespace std::chrono;
	return static_cast<T>(duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

template <typename T = int64_t>
T GetMilliseconds()
{
	using namespace std::chrono;
	return static_cast<T>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

std::filesystem::path GetRootPath();

std::filesystem::path GetOutputPath();

GeoToolbox::Config& GetConfig();

bool PrintVerboseMessages();

std::string GetCatchTestName();

// Checks if testValue is included in the comma-separated list contained in configKey. If printMessageWithIndentLevel >= 0, print a "Skipped" message with that many Tabs in front
bool IsSelected(char const* configKey, std::string_view testValue, int printMessageWithIndentLevel = -1, bool selectedByDefault = true);


inline void WarnInDebugBuild()
{
#ifndef NDEBUG
	std::cout << SetColorRed << "\nWARNING! Running unoptimized (not 'Release') build\n" << ResetColor;
#endif // !NDEBUG
}


std::pair<int, int> GetDatasetSizeRange();

inline int GetDatasetSizeFromOrder(int order)
{
	return static_cast<int>(pow(10, order));
}

template <typename TSpatialKey>
class Dataset
{
public:

	using ScalarType = typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::ScalarType;
	using BoxType = typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::BoxType;

	Dataset() = default;
	//~Dataset() = default;
	//Dataset(Dataset const&) = delete;
	//Dataset& operator=(Dataset const&) = delete;
	//Dataset(Dataset&&) = default;
	//Dataset& operator=(Dataset&&) = default;

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

	void SetSize(int newSize)
	{
		if (onSizeChange_ != nullptr)
		{
			onSizeChange_(*this, newSize);
		}
		else
		{
			SetSize_(newSize);
		}
	}

	[[nodiscard]] GeoToolbox::Span<GeoToolbox::Feature<TSpatialKey> const> GetData() const
	{
		return { data_.data(), size_ };
	}

	[[nodiscard]] std::vector<GeoToolbox::FeatureId> GetIds() const
	{
		return GeoToolbox::Transform(GetData(), [](auto const& feature)
			{
				return feature.id;
			});
	}

	[[nodiscard]] std::vector<typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::SpatialKeyArrayType> GetKeys() const
	{
		using VectorTraits = typename GeoToolbox::SpatialKeyTraits<TSpatialKey>::VectorTraitsType;

		if constexpr (GeoToolbox::SpatialKeyIsPoint<TSpatialKey>)
		{
			return GeoToolbox::Transform(GetData(), [](auto const& feature)
				{
					return VectorTraits::ToArray(feature.spatialKey);
				});
		}
		else
		{
			static_assert(GeoToolbox::SpatialKeyIsBox<TSpatialKey>);
			return GeoToolbox::Transform(GetData(), [](auto const& feature)
				{
					return GeoToolbox::Box<typename VectorTraits::ArrayType>{ VectorTraits::ToArray(feature.spatialKey.Min()), VectorTraits::ToArray(feature.spatialKey.Max()) };
				});
		}
	}

	BoxType GetBoundingBox() const;

	auto GetSmallestExtent() const
	{
		auto const sizes = GetBoundingBox().Sizes();
		auto minSize = std::numeric_limits<ScalarType>::max();
		for (auto i = 0u; i < GeoToolbox::SpatialKeyTraits<TSpatialKey>::Dimensions; ++i)
		{
			if (sizes[i] > 0 && sizes[i] < minSize)
			{
				minSize = sizes[i];
			}
		}

		return minSize;
	}

	void Clear();

protected:

	static BoxType GetFeatureBox(GeoToolbox::Feature<TSpatialKey> const& feature)
	{
		return BoxType(feature.spatialKey);
	}

	void SetSize_(int newSize);


	std::string name_;

	std::vector<GeoToolbox::Feature<TSpatialKey>> data_{};

	int size_ = 0;

	mutable BoxType boundingBox_;

	void (*onSizeChange_)(Dataset&, int newSize) = nullptr;
};

template <typename TSpatialKey>
void Draw([[maybe_unused]] GeoToolbox::Image& image, [[maybe_unused]] Dataset<TSpatialKey> const& set)
{
	if constexpr (GeoToolbox::SpatialKeyTraits<TSpatialKey>::Dimensions == 2)
	{
		auto const toArray = GeoToolbox::SpatialKeyTraits<TSpatialKey>::VectorTraitsType::ToArray;

		auto keys = set.GetKeys();
		auto const& origBox = set.GetBoundingBox();
		auto const box = GeoToolbox::Box2{ toArray(origBox.Min()), toArray(origBox.Max()) };
		DrawSpatialKeys(image, keys, box);
	}
}


class PerfRecord
{
public:

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

	struct Stats
	{
		int64_t bestTime = std::numeric_limits<int64_t>::max();
		int64_t memoryDelta = 0;// std::numeric_limits<int64_t>::max();
		bool failed = false;

		int queryVisitedNodes = 0;
		int queryScalarComparisons = 0;
		int queryBoxOverlaps = 0;
		int queryObjectTests = 0;

		std::string info{};  // NOLINT(readability-redundant-member-init)


		static constexpr auto DescribeStruct()
		{
			using GeoToolbox::Field;

			return std::make_tuple(
				Field{ &Stats::bestTime, "Time" },
				Field{ &Stats::queryVisitedNodes, "NodeVisits" },
				Field{ &Stats::queryObjectTests, "ObjTests" },
				Field{ &Stats::queryScalarComparisons, "Scalar <>" },
				Field{ &Stats::queryBoxOverlaps, "Box <>" },
				Field{ &Stats::memoryDelta, "Mem Delta" },
				Field{ &Stats::failed, "Failed" },
				Field{ &Stats::info, "Info" });
		}

		friend bool operator==(Stats const& left, Stats const& right) noexcept
		{
			return GeoToolbox::AsTuple(left) == GeoToolbox::AsTuple(right);
		}

		friend bool operator!=(Stats const& left, Stats const& right) noexcept
		{
			return !(left == right);
		}
	};

private:

	std::string name_;
	std::string fileId_;
	std::string runId_;
	std::filesystem::path filepath_;
	std::vector<std::string> prefixLines_;
	std::vector<std::string> otherIdLines_;

	std::map<Entry, Stats> entries_;

	GeoToolbox::StringStorage stringStorage_;

	bool modified_ = false;

public:

	explicit PerfRecord(std::string name, std::string runId = {}, std::string fileId = {});

	[[nodiscard]] std::string const& GetRunId() const noexcept
	{
		return runId_;
	}

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

	void MergeEntry(Entry const& entry, Stats const&, std::pair<int64_t, int64_t>* accumulatedOldAndNewBestTimes = nullptr);

	void SetEntry(Entry const& entry, Stats const&);
};
