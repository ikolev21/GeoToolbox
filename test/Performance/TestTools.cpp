// Copyright 2024-2026 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "TestTools.hpp"

#include "GeoToolbox/Iterators.hpp"

#include "catch2/catch_session.hpp"
#include "catch2/catch_template_test_macros.hpp"

#include <array>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

using namespace GeoToolbox;
using namespace std;
using namespace std::string_view_literals;


static constexpr string_view FileExtension = ".tsv";

static constexpr auto Separator = '\t';


Config& GetConfig()
{
	static Config theConfig;
	return theConfig;
}

bool PrintVerboseMessages()
{
	static bool value = GetConfig().GetBool("verbose", false);  // NOLINT(misc-const-correctness)
	return value;
}


filesystem::path GetRootPath()
{
	static auto const rootPath = []
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
	static auto const outputPath = []
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
	auto const selectedDatasetSize = GetConfig().Get<int>("DatasetSize");

	return selectedDatasetSize > 0
		? pair{ selectedDatasetSize, selectedDatasetSize + 1 }
		// Up to 10^6 by default?
	: pair{ GetConfig().Get<int>("MinDatasetSize"), GetConfig().Get<int>("MaxDatasetSize") + 1 };
}

bool IsSelected(char const* configKey, string_view testValue, int printMessageWithIndent, bool selectedByDefault)
{
	auto const selectedValueList = GetConfig().Get<string>(configKey);
	auto const selectedValues = SplitIterator{ selectedValueList, ',' }.toArray(true);
	auto const isSelected = selectedByDefault && selectedValues.empty() || AnyOf(selectedValues, [&testValue](auto const& v)
		{ return FindString<CaseInsensitiveCharTraits>(testValue, v) >= 0; });
	if (!isSelected && printMessageWithIndent >= 0 && PrintVerboseMessages())
	{
		cout << string(printMessageWithIndent, '\t') << "Skipped " << testValue << " (" << configKey << " = " << selectedValueList << ")\n";
	}

	return isSelected;
}


constexpr string_view PerfRecordId = "RunEnvId/V";

PerfRecord::PerfRecord(std::string name, std::string runId, std::string fileId)
	: name_{ std::move(name) }
	, fileId_{ !fileId.empty() ? std::move(fileId) : GetConfig().Get<string>("FileId") }
	, runId_{ !runId.empty() ? std::move(runId) : GetConfig().GetString("RunId", fileId_) }
	, filepath_{ GetOutputPath() / std::filesystem::path{ name_ + (!fileId_.empty() ? '_' + fileId_ : "") + string(FileExtension) } }
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

	if (!prefixLines_.empty())
	{
		for (auto const& line : prefixLines_)
		{
			outfile << line << '\n';
		}
	}

	outfile << PerfRecordId << Version << Separator;
	WriteFieldNames<Entry>(outfile);
	outfile << Separator;
	WriteFieldNames<Stats>(outfile);
	outfile << '\n';

	for (auto const& entry : entries_)
	{
		outfile << runId_ << Separator;
		WriteStruct(outfile, entry.first);
		outfile << Separator;
		WriteStruct(outfile, entry.second);
		outfile << '\n';
	}

	for (auto const& line : otherIdLines_)
	{
		outfile << line << '\n';
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
	auto headerFound = false;
	while (getline(infile, line))
	{
		if (StartsWith(line, PerfRecordId))
		{
			headerFound = true;
			break;
		}

		prefixLines_.emplace_back(std::move(line));
	}

	if (!headerFound)
	{
		return;
	}

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
			string runId;
			ins >> runId;
			if (runId != runId_)
			{
				otherIdLines_.emplace_back(std::move(line));
				continue;
			}
		}

		Entry entry;
		ReadStruct(ins, entry, stringStorage_);

		Stats stats;
		ReadStruct(ins, stats);

		entries_[entry] = stats;
	}
}

void PerfRecord::MergeEntry(Entry const& entry, Stats const& newStats, std::pair<int64_t, int64_t>* accumulatedOldAndNewBestTimes)
{
	auto const previousExisted = entries_.count(entry) > 0;
	auto& stats = entries_[entry];
	auto const oldBestTime = stats.bestTime;
	if (stats != newStats)
	{
		stats = newStats;
		modified_ = true;
		if (newStats.bestTime > oldBestTime)
		{
			stats.bestTime = oldBestTime;
		}
	}

	if (accumulatedOldAndNewBestTimes != nullptr)
	{
		if (previousExisted && accumulatedOldAndNewBestTimes->first >= 0)
		{
			accumulatedOldAndNewBestTimes->first += oldBestTime;
			if (newStats.bestTime > 0)
			{
				accumulatedOldAndNewBestTimes->second += newStats.bestTime;
			}
		}
		else
		{
			accumulatedOldAndNewBestTimes->first = accumulatedOldAndNewBestTimes->second = -1;
		}
	}
}

void PerfRecord::SetEntry(Entry const& entry, Stats const& newStats)
{
	entries_[entry] = newStats;
	modified_ = true;
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
void Dataset<TSpatialKey>::SetSize_(int newSize)
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
template class Dataset<Vector3f>;
template class Dataset<Box2>;
template class Dataset<Box3f>;
#if defined( ENABLE_EIGEN )
template class Dataset<EVector2>;
template class Dataset<Box<EVector2>>;
#endif


int main(int argc, char* argv[])
{
	try
	{
		GetConfig().RegisterKeys(
			{
				{ "RunId", "", "An identifier of the test run. Default value equals FileId" },
				{ "FileId", RUNTIME_ENVIRONMENT_ID, "A string to include in the name of the test results file, may be empty. Default: {def}" },
				{ "Reset", false, "Resets the stored test results. Default: {def}" },
				{ "Record", SelectDebugRelease(false, true), "Should the test results be recorded to a file. Default is 1 in optimized builds, 0 in debug build" },
				{ "Index", "", "Comma-separated list of indices to run tests for, these could be partial names, comparison is case-insensitive, for example: Index=nano,geos" },
				{ "Dataset", "", "Comma-separated list of datasets to run tests for, these could be partial names, comparison is case-insensitive, for example: Dataset=synthetic,parcels" },
				{ "DatasetSize", -1, "Fixes the order of the dataset size to use (i.e. 1 means 10 element, 6 means 1 million elements). If not set, all orders from MinDatasetSize to MaxDatasetSize will be used" },
				{ "MinDatasetSize", 2, "Minimum order of the size of the dataset, tests are executed for all orders from MinDatasetSize to MaxDatasetSize. Default: {def}" },
				{ "MaxDatasetSize", 6, "Maximum order of the size of the dataset, tests are executed for all orders from MinDatasetSize to MaxDatasetSize. Default: {def}" },
				{ "Scenario", "", "Comma-separated list of scenarios to run (partial case-insensitive match), one of: Load-QueryBox-Destroy, Load-QueryNearest-Destroy, Insert-Erase-Query" },
				{ "SpatialKey", "", "Comma-separated list of spatial keys to run the tests for, possible ones are 'point' and 'box'" },
				{ "Vector", "", "Comma-separated list of vector types to run the tests for (if compiled), like 'array2d' or 'array3f'" },
				{ "Dimensions", "", "Comma-separated list of dimensions to run the tests for" },
				{ "StoreDatasetFormat", "", "Comma-separated list of formats to store the dataset used for each test, either PNG, SHP or OBJ" },
			}
			);

		if (argc == 1)
		{
			std::cout
				<< "No tests are configured to run by default, please specify the name of a perf.test you'd like to run, like CompareSpatialIndices\n\n"
				<< "Configuration keys for CompareSpatialIndices:\n"
				<< "(either save these to " << (GetOutputPath() / "CompareSpatialIndices.cfg").generic_string() << " or pass them as -key=value on the command line after the Catch arguments and --)\n"
				<< GetConfig().GenerateDefaultConfigFile();
			return 0;
		}

		for (auto i = 1; i < argc; ++i)
		{
			if (string_view{ argv[i] } == "--")
			{
				GetConfig().AddCommandLine(Span{ argv + i, argv + argc });
				argc = i;
				break;
			}
		}

		Catch::Session session;

		auto result = session.applyCommandLine(argc, argv);
		if (result != 0)
		{
			return result;
		}

		auto const repeats = GetConfig().GetInt("repeats", 1);
		auto const startTime = std::chrono::steady_clock::now();

		for (auto i = 0; i < repeats; ++i)
		{
			if (i > 0)
			{
				static auto DelayBetweenRuns = std::chrono::seconds( GetConfig().GetInt("delay", 10) );
				std::cout << "Test run " << i << " starting in " << DelayBetweenRuns.count() << "s... (press Ctrl-C to stop execution)\n";
				std::this_thread::sleep_for(DelayBetweenRuns);
			}

			result = session.run();
			if (result != 0)
			{
				break;
			}
		}

		std::cout << "Time elapsed: " << PrintMilliSeconds(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startTime).count()) << '\n';
		return 0;
	}
	catch (std::exception const& exc)
	{
		std::puts(exc.what());
		std::puts("\n");
		return 1;
	}
	//catch (...)
	//{
	//	std::puts("Unknown unhandled exception\n");
	//	return 1;
	//}
}
