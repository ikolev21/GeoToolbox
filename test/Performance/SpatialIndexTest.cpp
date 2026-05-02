// Copyright 2024-2026 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "TestTools.hpp"
#include "GeoToolbox/GeometryTools.hpp"
#include "GeoToolbox/Image.hpp"
#include "GeoToolbox/Iterators.hpp"
#include "GeoToolbox/Profiling.hpp"
#include "GeoToolbox/ShapeFile.hpp"
#include "GeoToolbox/StlExtensions.hpp"

#include "AlgLib.hpp"
#include "Boost.hpp"
#include "Geos.hpp"
#include "NanoflannAdapter.hpp"
// ReSharper disable once CppUnusedIncludeDirective
#include "SpatialIndexWrapper.hpp"
#include "TidwallRtree.hpp"
#ifdef ENABLE_PRIVATE
#include "Private.hpp"
#endif

#include "catch2/catch_template_test_macros.hpp"
#include "catch2/internal/catch_random_number_generator.hpp"
#include "catch2/matchers/catch_matchers_floating_point.hpp"

#include <array>
#include <iostream>
#include <random>

using namespace GeoToolbox;
using namespace std;

constexpr auto OpNameQueryBox = "Query Range";
constexpr auto OpNameQueryNearest = "Query Nearest";

using SpatialKeysToTest = TypeList<
	Vector2, Box2
#if defined(ENABLE_EIGEN)
	, EVector2, Box<EVector2>
#endif
	, Vector3f, Box3f
>;


template <typename TSpatialKey, template <class> class... TIndex>
auto MakeIndicesToTest()
{
	return array{ static_cast<unique_ptr<SpatialIndexWrapper<TSpatialKey>>>(make_unique<TIndex<TSpatialKey>>())... };
}

template <typename TSpatialKey>
auto const IndicesToTest = MakeIndicesToTest<TSpatialKey
	, StdVector // Too slow, enable only to verify the results of the other participants
	, NanoflannStaticKdtree
	, GeosTemplateStrTree
	//, GeosKdTree	// Always slower than TemplateStrTree
	//, GeosQuadTree	// Always slower than TemplateStrTree
	//, GeosVertexSequencePackedRtree	// In rare cases is just a bit faster than TemplateStrTree, (much) slower in 
	, TidwallRtree
	, BoostRtree
	, AlglibKdtree	// works with double only and needs conversion from float, not implemented yet. Query times are consistently worse than all other indices
#ifdef ENABLE_PRIVATE
	, PrivateIndex
#endif
>();


namespace
{
	filesystem::path GetDataDirectory()
	{
		auto result = GetRootPath() / "data";
		return is_directory(result) ? result : filesystem::path{};
	}

	[[maybe_unused]] void PrintNearest(vector<pair<FeatureId, double>> list)
	{
		sort(list.begin(), list.end(), [](auto const& a, auto const& b) { return a.second < b.second; });
		for (auto const& feature : list)
		{
			cout << feature.first << '/' << feature.second << ' ';
		}

		cout << '\n';
	}


	string const DatasetName_Uniform = "Synthetic_Uniform";
	string const DatasetName_Skewed = "Synthetic_Skewed";
	string const DatasetName_Aspect = "Synthetic_Aspect";
	string const DatasetName_Islands = "Synthetic_Islands";
	string const DatasetName_Polygon = "Synthetic_Polygon";

	template <typename TSpatialKey>
	struct DatasetMaker
	{
		static constexpr auto DefaultRandomSeed = 13;

		using VectorType = typename SpatialKeyTraits<TSpatialKey>::VectorType;
		using ScalarType = typename SpatialKeyTraits<TSpatialKey>::ScalarType;


		ScalarType extent;
		Interval<ScalarType> heightRange;
		Box<VectorType> boundingBox;
		Catch::SimplePcg32 randomGenerator;
		uniform_real_distribution<ScalarType> distributionHeight;


		explicit DatasetMaker(ScalarType extent = 0, ScalarType maxBoxHeight = 0, int randomSeed = 0)
			: extent{ std::max(extent, ScalarType(1)) }
			, heightRange{ ScalarType(1e-7), std::max(maxBoxHeight, ScalarType(1e-6)) }
			, boundingBox{ Box<VectorType>::Square(extent) }
			, randomGenerator(randomSeed > 0 ? randomSeed : DefaultRandomSeed)
			, distributionHeight{ heightRange.min, heightRange.max }
		{
		}

		[[nodiscard]] Dataset<TSpatialKey> Make(std::string name, int datasetSize, ScalarType skewPower = 0, ScalarType averageBoxAspect = 1)
		{
			auto data = MakeRandomSpatialKeys<TSpatialKey>(randomGenerator, datasetSize, boundingBox, heightRange, skewPower, averageBoxAspect);
			return Dataset{ std::move(name), data };
		}

		[[nodiscard]] Dataset<TSpatialKey> MakeIslands(int datasetSize, ScalarType islandRadiusFactor = 0)
		{
			ASSERT(datasetSize > 0);
			islandRadiusFactor = islandRadiusFactor > 0 ? min(ScalarType(0.1), islandRadiusFactor) : ScalarType(0.01);
			auto const islandRadius = extent * islandRadiusFactor;

			constexpr auto zero = ScalarType(0);
			uniform_int_distribution distributionIslandIndex{ 0, 2 };
			array positions = { -islandRadius, -islandRadius / 2, zero, islandRadius / 2, islandRadius };
			array weights = { zero, ScalarType(0.1), ScalarType(1), ScalarType(0.1), zero };
			piecewise_linear_distribution<ScalarType> distributionOffset{ positions.begin(), positions.end(), weights.begin() };
			uniform_real_distribution<ScalarType> distributionAspect{ ScalarType(0.5), ScalarType(2) };

			vector<Feature<TSpatialKey>> data{ size_t(datasetSize) };
			array islandCenters = { islandRadius, extent / 2, extent - islandRadius };
			for (auto i = 0; i < datasetSize; ++i)
			{
				auto const island = distributionIslandIndex(randomGenerator);
				auto const islandCenter = islandCenters[island];
				auto center = VectorType{ std::clamp(islandCenter + distributionOffset(randomGenerator), zero, extent), std::clamp(islandCenter + distributionOffset(randomGenerator), zero, extent) };
				if constexpr (SpatialKeyTraits<TSpatialKey>::Dimensions == 3)
				{
					center[2] = std::clamp(islandCenter + distributionOffset(randomGenerator), zero, extent);
				}

				if constexpr (SpatialKeyIsPoint<TSpatialKey>)
				{
					data[i] = { i, center };
				}
				else
				{
					data[i] = { i, MakeRandomBox(randomGenerator, center, boundingBox, distributionHeight, distributionAspect) };
				}
			}

			return Dataset{ DatasetName_Islands, data };
		}
	};
}


template <typename TSpatialKey>
shared_ptr<Dataset<TSpatialKey>> LoadShapeFile(std::filesystem::path const& path)
{
	if (!IsSelected("Dataset", path.filename().string(), 0))
	{
		return {};
	}

	ShapeFile const shapeFile{ path.string() };
	if (!shapeFile.Supports<TSpatialKey>())
	{
		if (PrintVerboseMessages())
		{
			cout << "Skipped " << path.filename().string() << ", data doesn't match spatial key " << SpatialKeyTraits<TSpatialKey>::GetName() << '\n';
		}

		return {};
	}

	auto const sizeRange = GetDatasetSizeRange();
	if (auto const minSize = GetDatasetSizeFromOrder(sizeRange.first); shapeFile.GetObjectCount() < minSize)
	{
		if (PrintVerboseMessages())
		{
			cout << "Skipped " << path.filename().string() << " (" << shapeFile.GetObjectCount() << " < " << minSize << ")\n";
		}

		return {};
	}

	auto const maxSize = GetDatasetSizeFromOrder(sizeRange.second - 1);
	auto data = shapeFile.GetKeys<TSpatialKey>(maxSize);
	return make_shared<Dataset<TSpatialKey>>(path.filename().string(), std::move(data));
}

template <typename TSpatialKey>
shared_ptr<Dataset<TSpatialKey>> LoadObjFile(std::filesystem::path const& path)
{
	if (!IsSelected("Dataset", path.filename().string(), 0))
	{
		return {};
	}

	if constexpr (!SpatialKeyIsPoint<TSpatialKey> || SpatialKeyTraits<TSpatialKey>::Dimensions != 3)
	{
		if (PrintVerboseMessages())
		{
			cout << "Skipped " << path.filename().string() << ", data doesn't match spatial key " << SpatialKeyTraits<TSpatialKey>::GetName() << '\n';
		}

		return {};
	}
	else
	{
		vector<TSpatialKey> verts;
		using ScalarType = typename VectorTraits<TSpatialKey>::ScalarType;
		ifstream inputFile{ path };
		string line;
		while (std::getline(inputFile, line))
		{
			if (StartsWith(line, "v "))
			{
				stringstream ls{ line.substr(2) };
				ScalarType x = 0, y = 0, z = 0;
				ls >> x >> y >> z;
				if (ls)
				{
					if constexpr (VectorTraits<TSpatialKey>::Dimensions == 2)
					{
						verts.push_back({ x, y });
					}
					else
					{
						verts.push_back({ x, y, z });
					}
				}
			}
		}

		return make_shared<Dataset<TSpatialKey>>(path.filename().string(), std::move(verts));
	}
}

template <typename TSpatialKey>
struct DatasetFileIterator
{
	using iterator_category = std::forward_iterator_tag;
	using value_type = Dataset<TSpatialKey>;
	using pointer = Dataset<TSpatialKey>*;
	using reference = Dataset<TSpatialKey>&;
	using difference_type = std::ptrdiff_t;
	using const_iterator = DatasetFileIterator;
	using iterator = DatasetFileIterator;

private:

	std::filesystem::path directoryPath_;
	bool singleFile_ = true;
	bool directoryFinished_ = false;
	std::filesystem::directory_iterator directoryIterator_;
	shared_ptr<Dataset<TSpatialKey>> currentSet_;

public:

	DatasetFileIterator() = default;

	explicit DatasetFileIterator(std::filesystem::path directoryPath)
		: directoryPath_{ std::move(directoryPath) }
		, singleFile_{ is_regular_file(directoryPath_) }
		, directoryIterator_{ singleFile_ || !is_directory(directoryPath_) ? filesystem::directory_iterator{} : filesystem::directory_iterator{ directoryPath_ } }
	{
		directoryFinished_ = directoryIterator_ == filesystem::directory_iterator{};

		LoadNextFile();
	}

	DatasetFileIterator& operator++()
	{
		MoveToNextValid();
		return *this;
	}

	[[nodiscard]] Dataset<TSpatialKey>& operator*() const
	{
		ASSERT(currentSet_ != nullptr);
		return *currentSet_;
	}

	[[nodiscard]] bool operator==(DatasetFileIterator const& other) const
	{
		return currentSet_ == other.currentSet_;
	}

	[[nodiscard]] bool operator!=(DatasetFileIterator const& other) const
	{
		return !(*this == other);
	}

	[[nodiscard]] DatasetFileIterator begin() const
	{
		return *this;
	}

	[[nodiscard]] DatasetFileIterator end() const
	{
		return {};
	}

private:

	static map<string, shared_ptr<Dataset<TSpatialKey>>(*)(std::filesystem::path const&)> SupportedExtensions;

	void MoveToNextValid()
	{
		if (currentSet_ == nullptr)
		{
			return;
		}

		LoadNextFile();
	}

	void LoadNextFile()
	{
		if (singleFile_)
		{
			if (currentSet_ != nullptr || SupportedExtensions.count(directoryPath_.extension().string()) != 1)
			{
				currentSet_.reset();
				return;
			}

			auto loader = SupportedExtensions[directoryPath_.extension().string()];
			if (loader != nullptr)
			{
				currentSet_ = loader(directoryPath_);
			}

			return;
		}

		currentSet_.reset();
		if (directoryFinished_)
		{
			return;
		}

		for (; currentSet_ == nullptr; ++directoryIterator_)
		{
			if (directoryIterator_ == filesystem::directory_iterator{})
			{
				return;
			}

			auto const& path = directoryIterator_->path();
			if (!directoryIterator_->is_regular_file() || SupportedExtensions.count(path.extension().string()) != 1)
			{
				continue;
			}

			if (auto loader = SupportedExtensions[directoryIterator_->path().extension().string()])
			{
				currentSet_ = loader(path);
			}
		}
	}
};

template <typename TSpatialKey>
map<string, shared_ptr<Dataset<TSpatialKey>>(*)(std::filesystem::path const&)> DatasetFileIterator<TSpatialKey>::SupportedExtensions =
{
	{ ".shp", LoadShapeFile },
	{ ".obj", LoadObjFile },
};


static constexpr auto DatasetKey = "Dataset";


template <typename TSpatialKey>
struct DatasetPolygon : Dataset<TSpatialKey>
{
	using KeyTraits = SpatialKeyTraits<TSpatialKey>;
	using ScalarType = typename KeyTraits::ScalarType;
	using VectorType = typename KeyTraits::VectorType;
	using BoxType = typename KeyTraits::BoxType;

	static constexpr auto OuterRadius = 100;
	static constexpr auto InnerRadius = 80;

	explicit DatasetPolygon(int maxSize)
		: Dataset<TSpatialKey>{ DatasetName_Polygon, std::vector<TSpatialKey>(maxSize) }
	{
		this->onSizeChange_ = OnSizeChange;
	}

	static void TwoCircles(DatasetPolygon& dataset, int pointCount, int startIndex)
	{
		auto i = startIndex;
		if constexpr (KeyTraits::Kind == SpatialKeyKind::Box)
		{
			auto const boxSize = ScalarType(2 * Pi * InnerRadius) / ScalarType(pointCount);
			auto const output = [&dataset, boxSize, &i](VectorType const& point) { dataset.data_[i++] = { i, BoxType::FromCenterAndSize(point, boxSize) }; };
			MakeCircle<VectorType>(OutputIteratorFunction{ output }, OuterRadius, pointCount / 2);
			MakeCircle<VectorType>(OutputIteratorFunction{ output }, InnerRadius, pointCount / 2);
		}
		else
		{
			auto const output = [&dataset, &i](VectorType const& point) { dataset.data_[i++] = { i, point }; };
			MakeCircle<VectorType>(OutputIteratorFunction{ output }, OuterRadius, pointCount / 2);
			MakeCircle<VectorType>(OutputIteratorFunction{ output }, InnerRadius, pointCount / 2);
		}
	}

	static void OnSizeChange(Dataset<TSpatialKey>& datasetBase, int newSize)
	{
		auto& dataset = static_cast<DatasetPolygon&>(datasetBase);
		dataset.SetSize_(newSize);
		if constexpr (SpatialKeyTraits<TSpatialKey>::Dimensions == 2)
		{
			TwoCircles(dataset, newSize, 0);
		}
		else
		{
			TwoCircles(dataset, newSize / 2, 0);
			for (auto i = newSize / 2; i < newSize; ++i)
			{
				dataset.data_[i] = dataset.data_[i - newSize / 2];
				if constexpr (KeyTraits::Kind == SpatialKeyKind::Point)
				{
					dataset.data_[i].spatialKey[2] = OuterRadius;
				}
				else
				{
					dataset.data_[i].spatialKey.Move(VectorType{ 0, 0, OuterRadius });
				}
			}
		}
	}
};

template <typename TSpatialKey>
struct SyntheticDatasetGenerator : Generators::State<Dataset<TSpatialKey>>
{
	using ScalarType = typename SpatialKeyTraits<TSpatialKey>::ScalarType;

	int maxSize = 0;

	int Run()
	{
		using namespace Generators;

		for (;; this->Next())
		{
			switch (this->CurrentStage())
			{
			case Stage_Start:
				maxSize = GetDatasetSizeFromOrder(GetDatasetSizeRange().second - 1);
				if (IsSelected(DatasetKey, DatasetName_Uniform, 0))
				{
					DatasetMaker<TSpatialKey> maker{ 10, ScalarType(0.01) };
					return this->Next(maker.Make(DatasetName_Uniform, maxSize));
				}

				break;

			case 1:
				if (IsSelected(DatasetKey, DatasetName_Skewed, 0))
				{
					DatasetMaker<TSpatialKey> maker{ 10, ScalarType(0.001) };
					return this->Next(maker.Make(DatasetName_Skewed, maxSize, 4));
				}

				break;

			case 2:
				if (IsSelected(DatasetKey, DatasetName_Islands, 0))
				{
					DatasetMaker<TSpatialKey> maker{ 1000, ScalarType(0.01) };
					return this->Next(maker.MakeIslands(maxSize, ScalarType(0.01)));
				}

				break;

			case 3:
				if constexpr (SpatialKeyIsBox<TSpatialKey>)
				{
					if (IsSelected(DatasetKey, DatasetName_Aspect, 0))
					{
						DatasetMaker<TSpatialKey> maker{ 10, ScalarType(0.0005) };
						return this->Next(maker.Make(DatasetName_Aspect, maxSize, 0, 100));
					}
				}

				break;

			case 4:
				if (IsSelected(DatasetKey, DatasetName_Polygon, 0))
				{
					DatasetPolygon<TSpatialKey> polygon{ maxSize };
					return this->Next(std::move(polygon));
				}

				break;

				// Other synthetic datasets?

			default:
				return this->Finish();
			}
		}
	}
};


template <typename TSpatialKey>
std::string GetFilename(Dataset<TSpatialKey> const& dataset)
{
	return dataset.GetName() + "-" + string(SpatialKeyTraits<TSpatialKey>::GetName()) + "-" + to_string(dataset.GetSize());
}

template <typename TSpatialKey>
void SaveImage(Dataset<TSpatialKey> const& dataset)
{
	if constexpr (SpatialKeyTraits<TSpatialKey>::Dimensions == 2)
	{
		if (!IsSelected("StoreDatasetFormat", "png", -1, false))
		{
			return;
		}

		auto const filename = GetFilename<TSpatialKey>(dataset);
		auto const filepath = GetOutputPath() / filesystem::path{ filename + ".png" };
		if (exists(filepath))
		{
			return;
		}

		static constexpr auto ImageSize = 1024;
		auto datasetImage = Image{ ImageSize, ImageSize };
		Draw(datasetImage, dataset);
		datasetImage.Encode(filepath.string());
	}
}

template <typename TSpatialKey>
void SaveShapefile(Dataset<TSpatialKey> const& dataset)
{
	if (!IsSelected("StoreDatasetFormat", "shp", -1, false) || EndsWith<CaseInsensitiveCharTraits>(dataset.GetName(), ".shp"))
	{
		return;
	}

	auto const filename = GetFilename<TSpatialKey>(dataset);
	auto const filepath = GetOutputPath() / filesystem::path{ filename + ".shp" };
	if (exists(filepath))
	{
		return;
	}

	ShapeFile::Write(filepath, dataset.GetKeys());
}

template <typename TSpatialKey>
void SaveObj(Dataset<TSpatialKey> const& dataset)
{
	if (!IsSelected("StoreDatasetFormat", "obj", -1, false) || EndsWith<CaseInsensitiveCharTraits>(dataset.GetName(), ".obj"))
	{
		return;
	}

	auto const filename = GetFilename<TSpatialKey>(dataset);
	auto const filepath = GetOutputPath() / filesystem::path{ filename + ".obj" };
	if (exists(filepath))
	{
		return;
	}

	using ScalarType = typename SpatialKeyTraits<TSpatialKey>::ScalarType;
	using BoxType = typename SpatialKeyTraits<TSpatialKey>::BoxType;

	std::ofstream file(filepath);
	if (!file)
	{
		return;
	}

	auto writeVertex = [&file](Vector3f const& v)
		{
			file << "v  " << v[0] << ' ' << v[1] << ' ' << v[2] << '\n';
		};

	auto const& keys = dataset.GetKeys();
	for (auto const& key : keys)
	{
		BoxType box;
		if constexpr (SpatialKeyIsPoint<TSpatialKey>)
		{
			box = BoxType::FromCenterAndSize(key, ScalarType(0.01));
		}
		else
		{
			box = key;
		}

		auto const& a = Convert<Vector3f>(box.Min());
		auto const& b = Convert<Vector3f>(box.Max());
		writeVertex({ a[0], a[1], b[2] });
		writeVertex(a);
		writeVertex({ b[0], a[1], a[2] });
		writeVertex({ b[0], a[1], b[2] });
		writeVertex({ a[0], b[1], b[2] });
		writeVertex(b);
		writeVertex({ b[0], b[1], a[2] });
		writeVertex({ a[0], b[1], a[2] });
	}

	file << '\n';
	for (auto startIndex = 0LL, keyIndex = 0LL; keyIndex < Size(keys); ++keyIndex, startIndex += 8)
	{
		file << "f " << startIndex + 1 << ' ' << startIndex + 2 << ' ' << startIndex + 3 << ' ' << startIndex + 4 << '\n';
		file << "f " << startIndex + 5 << ' ' << startIndex + 6 << ' ' << startIndex + 7 << ' ' << startIndex + 8 << '\n';
		file << "f " << startIndex + 1 << ' ' << startIndex + 4 << ' ' << startIndex + 6 << ' ' << startIndex + 5 << '\n';
		file << "f " << startIndex + 4 << ' ' << startIndex + 3 << ' ' << startIndex + 7 << ' ' << startIndex + 6 << '\n';
		file << "f " << startIndex + 3 << ' ' << startIndex + 2 << ' ' << startIndex + 8 << ' ' << startIndex + 7 << '\n';
		file << "f " << startIndex + 2 << ' ' << startIndex + 1 << ' ' << startIndex + 5 << ' ' << startIndex + 8 << '\n';
	}
}


struct TestContextBase
{
	Timings timings{ 2 * Timings::MsPerSecond };

	PerfRecord* perfRecord;

	string indexStats;

	bool const resetResults = GetConfig().Get<bool>("Reset");
};


static constexpr auto QueriesPerAxis = 21;
constexpr auto QueryNearestCount = 15;

template <typename TSpatialKey>
struct TestContext : TestContextBase
{
	static constexpr auto Dimensions = SpatialKeyTraits<TSpatialKey>::Dimensions;
	using ScalarType = typename SpatialKeyTraits<TSpatialKey>::ScalarType;
	using VectorType = typename SpatialKeyTraits<TSpatialKey>::VectorType;
	using BoxType = typename SpatialKeyTraits<TSpatialKey>::BoxType;


	Dataset<TSpatialKey> const* dataset;

	vector<BoxType> queries;
	vector<double> queryResults;


	explicit TestContext(Dataset<TSpatialKey> const& dataset, PerfRecord& record)
		: dataset(&dataset)
	{
		perfRecord = &record;

		if constexpr (StartsWith(SpatialKeyTraits<TSpatialKey>::VectorTraitsType::Name, "array") && SpatialKeyTraits<TSpatialKey>::Dimensions == 2)
		{
			SaveImage(dataset);
			SaveObj(dataset);
			if constexpr (SpatialKeyIsPoint<TSpatialKey>)
			{
				if (StartsWith(dataset.GetName(), "Synthetic"))
				{
					SaveShapefile(dataset);
				}
			}
		}

		auto const querySize = 2 * dataset.GetSmallestExtent() / std::max(1, QueriesPerAxis - 1);
		ASSERT(querySize > 0);
		QueryIterator firstQuery{ GetLowBound(dataset.GetData().back().spatialKey), dataset.GetBoundingBox(), QueriesPerAxis, { querySize / 16, querySize / 2, querySize } };
		std::copy(firstQuery, QueryIterator<VectorType>{}, back_inserter(queries));
		//ASSERT(queryCount == QueriesPerAxis * QueriesPerAxis * 2);

		queryResults.reserve(queries.size());

		cout << dataset.GetName() << '\t' << dataset.GetSize() << '\n';
	}

	static constexpr auto Tolerance = 0.1;

	bool VerifyQueryResults(vector<double>&& results, string_view spatialIndexName)
	{
		if (queryResults.empty())
		{
			queryResults = std::move(results);
		}
		else
		{
			for (auto i = 0; i < Size(results); ++i)
			{
				if (abs(results[i] - queryResults[i]) > Tolerance)
				{
					cout << SetColorRed << std::fixed << "\t\t\tFAILED query index " << i << " for spatial index " << spatialIndexName
						<< ", expected result " << queryResults[i] << ", got " << results[i] << ResetColor << '\n';
					return false;
				}
			}
		}

		return true;
	}

	void ResetQueryVerifier()
	{
		queryResults.clear();
	}

	// Returns the change factor compared to the previous best time
	double StoreResults(string_view testName, string_view spatialIndexName)
	{
		pair<int64_t, int64_t> accumulatedOldAndNewBestTimes{};

		for (auto const& action : timings.GetAllActions())
		{
			auto entry = this->perfRecord->MakeEntry(*dataset, spatialIndexName, testName, action.first);
			PerfRecord::Stats stats{ int64_t(action.second.bestTime), action.second.memoryDelta/* == std::numeric_limits<int64_t>::max() ? 0 : action.second.memoryDelta*/, action.second.failed };
			if (auto const queryStats = static_cast<QueryStats*>(action.second.extra.get()))
			{
				stats.queryScalarComparisons = queryStats->ScalarComparisonsCount;
				stats.queryBoxOverlaps = queryStats->BoxOverlapsCount;
				stats.queryVisitedNodes = queryStats->VisitedNodesCount;
				stats.queryObjectTests = queryStats->ObjectTestsCount;
			}

			if (resetResults)
			{
				this->perfRecord->SetEntry(entry, stats);
			}
			else
			{
				this->perfRecord->MergeEntry(entry, stats, &accumulatedOldAndNewBestTimes);
			}
		}

		if (!timings.GetAllActions().empty())
		{
			auto entry = this->perfRecord->MakeEntry(*dataset, spatialIndexName, testName, "Total");
			PerfRecord::Stats stats{ int64_t(timings.BestIterationTime()) };
			stats.info = indexStats;
			if (resetResults)
			{
				this->perfRecord->SetEntry(entry, stats);
			}
			else
			{
				this->perfRecord->MergeEntry(entry, stats);
			}
		}

		return accumulatedOldAndNewBestTimes.second > 0 ? double(accumulatedOldAndNewBestTimes.second) * 100.0 / double(accumulatedOldAndNewBestTimes.first) : -1;
	}
};

template <typename TSpatialKey>
struct TestScenario
{
	virtual ~TestScenario() = default;

	[[nodiscard]] virtual std::string_view Name() const = 0;

	// Return -1 if the scenario is not supported, or the number of failures is supported
	[[nodiscard]] virtual int Run(TestContext<TSpatialKey>&, SpatialIndexWrapper<TSpatialKey> const&) const = 0;
};


template <typename TSpatialKey>
struct Test_Load_Query_Destroy : TestScenario<TSpatialKey>
{
	using BoxType = typename SpatialKeyTraits<TSpatialKey>::BoxType;
	using VectorType = typename SpatialKeyTraits<TSpatialKey>::VectorType;

	[[nodiscard]] virtual char const* GetOpName() const = 0;

	[[nodiscard]] int Run(TestContext<TSpatialKey>& test, SpatialIndexWrapper<TSpatialKey> const& wrapper) const override
	{
		if (RunQuery(wrapper, wrapper.Load(Dataset<TSpatialKey>{}), BoxType{ VectorType{0} }) < 0)
		{
			if (PrintVerboseMessages())
			{
				cout << "\t\t" << "Skipped " << wrapper.Name() << " (does not support " << GetOpName() << ")\n";
			}

			return -1;
		}

		if (!wrapper.SupportsDatasetSize(test.dataset->GetSize()))
		{
			if (PrintVerboseMessages())
			{
				cout << "\t\t" << "Skipped " << wrapper.Name() << " (does not support datasets of size " << test.dataset->GetSize() << ")\n";
			}

			return -1;
		}

		Timings::ActionStats* statsQuery = nullptr;

		auto statsStored = false;

		vector<double> queryResults;
		queryResults.reserve(test.queries.size());

		while (test.timings.NextIteration())
		{
			auto spatialIndex = test.timings.Record(
				"Bulk Load",
				[&]
				{
					return wrapper.Load(*test.dataset);
				});

			if (spatialIndex == nullptr)
			{
				return -1;
			}

			TheQueryStats.Clear();

			test.timings.Record(
				GetOpName(),
				[&]
				{
					auto queryIndex = 0;
					for (auto const& query : test.queries)
					{
						auto const result = RunQuery(wrapper, spatialIndex, query);
						if (queryIndex > Size(queryResults))
						{
							queryResults.push_back(result);
						}

						++queryIndex;
					}
				},
				&statsQuery);

			if (!statsStored)
			{
				statsStored = true;
				test.indexStats = wrapper.GetIndexStats(spatialIndex);
			}

			statsQuery->extra = make_shared<QueryStats>(TheQueryStats);
			TheQueryStats.Clear();

			test.timings.Record("Destroy", [&spatialIndex]
				{
					[[maybe_unused]] auto toKill = std::move(spatialIndex);
					return 0;
				});
		}

		return test.VerifyQueryResults(std::move(queryResults), wrapper.Name()) ? 0 : 1;
	}

	[[nodiscard]] virtual double RunQuery(SpatialIndexWrapper<TSpatialKey> const& wrapper, std::shared_ptr<void> const& spatialIndex, BoxType const& query) const = 0;
};

template <typename TSpatialKey>
struct Test_Load_QueryBox_Destroy final : Test_Load_Query_Destroy<TSpatialKey>
{
	using BoxType = typename SpatialKeyTraits<TSpatialKey>::BoxType;

	[[nodiscard]] std::string_view Name() const override
	{
		return "Load-QueryRange-Destroy";
	}

	[[nodiscard]] char const* GetOpName() const override
	{
		return OpNameQueryBox;
	}

	[[nodiscard]] double RunQuery(SpatialIndexWrapper<TSpatialKey> const& wrapper, std::shared_ptr<void> const& spatialIndex, BoxType const& query) const override
	{
		return wrapper.QueryBox(spatialIndex, query);
	}
};

template <typename TSpatialKey>
struct Test_Load_QueryNearest_Destroy final : Test_Load_Query_Destroy<TSpatialKey>
{
	using BoxType = typename SpatialKeyTraits<TSpatialKey>::BoxType;

	[[nodiscard]] std::string_view Name() const override
	{
		return "Load-QueryNearest-Destroy";
	}

	[[nodiscard]] char const* GetOpName() const override
	{
		return OpNameQueryNearest;
	}

	[[nodiscard]] double RunQuery(SpatialIndexWrapper<TSpatialKey> const& wrapper, std::shared_ptr<void> const& spatialIndex, BoxType const& query) const override
	{
		return wrapper.QueryNearest(spatialIndex, query.Center(), QueryNearestCount);
	}
};

template <typename TSpatialKey>
struct Test_Insert_Erase_Query : TestScenario<TSpatialKey>
{
	[[nodiscard]] std::string_view Name() const override
	{
		return "Insert-Erase-Query";
	}

	[[nodiscard]] int Run(TestContext<TSpatialKey>& test, SpatialIndexWrapper<TSpatialKey> const& wrapper) const override
	{
		if (!wrapper.IsDynamic())
		{
			if (PrintVerboseMessages())
			{
				cout << "\t\t" << "Skipped " << wrapper.Name() << " (does not support removal)" << '\n';
			}

			return -1;
		}

		if (!wrapper.SupportsDatasetSize(test.dataset->GetSize()))
		{
			if (PrintVerboseMessages())
			{
				cout << "\t\t" << "Skipped " << wrapper.Name() << " (does not support datasets of size " << test.dataset->GetSize() << ")\n";
			}

			return -1;
		}

		if constexpr (SpatialKeyTraits<TSpatialKey>::Dimensions != 2)
		{
			if (PrintVerboseMessages())
			{
				cout << "\t\t" << "Skipped " << wrapper.Name() << " (too slow in 3 dimensions)" << '\n';
			}

			return -1;
		}
		else
		{
			return Run_(test, wrapper);
		}
	}

	static int Run_(TestContext<TSpatialKey>& test, SpatialIndexWrapper<TSpatialKey> const& wrapper)
	{
		Timings::ActionStats* statsQueryBox = nullptr;

		auto const& dataset = *test.dataset;

		vector<double> queryResults;
		queryResults.reserve(test.queries.size());

		auto statsStored = false;

		while (test.timings.NextIteration())
		{
			auto spatialIndex = wrapper.MakeEmptyIndex();

			test.timings.Record(
				"Insert",
				[&]
				{
					for (auto const& feature : dataset.GetData())
					{
						wrapper.Insert(spatialIndex, &feature);
					}
				});

			if (!wrapper.Erase(spatialIndex, &dataset.GetData()[0]))
			{
				cout << "\t\t" << wrapper.Name() << '\t' << "skipped, removing failed\n";
				return -1;
			}

			// Remove some elements, both to measure erasing speed and disbalance the index
			test.timings.Record(
				"Erase",
				[&]
				{
					auto const dataSetSize = dataset.GetSize();
					for (auto i = 0; i < dataSetSize; i += 5)
					{
						wrapper.Erase(spatialIndex, &dataset.GetData()[i]);
					}
				});

			// Return the erased elements back, both to make query results comparable to the other tests and to disbalance the index even more
			test.timings.Record(
				"Reinsert",
				[&]
				{
					auto const dataSetSize = dataset.GetSize();
					for (auto i = 0; i < dataSetSize; i += 5)
					{
						wrapper.Insert(spatialIndex, &dataset.GetData()[i]);
					}
				});

			// Then give indices that need re-balancing a chance to do it
			test.timings.Record(
				"Rebalance",
				[&]
				{
					wrapper.Rebalance(spatialIndex);
				});

			TheQueryStats.Clear();
			test.timings.Record(
				OpNameQueryBox,
				[&]
				{
					auto queryIndex = 0;
					for (auto const& query : test.queries)
					{
						auto const result = wrapper.QueryBox(spatialIndex, query);
						if (queryIndex > Size(queryResults))
						{
							queryResults.push_back(result);
						}

						++queryIndex;
					}
				}, &statsQueryBox);
			statsQueryBox->extra = make_shared<QueryStats>(TheQueryStats);
			TheQueryStats.Clear();

			if (!statsStored)
			{
				statsStored = true;
				test.indexStats = wrapper.GetIndexStats(spatialIndex);
			}
		}

		return test.VerifyQueryResults(std::move(queryResults), wrapper.Name()) ? 0 : 1;
	}
};

template <typename TSpatialKey>
int RunSpatialIndex(TestContext<TSpatialKey>& testContext, TestScenario<TSpatialKey> const& scenario, SpatialIndexWrapper<TSpatialKey> const& wrapper)
{
	if (wrapper.Name().empty() || !IsSelected("Index", wrapper.Name(), 2))
	{
		return 0;
	}

	testContext.timings.Reset();
	auto failuresCount = 0;

	auto const failures = scenario.Run(testContext, wrapper);
	if (failures >= 0)
	{
		auto const changeFactor = testContext.StoreResults(scenario.Name(), wrapper.Name());

		cout << "\t\t" << wrapper.Name();
		if (changeFactor > 0)
		{
			cout << '\t' << std::setprecision(1) << std::fixed;
			if (changeFactor >= 100)
			{
				cout << '+' << changeFactor - 100;
			}
			else
			{
				cout << '-' << 100 - changeFactor;
			}

			cout << '%';
		}

		if (!testContext.indexStats.empty())
		{
			cout << '\t' << testContext.indexStats;
		}

		cout << '\n';
		failuresCount = failures;
	}

	return failuresCount;
}

template <typename TSpatialKey>
int RunScenario(TestContext<TSpatialKey>& testContext, TestScenario<TSpatialKey> const& scenario)
{
	if (!IsSelected("Scenario", scenario.Name(), 1))
	{
		return 0;
	}

	cout << '\t' << scenario.Name() << '\n';
	auto failuresCount = 0;

	testContext.ResetQueryVerifier();

	for (auto const& wrapper : IndicesToTest<TSpatialKey>)
	{
		failuresCount += RunSpatialIndex(testContext, scenario, *wrapper);
	}

	return failuresCount;
}

template <typename TSpatialKey>
int RunSpatialKey(PerfRecord& perfRecord)
{
	using SpatialKeyType = TSpatialKey;

	auto const printHeader = []
		{
			cout << "\n--- " << SpatialKeyTraits<SpatialKeyType>::GetName() << '\n';
		};

	if (PrintVerboseMessages())
	{
		printHeader();
	}

	if (!IsSelected("SpatialKey", SpatialKeyTraits<SpatialKeyType>::GetName(), 0)
		|| !IsSelected("Dimensions", std::to_string(SpatialKeyTraits<SpatialKeyType>::Dimensions), 0)
		|| !IsSelected("Vector", SpatialKeyTraits<SpatialKeyType>::VectorTraitsType::Name, 0))
	{
		return 0;
	}

	if (!PrintVerboseMessages())
	{
		printHeader();
	}

	auto const sizeRange = GetDatasetSizeRange();
	auto totalFailures = 0;

	Generators::Generator<SyntheticDatasetGenerator<SpatialKeyType>> synthetic{};
	auto files = DatasetFileIterator<SpatialKeyType>(GetDataDirectory());
	for (auto& dataset : Concat(synthetic, files))
	{
		for (auto sizeOrder = sizeRange.first; sizeOrder < sizeRange.second; ++sizeOrder)
		{
			auto const size = GetDatasetSizeFromOrder(sizeOrder);
			if (size > dataset.GetAvailableSize())
			{
				break;
			}

			dataset.SetSize(size);

			TestContext testContext{ dataset, perfRecord };

			totalFailures += RunScenario<SpatialKeyType>(testContext, Test_Load_QueryBox_Destroy<SpatialKeyType>{});

			totalFailures += RunScenario<SpatialKeyType>(testContext, Test_Load_QueryNearest_Destroy<SpatialKeyType>{});

			totalFailures += RunScenario<SpatialKeyType>(testContext, Test_Insert_Erase_Query<SpatialKeyType>{});

			if (GetConfig().Get<bool>("Record"))
			{
				testContext.perfRecord->Save();
			}
		}
	}

	return totalFailures;
}

int CompareSpatialIndices(PerfRecord& perfRecord)
{
	WarnInDebugBuild();

	if (!GetConfig().Get<bool>("Record"))
	{
		std::cout << SetColorRed << "\nWARNING! Results will NOT be recorded ('Record' configuration key set to OFF)\n" << ResetColor;
	}

	cout << "RunId: " << perfRecord.GetRunId() << '\n';

	auto totalFailures = 0;

	Stopwatch const timer;

	TypeListForEach<SpatialKeysToTest>([&totalFailures, &perfRecord]([[maybe_unused]] auto spatialKey)
		{
			totalFailures += RunSpatialKey<decltype(spatialKey)>(perfRecord);
		});

	cout << "\nTotal running time (s): " << (timer.ElapsedMilliseconds() + 900) / 1000 << '\n';

	return totalFailures;
}


TEST_CASE("adhoc", "[.]")
{
	vector<Vector3f> verts;

	ifstream inputFile(R"(C:\Users\ikolev\Documents\maya\projects\default\scenes\hair.obj)");
	string line;
	while (std::getline(inputFile, line))
	{
		if (StartsWith(line, "v "))
		{
			stringstream ls{ line.substr(2) };
			float x = 0, y = 0, z = 0;
			ls >> x >> y >> z;
			if (ls)
			{
				verts.push_back({ x, y, z });
			}
		}
	}

	//auto const result = thinks::ReadObj(ifs, add_position, add_face);
}

TEST_CASE("CompareSpatialIndices", "[.Performance]")
{
	if (GetRootPath().empty())
	{
		SKIP("Run from a directory under the project root");
	}

	create_directory(GetOutputPath());

	if (GetDataDirectory().empty())
	{
		cout << "\n'data' directory not found at root, no datasets will be loaded\n";
	}

	auto const configFilePath = GetOutputPath() / (GetCatchTestName() + ".cfg");
	if (is_regular_file(configFilePath))
	{
		GetConfig().ReadFile(configFilePath, false);
	}

	PerfRecord perfRecord{ GetCatchTestName() };

	REQUIRE(CompareSpatialIndices(perfRecord) == 0);
}


// This is not a test, just a tool to concatenate all .tsv files in the current directory into a single one
TEST_CASE("ConcatResults", "[.Performance]")
{
	auto const outfilename = "All"s;
	auto const outfilePath = outfilename + ".tsv"s;
	ofstream outfile{ outfilePath };
	auto first = true;
	for (filesystem::directory_iterator iter{ "." }; iter != filesystem::directory_iterator{}; ++iter)
	{
		auto const filepath = iter->path().string();
		if (!EndsWith(filepath, ".tsv") || iter->path().filename() == outfilePath)
		{
			continue;
		}

		cout << "Adding " << filepath << '\n';
		ifstream infile{ filepath };
		string line;
		std::getline(infile, line);
		if (first)
		{
			first = false;
			outfile << line << '\n';
		}

		while (std::getline(infile, line))
		{
			outfile << line << '\n';
		}
	}
}
