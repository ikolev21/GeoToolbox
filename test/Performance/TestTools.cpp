// Copyright 2024-2025 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "TestTools.hpp"

#include "GeoToolbox/Image.hpp"
#include "GeoToolbox/Iterators.hpp"

#include "catch2/catch_session.hpp"
#include "catch2/catch_template_test_macros.hpp"

#include <array>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace GeoToolbox;
using namespace std;
using namespace std::string_view_literals;


static constexpr string_view FileExtension = ".tsv";

static constexpr string_view TimeColumnName = "Time";

static constexpr string_view MemoryDeltaColumnName = "Mem Delta";

static constexpr auto Separator = '\t';


Config& GetConfig()
{
	static Config theConfig;
	return theConfig;
}


filesystem::path GetRootPath()
{
	static filesystem::path const rootPath = []
		{
			auto result = filesystem::current_path();
			for (; !is_regular_file(result / "test/Performance/CMakeLists.txt"); result = result.parent_path())
			{
				if (!result.has_parent_path())
				{
					return filesystem::path{};
				}
			}

			return result;
		}();

	return rootPath;
}

filesystem::path GetOutputPath()
{
	static filesystem::path const outputPath = []
		{
			filesystem::path const binDir{ CMAKE_BINARY_DIR };
			if (!is_directory(binDir))
			{
				return filesystem::path{};
			}

			return binDir / "testResults";
		}();

	return outputPath;
}


string GetCatchTestName()
{
	auto testName = Catch::getResultCapture().getCurrentTestName();
	replace(testName.begin(), testName.end(), ':', '_');
	return testName;
}


pair<int, int> GetDatasetSizeRange()
{
	auto const selectedDatasetSize = GetConfig().GetValue("DatasetSize", -1);

	return selectedDatasetSize > 0
		? pair{ selectedDatasetSize, selectedDatasetSize + 1 }
		// Up to 10^6 by default?
	: pair{ GetConfig().GetValue("MinDatasetSize", 2), GetConfig().GetValue("MaxDatasetSize", 6) + 1 };
}

bool IsSelected(char const* envVarName, string_view currentValue, int printMessageWithIndent)
{
	auto const selectedValueList = GetConfig().GetValue(envVarName);
	auto const selectedValues = SplitIterator{ GetConfig().GetValue(envVarName), ',' }.toArray();
	auto const isSelected = selectedValues.empty() || AnyOf(selectedValues, [&currentValue](auto const& v)
		{ return FindString<CaseInsensitiveCharTraits>(currentValue, v) >= 0; });
	if (!isSelected && printMessageWithIndent >= 0)
	{
		cout << string(printMessageWithIndent, '\t') << "Skipped " << currentValue << " (" << envVarName << " = " << selectedValueList << ")\n";
	}

	return isSelected;
}


constexpr string_view PerfRecordId = "RunEnvId/V";

PerfRecord::PerfRecord(std::string name, std::string runId)
	: name_{ std::move(name) }
	, runId_{ !runId.empty() ? runId : RUNTIME_ENVIRONMENT_ID }
	, filepath_{ GetOutputPath() / std::filesystem::path{ name_ + '_' + runId_ + string(FileExtension) } }
{
	Load();
}

void PerfRecord::Save() const
{
	if (!modified_)
	{
		return;
	}

	ofstream outfile{ filepath_.string() };

	if (!outfile)
	{
		throw runtime_error{ "Failed to open file for writing: "s + filepath_.filename().string() };
	}

	outfile << PerfRecordId << Version << Separator;
	WriteFieldNames<Entry>(outfile);
	outfile << Separator << TimeColumnName << Separator << MemoryDeltaColumnName << Separator << "Failed" << '\n';

	for (auto const& entry : entries_)
	{
		outfile << runId_ << Separator;
		WriteStruct(outfile, entry.first);
		outfile << Separator << entry.second.bestTime << Separator << entry.second.memoryDelta << Separator << (entry.second.failed ? "FAILED!" : "") << '\n';
	}
}

struct TabDelimiterCtype final : ctype<char>
{
	static mask const* make_table()
	{
		// make a copy of the "C" locale table
		static vector<mask> v{ classic_table(), classic_table() + table_size };
		v['\t'] = space;
		v[' '] = alnum;
		return v.data();
	}

	explicit TabDelimiterCtype(size_t refs = 0)
		: ctype(make_table(), false, refs)
	{
	}
};

void PerfRecord::Load()
{
	ifstream infile{ filepath_.string() };
	if (!infile)
	{
		return;
	}

	string line;
	auto version = 0;
	getline(infile, line); // header
	if (StartsWith(line, PerfRecordId))
	{
		line.erase(0, PerfRecordId.size());
		istringstream ins(line);
		ins >> version;
	}

	while (getline(infile, line))
	{
		istringstream ins(line);
		ins.imbue(locale(ins.getloc(), new TabDelimiterCtype));
		if (version > 0)
		{
			string envId;
			ins >> envId;
			if (envId != runId_)
			{
				continue;
			}
		}

		Entry entry;
		ReadStruct(ins, entry, stringStorage_);

		int64_t time = 0;
		int64_t memoryDelta = 0;
		ins >> time >> memoryDelta;
		if (!ins || entry.spatialKeyKind == SpatialKeyKind::Undefined)
		{
			break;
		}

		string failedText;
		ins >> failedText;
		auto const failed = !failedText.empty();

		entries_[entry] = { time, memoryDelta, failed };
	}
}

void PerfRecord::MergeEntry(Entry const& entry, int64_t time, int64_t memoryDelta, bool failed, std::pair<int64_t, int64_t>* accumulatedChange)
{
	auto const previousExisted = entries_.count(entry) > 0;
	auto& stats = entries_[entry];
	if (stats.failed != failed)
	{
		stats.failed = failed;
		modified_ = true;
	}

	time = max(time, int64_t(1));
	if( accumulatedChange != nullptr )
	{
		if( previousExisted && accumulatedChange->first >= 0 )
		{
			accumulatedChange->first += stats.bestTime;
			accumulatedChange->second += time;
		}
		else
		{
			accumulatedChange->first = accumulatedChange->second = -1;
		}
	}

	if (stats.bestTime > time)
	{
		stats.bestTime = time;
		modified_ = true;
	}

	memoryDelta /= Kilobyte;
	if (stats.memoryDelta != memoryDelta)
	{
		stats.memoryDelta = memoryDelta;
		modified_ = true;
	}
}

void PerfRecord::SetEntry(Entry const& entry, int64_t time, int64_t memoryDelta, bool failed)
{
	auto& stats = entries_[entry];
	stats.failed = failed;
	stats.bestTime = max(time, int64_t(1));
	stats.memoryDelta = memoryDelta / Kilobyte;
	modified_ = true;
}


template <typename T>
double ReInterpolate(T x, Interval<T> from, Interval<T> to)
{
	auto const t = (x - from.min) / (from.max - from.min);
	return LinearInterpolate(to, t);
}

Vector2 ReInterpolate(Vector2 const& p, Vector2 const& fromMin, Vector2 const& fromMax, Vector2 const& toMin, Vector2 const& toMax)
{
	return
	{
		ReInterpolate(p[0], { fromMin[0], fromMax[0] }, { toMin[0], toMax[0] }),
		ReInterpolate(p[1], { fromMin[1], fromMax[1] }, { toMin[1], toMax[1] })
	};
}


template <typename TSpatialKey>
Dataset<TSpatialKey>::Dataset(string name, vector<TSpatialKey> const& keys)
	: name_{ std::move(name) }
	, size_{ int(keys.size()) }
{
	data_.resize(keys.size());
	for (auto i = 0LL; i < Size(keys); ++i)
	{
		data_[i] = { i, keys[i] };
	}
}

template <typename TSpatialKey>
void Dataset<TSpatialKey>::SetSize(int newSize)
{
	ASSERT(newSize <= GetAvailableSize());
	if (!boundingBox_.IsEmpty())
	{
		if (newSize < size_)
		{
			boundingBox_ = {};
		}
		else
		{
			boundingBox_.Add(Bound(Iterable{ data_.begin() + size_, data_.begin() + newSize }, GetFeatureBox));
		}
	}

	size_ = newSize;
}

template <typename TSpatialKey>
auto Dataset<TSpatialKey>::GetBoundingBox() const -> BoxType
{
	if (boundingBox_.IsEmpty())
	{
		boundingBox_ = Bound(MakeIterable(data_.begin(), size_), GetFeatureBox);
	}

	return boundingBox_;
}

template <typename TSpatialKey>
void Dataset<TSpatialKey>::Clear()
{
	name_.clear();
	data_.clear();
}

template class Dataset<Vector2>;
template class Dataset<Box2>;
#if defined( ENABLE_EIGEN )
template class Dataset<EVector2>;
template class Dataset<Box<EVector2>>;
#endif


template <typename TSpatialKeyArray>
void DrawSpatialKeys(Image& image, vector<TSpatialKeyArray> const& keys, Box2 const& boundingBox)
{
	static constexpr auto MaxElements = 10000;

	auto const step = keys.size() < MaxElements ? 1.0 : double(keys.size()) / MaxElements;
	image.Fill(White);
	static constexpr auto ImageMin = Vector2{ 0, 0 };
	auto const imageMax = Vector2{ double(image.GetWidth() - 1), double(image.GetHeight() - 1) };
	for (auto i = 0.0; size_t(i) < keys.size(); i += step)
	{
		if constexpr (SpatialKeyIsPoint<TSpatialKeyArray>)
		{
			auto const point = keys[size_t(i)];
			auto const pos = ReInterpolate(point, boundingBox.Min(), boundingBox.Max(), ImageMin, imageMax);
			image.Draw(pos, Black);
		}
		else
		{
			static_assert(SpatialKeyIsBox<TSpatialKeyArray>);
			auto const minPoint = keys[size_t(i)].Min();
			auto const maxPoint = keys[size_t(i)].Max();
			auto const min = ReInterpolate(minPoint, boundingBox.Min(), boundingBox.Max(), ImageMin, imageMax);
			auto const max = ReInterpolate(maxPoint, boundingBox.Min(), boundingBox.Max(), ImageMin, imageMax);
			image.Draw(Box2{ min, max }, Black);
		}
	}
}

template void DrawSpatialKeys(Image&, vector<Vector2> const&, Box2 const&);
template void DrawSpatialKeys(Image&, vector<Box2> const&, Box2 const&);


int main( int argc, char* argv[] )
{
	for (auto i = 1; i < argc; ++i)
	{
		if (string_view{ argv[i] } == "--")
		{
			GetConfig().AddCommandLine(Span{ argv + i, argv + argc });
			argc = i;
			break;
		}
	}

	return Catch::Session().run( argc, argv );
}
