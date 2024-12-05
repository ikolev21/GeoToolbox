#include "TestTools.hpp"
#include "GeoToolbox/GeometryTools.hpp"
#include "GeoToolbox/Image.hpp"
#include "GeoToolbox/Profiling.hpp"
#include "GeoToolbox/ShapeFile.hpp"
#include "GeoToolbox/StlExtensions.hpp"

#include "Boost.hpp"
#include "Geos.hpp"
#include "NanoflannAdaptor.hpp"
#include "SpatialIndexWrapper.hpp"
#include "Spatialpp.hpp"

#include "catch2/catch_template_test_macros.hpp"
#include "catch2/internal/catch_random_number_generator.hpp"
#include "catch2/matchers/catch_matchers_floating_point.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <random>

using namespace GeoToolbox;
using namespace std;

constexpr auto Epsilon = 1e-8;

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

	template <typename TSpatialKey>
	Dataset<TSpatialKey> MakeUniformDataSet(int datasetSize, double extent = 0, [[maybe_unused]] double maxSize = 0, int randomSeed = 0)
	{
		static_assert(SpatialKeyTraits<TSpatialKey>::Dimensions == 2);

		static constexpr auto RandomSeed = 13;

		ASSERT(datasetSize > 0);
		extent = max(extent, 1.0);
		maxSize = max(maxSize, 0.001);
		using VectorType = typename SpatialKeyTraits<TSpatialKey>::VectorType;
		auto const boundingBox = Box<VectorType>{ { 0, 0 }, { extent, extent } };
		Catch::SimplePcg32 randomGenerator(randomSeed > 0 ? randomSeed : RandomSeed);
		uniform_real_distribution distributionPosition(0.0, extent);
		[[maybe_unused]] uniform_real_distribution distributionSize(0.0, maxSize);
		vector<Feature<TSpatialKey>> data{ size_t(datasetSize) };
		for (auto i = 0; i < datasetSize; ++i)
		{
			auto const center = VectorType{ distributionPosition(randomGenerator), distributionPosition(randomGenerator) };
			if constexpr (SpatialKeyIsPoint<TSpatialKey>)
			{
				data[i] = { i, center };
				// To get the same positions for points and boxes
				(void)distributionSize(randomGenerator);
			}
			else
			{
				static_assert(SpatialKeyIsBox<TSpatialKey>, "Not implemented for this type");
				auto const halfSize = distributionSize(randomGenerator) / 2;
				auto const v = VectorType{ halfSize, halfSize };
				auto const box = Box<VectorType>{ center - v, center + v };
				data[i] = { i, Intersect(box, boundingBox) };
			}
		}

		return Dataset{ DatasetName_Uniform, data };
	}
}


template <typename TSpatialKey>
struct DatasetIterator
{
	using iterator_category = std::forward_iterator_tag;
	using value_type = Dataset<TSpatialKey>;
	using pointer = Dataset<TSpatialKey> const*;
	using reference = Dataset<TSpatialKey> const&;
	using difference_type = std::ptrdiff_t;
	using const_iterator = DatasetIterator;
	using iterator = DatasetIterator;

	DatasetIterator() = default;

	explicit DatasetIterator(std::filesystem::path directoryPath)
		: sizeRange_{ GetDatasetSizeRange() }
		, directoryPath_{ std::move(directoryPath) }
		, singleFile_{ is_regular_file(directoryPath_) }
		, directoryIterator_{ singleFile_ || !is_directory(directoryPath_) ? filesystem::directory_iterator{} : filesystem::directory_iterator{ directoryPath_ } }
	{
		directoryFinished_ = directoryIterator_ == filesystem::directory_iterator{};

		// TODO: more synthetic datasets

		if (IsSelected("Dataset", DatasetName_Uniform, 0))
		{
			currentSet_ = make_shared<Dataset<TSpatialKey>>(MakeUniformDataSet<TSpatialKey>(GetDatasetSizeFromOrder(sizeRange_.second - 1), 10, 0.1));
			datasetSizeOrder_ = sizeRange_.first;
			currentSet_->SetSize(GetDatasetSizeFromOrder(datasetSizeOrder_));
		}
		else
		{
			LoadNextFile();
		}
	}

	DatasetIterator& operator++()
	{
		MoveToNextValid();
		return *this;
	}

	[[nodiscard]] Dataset<TSpatialKey> const& operator*() const
	{
		ASSERT(currentSet_ != nullptr);
		return *currentSet_;
	}

	[[nodiscard]] bool operator==(DatasetIterator const& other) const
	{
		return currentSet_ == other.currentSet_;
	}

	[[nodiscard]] bool operator!=(DatasetIterator const& other) const
	{
		return !(*this == other);
	}

	[[nodiscard]] DatasetIterator begin() const
	{
		return *this;
	}

	[[nodiscard]] DatasetIterator end() const
	{
		return {};
	}

private:

	static constexpr std::string_view ShapeFileExtension = ".shp";

	void MoveToNextValid()
	{
		if (currentSet_ == nullptr)
		{
			return;
		}

		++datasetSizeOrder_;
		if (datasetSizeOrder_ < sizeRange_.second)
		{
			auto const size = GetDatasetSizeFromOrder(datasetSizeOrder_);
			if (size <= currentSet_->GetAvailableSize())
			{
				currentSet_->SetSize(size);
				return;
			}
		}

		LoadNextFile();
	}

	void LoadNextFile()
	{
		if (singleFile_)
		{
			if (currentSet_ != nullptr || directoryPath_.extension() != ShapeFileExtension)
			{
				currentSet_.reset();
				return;
			}

			LoadShapeFile(directoryPath_.string());
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
			if (!directoryIterator_->is_regular_file() || path.extension() != ShapeFileExtension)
			{
				continue;
			}

			LoadShapeFile(path.string());
		}
	}

	void LoadShapeFile(std::filesystem::path const& path)
	{
		if (!IsSelected("Dataset", path.filename().string(), 0))
		{
			return;
		}

		ShapeFile const shapeFile{ path.string() };
		if (!shapeFile.Supports<TSpatialKey>())
		{
			cout << "Skipping " << path.filename().string() << ", data doesn't match spatial key " << SpatialKeyTraits<TSpatialKey>::GetName() << '\n';
			return;
		}

		datasetSizeOrder_ = sizeRange_.first;
		auto const size = GetDatasetSizeFromOrder(datasetSizeOrder_);
		if (shapeFile.GetObjectCount() < size)
		{
			cout << "Skipping " << path.filename().string() << " (" << shapeFile.GetObjectCount() << " < " << size << ")\n";
			return;
		}

		auto const maxSize = GetDatasetSizeFromOrder(sizeRange_.second - 1);
		auto data = shapeFile.GetKeys<TSpatialKey>(maxSize);
		currentSet_ = make_shared<Dataset<TSpatialKey>>(path.filename().string(), std::move(data));
		currentSet_->SetSize(size);
	}


	pair<int, int> sizeRange_;
	std::filesystem::path directoryPath_;
	bool singleFile_ = true;
	bool directoryFinished_ = false;
	std::filesystem::directory_iterator directoryIterator_;
	int datasetSizeOrder_ = 0;
	shared_ptr<Dataset<TSpatialKey>> currentSet_;
};


template <typename T>
struct IsSpatialIndex
{
	static constexpr auto value = !std::is_void_v<typename T::IndexType>;
};

template <typename TSpatialKey>
using IndicesToTest = MakeFilteredTypeList<IsSpatialIndex,
	StdVector<TSpatialKey>,
	StdHashset<TSpatialKey>,
	BoostRtree<TSpatialKey>,
	GeosTemplateStrTree<TSpatialKey>,
	GeosQuadTree<TSpatialKey>,
	NanoflannStaticKdtree<TSpatialKey>,
	SpatialppKdtree<TSpatialKey>>;


struct ResultVerifier
{
	static constexpr auto MaxResultCount = 5;

	ResultVerifier()
	{
		Reset();
	}

	int Check(double result, int index, Timings::ActionStats* stats = nullptr)
	{
		if (result < 0)
		{
			return 0;
		}

		if (checkResults[index] < 0)
		{
			checkResults[index] = result;
			return 0;
		}

		//REQUIRE_THAT(results[i], Catch::Matchers::WithinAbs(checkResults[i], Epsilon));
		if (abs(result - checkResults[index]) <= Epsilon)
		{
			return 0;
		}

		cout << "FAILED: " << result << "\texpected: " << checkResults[index] << '\n';
		if (stats != nullptr)
		{
			stats->failed = true;
		}

		return 1;
	}

	void Reset()
	{
		std::fill_n(checkResults.begin(), MaxResultCount, -1.0);
	}


	array<double, MaxResultCount> checkResults{};
};

template <typename TSpatialKey>
void SaveImage(Dataset<TSpatialKey> const& dataset)
{
	static constexpr auto ImageSize = 1024;
	auto datasetImage = Image(ImageSize, ImageSize);
	Draw(datasetImage, dataset);
	auto const filename = dataset.GetName() + "-" + string(SpatialKeyTraits<TSpatialKey>::GetName()) + char('0' + SpatialKeyTraits<TSpatialKey>::Dimensions) + "-" + to_string(dataset.GetSize());
	auto const filepath = GetOutputPath() / filesystem::path{ filename + ".png" };
	if (!exists(filepath))
	{
		datasetImage.Encode(filepath.string());
	}
}

struct TestContextBase
{
	Timings timings{ 2 * Timings::UsPerSecond };

	PerfRecord perfRecord{ GetCatchTestName() };

	shared_ptr<atomic<int64_t>> allocatorStats = make_shared<atomic<int64_t>>();

	ResultVerifier verifier;

	bool const resetResults = GetConfig().GetValue("Reset", 0) == 1;
};

template <typename TSpatialKey>
struct TestContext : TestContextBase
{
	static constexpr auto Dimensions = SpatialKeyTraits<TSpatialKey>::Dimensions;
	using ScalarType = typename SpatialKeyTraits<TSpatialKey>::ScalarType;
	using VectorType = typename SpatialKeyTraits<TSpatialKey>::VectorType;
	using BoxType = typename SpatialKeyTraits<TSpatialKey>::BoxType;


	Dataset<TSpatialKey> const* dataset;

	vector<BoxType> boxQueriesSmall = GenerateBoxQueries(*dataset, 65536);
	vector<BoxType> boxQueriesBig = GenerateBoxQueries(*dataset, 64);


	explicit TestContext(Dataset<TSpatialKey> const& dataset)
		: dataset(&dataset)
	{
		if constexpr (SpatialKeyTraits<TSpatialKey>::VectorTraitsType::Name == VectorTraits<Vector2>::Name)
		{
			SaveImage(dataset);
		}

		cout << dataset.GetName() << '\t' << dataset.GetSize() << '\n';
	}

	static vector<BoxType> GenerateBoxQueries(Dataset<TSpatialKey> const& dataset, double factor)
	{
		static constexpr auto QueriesCount = 8;

		auto const& extent = dataset.GetBoundingBox();
		auto const sizes = extent.Sizes();
		auto const windowSize = Flat<VectorType>(MinimumValue(sizes) / factor);
		vector<BoxType> queries;

		// To guarantee we find at least one element, put the last element in the dataset in the query list
		if constexpr (SpatialKeyIsPoint<TSpatialKey>)
		{
			auto const corner = dataset.GetData().back().spatialKey;
			queries.emplace_back(corner, corner + windowSize);
		}
		else
		{
			static_assert(SpatialKeyIsBox<TSpatialKey>);
			queries.emplace_back(dataset.GetData().back().spatialKey);
		}

		for (auto i = 0; i < QueriesCount; ++i)
		{
			VectorType pointOnDiagonal{};
			auto const center = extent.Center();
			for (auto dim = 0; dim < int(Dimensions); ++dim)
			{
				auto corner = center;
				corner[dim] = extent.Min()[dim] + (i + 0.5) * extent.Size(dim) / QueriesCount;
				pointOnDiagonal[dim] = corner[dim];
				queries.emplace_back(corner, corner + windowSize);
			}

			queries.emplace_back(pointOnDiagonal, pointOnDiagonal + windowSize);
		}

		return queries;
	}


	void StoreResults(string_view testName, string_view spatialIndexName, bool allSupported)
	{
		for (auto const& action : timings.GetAllActions())
		{
			auto entry = perfRecord.MakeEntry(*dataset, spatialIndexName, testName, action.first);
			if (resetResults)
			{
				perfRecord.SetEntry(
					entry,
					int64_t(action.second.bestTime),
					action.second.memoryDelta,
					action.second.failed
				);
			}
			else
			{
				perfRecord.MergeEntry(
					entry,
					int64_t(action.second.bestTime),
					action.second.memoryDelta,
					action.second.failed
				);
			}
		}

		if (allSupported)
		{
			auto entry = perfRecord.MakeEntry(*dataset, spatialIndexName, testName, "Total");
			if (resetResults)
			{
				perfRecord.SetEntry(
					entry,
					int64_t(timings.BestIterationTime())
				);
			}
			else
			{
				perfRecord.MergeEntry(
					entry,
					int64_t(timings.BestIterationTime())
				);
			}
		}
	}
};

template <typename TSpatialKey>
struct Test_Load_Query_Destroy
{
	static constexpr string_view Name = "Load-Query-Destroy";

	template <class TIndex>
	static pair<int, bool> Run(TestContext<TSpatialKey>& test, TIndex const&)
	{
		static_assert(std::is_move_constructible_v<TIndex>);

		auto allSupported = true;
		auto queryBoxSmallResult = -1.0;
		auto queryBoxBigResult = -1.0;
		auto queryNearestResult3 = -1.0;
		auto queryNearestResult50 = -1.0;
		Timings::ActionStats* statsQueryBoxSmall = nullptr;
		Timings::ActionStats* statsQueryBoxBig = nullptr;
		Timings::ActionStats* statsQueryNearest3 = nullptr;
		Timings::ActionStats* statsQueryNearest50 = nullptr;

		while (test.timings.NextIteration())
		{
			*test.allocatorStats = 0;
			auto index = test.timings.Record(
				"Bulk Load",
				test.allocatorStats,
				[&test]
				{
					return TIndex::Load(*test.dataset, test.allocatorStats);
				});

			if (TIndex::QueryBox(index, test.boxQueriesSmall.front()) < 0)
			{
				// Box query not supported
				queryBoxSmallResult = queryBoxBigResult = -1;
				allSupported = false;
			}
			else
			{
				queryBoxSmallResult = test.timings.Record(
					"Query Box Small",
					[&test, &index]
					{
						auto total = 0;
						for (auto const& query : test.boxQueriesSmall)
						{
							total += TIndex::QueryBox(index, query);
						}

						return total;
					}, &statsQueryBoxSmall);

				queryBoxBigResult = test.timings.Record(
					"Query Box Big",
					[&test, &index]
					{
						auto total = 0;
						for (auto const& query : test.boxQueriesBig)
						{
							total += TIndex::QueryBox(index, query);
						}

						return total;
					}, &statsQueryBoxSmall);
			}

			if (TIndex::QueryNearest(index, test.boxQueriesSmall.front().Min(), 3) < 0)
			{
				// Nearest query not supported
				queryNearestResult3 = queryNearestResult50 = -1;
				allSupported = false;
			}
			else
			{
				auto runQuery = [&test, &index](int nearestCount, Timings::ActionStats*& statsQueryNearest)
					{
						return test.timings.Record(
							"Query Nearest " + to_string(nearestCount),
							[&test, &index, nearestCount]
							{

								auto nearestResult = 0.0;
								for (auto const& query : test.boxQueriesSmall)
								{
									nearestResult += TIndex::QueryNearest(index, query.Min(), nearestCount);
								}

								return nearestResult;
							}, &statsQueryNearest);
					};
				queryNearestResult3 = runQuery(3, statsQueryNearest3);
				queryNearestResult50 = runQuery(50, statsQueryNearest50);
			}

			test.timings.Record("Destroy", test.allocatorStats, [&index]
				{
					[[maybe_unused]] auto toKill = std::move(index);
					return 0;
				});
		}

		auto const failureCount =
			test.verifier.Check(queryBoxSmallResult, 0, statsQueryBoxSmall)
			+ test.verifier.Check(queryBoxBigResult, 1, statsQueryBoxBig)
			+ test.verifier.Check(queryNearestResult3, 2, statsQueryNearest3)
			+ test.verifier.Check(queryNearestResult50, 3, statsQueryNearest50);
		return { failureCount, allSupported };
	}
};

template <typename TSpatialKey>
struct Test_Insert_Erase_Query
{
	static constexpr string_view Name = "Insert-Erase-Query";

	template <class TIndex>
	static pair<int, bool> Run(TestContext<TSpatialKey>& test, TIndex const& spatialIndex)
	{
		if constexpr (TIndex::IsDynamic)
		{
			return Run_(test, spatialIndex);
		}
		else
		{
			cout << "\t\t" << "Skipping " << spatialIndex.Name << " (does not support removal)" << '\n';
			return { -1, false };
		}
	}

	template <class TIndex>
	static pair<int, bool> Run_(TestContext<TSpatialKey>& test, TIndex const&)
	{
		auto queryBoxSmallResult = -1.0;
		auto queryBoxBigResult = -1.0;
		Timings::ActionStats* statsQueryBoxSmall = nullptr;
		Timings::ActionStats* statsQueryBoxBig = nullptr;

		while (test.timings.NextIteration())
		{
			*test.allocatorStats = 0;
			auto index = TIndex::MakeEmptyIndex(test.allocatorStats);

			test.timings.Record(
				"Insert",
				test.allocatorStats,
				[&test, &index]
				{
					for (auto const& feature : test.dataset->GetData())
					{
						TIndex::Insert(index, &feature);
					}
				});

			if (!TIndex::Erase(index, &test.dataset->GetData()[0]))
			{
				//cout << '\t' << spatialIndex.Name << '\t' << "skipped, does not support removing items\n";
				return { 0, false };
			}

			// Remove some elements, both to measure erasing speed and disbalance the index
			test.timings.Record(
				"Erase",
				test.allocatorStats,
				[&test, &index]
				{
					auto const dataSetSize = test.dataset->GetSize();
					for (auto i = 0; i < dataSetSize; i += 5)
					{
						TIndex::Erase(index, &test.dataset->GetData()[i]);
					}
				});

			// Return the erased elements back, both to make query results comparable to the other tests and to disbalance the index even more
			// Then give indices that support re-balancing a chance to do it
			test.timings.Record(
				"Reinsert",
				test.allocatorStats,
				[&test, &index]
				{
					auto const dataSetSize = test.dataset->GetSize();
					for (auto i = 0; i < dataSetSize; i += 5)
					{
						TIndex::Insert(index, &test.dataset->GetData()[i]);
					}

					TIndex::Rebalance(index);
				});

			queryBoxSmallResult = test.timings.Record(
				"Query Box Small",
				[&test, &index]
				{
					auto total = 0;
					for (auto const& query : test.boxQueriesSmall)
					{
						total += TIndex::QueryBox(index, query);
					}

					return total;
				}, &statsQueryBoxSmall);

			queryBoxBigResult = test.timings.Record(
				"Query Box Big",
				[&test, &index]
				{
					auto total = 0;
					for (auto const& query : test.boxQueriesBig)
					{
						total += TIndex::QueryBox(index, query);
					}

					return total;
				}, &statsQueryBoxBig);
		}

		auto const failureCount =
			test.verifier.Check(queryBoxSmallResult, 0, statsQueryBoxSmall)
			+ test.verifier.Check(queryBoxBigResult, 1, statsQueryBoxBig);
		return { failureCount, true };
	}
};

template <typename TSpatialKey, template <typename> class TestToRun>
int RunTest(TestContext<TSpatialKey>& test)
{
	if (!IsSelected("Scenario", TestToRun<TSpatialKey>::Name, 1))
	{
		return 0;
	}

	cout << '\t' << TestToRun<TSpatialKey>::Name << '\n';
	auto failuresCount = 0;

	TypeListForEach<IndicesToTest<TSpatialKey>>([&test, &failuresCount](auto const& index)
		{
			if (!IsSelected("Index", index.Name, 2))
			{
				return;
			}

			test.timings.Reset();
			test.verifier.Reset();

			auto const [failures, allSupported] = TestToRun<TSpatialKey>::Run(test, index);
			if (failures >= 0)
			{
				cout << "\t\t" << test.timings.BestIterationTime() << '\t' << index.Name << '\n';
				failuresCount += failures;
			}

			test.StoreResults(TestToRun<TSpatialKey>::Name, index.Name, allSupported);
		});

	return failuresCount;
}

constexpr char const* SpatialIndexCompareTestName = "CompareSpatialIndices";

TEST_CASE(SpatialIndexCompareTestName, "[.Performance]")
{
	if (GetRootPath().empty())
	{
		SKIP("Run from a directory under the project root");
	}

	create_directory(GetOutputPath());

	if (GetDataDirectory().empty())
	{
		cout << '\n' << "'data' directory not found at root, no datasets will be loaded\n";
	}

	auto const configFilePath = GetOutputPath() / (string(SpatialIndexCompareTestName) + ".cfg");
	if (is_regular_file(configFilePath))
	{
		GetConfig().ReadFile(configFilePath, false);
	}

	auto totalFailures = 0;

	TypeListForEach<SpatialKeyTypes>([&totalFailures]([[maybe_unused]] auto spatialKey)
		{
			using SpatialKeyType = decltype(spatialKey);

			cout << "\n--- " << SpatialKeyTraits<SpatialKeyType>::GetName() << '\n';

			if (!IsSelected("SpatialKey", SpatialKeyTraits<SpatialKeyType>::GetName(), 0))
			{
				return;
			}

			if (!IsSelected("Vector", SpatialKeyTraits<SpatialKeyType>::VectorTraitsType::Name, 0))
			{
				return;
			}

			for (auto const& dataset : DatasetIterator<SpatialKeyType>(GetDataDirectory()))
			{
				TestContext test{ dataset };

				totalFailures += RunTest<SpatialKeyType, Test_Load_Query_Destroy>(test);

				totalFailures += RunTest<SpatialKeyType, Test_Insert_Erase_Query>(test);

				test.perfRecord.Save();
			}
		});

	REQUIRE(totalFailures == 0);
}
