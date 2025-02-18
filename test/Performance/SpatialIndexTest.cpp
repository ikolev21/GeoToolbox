// Copyright 2024-2025 Ivan Kolev
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

#include "Boost.hpp"
#include "Geos.hpp"
#include "NanoflannAdapter.hpp"
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
	string const DatasetName_Skewed = "Synthetic_Skewed";
	string const DatasetName_Aspect = "Synthetic_Aspect";
	string const DatasetName_Islands = "Synthetic_Islands";

	template <typename TSpatialKey>
	struct DatasetMaker
	{
		static_assert(SpatialKeyTraits<TSpatialKey>::Dimensions == 2);

		static constexpr auto DefaultRandomSeed = 13;

		using VectorType = typename SpatialKeyTraits<TSpatialKey>::VectorType;
		using ScalarType = typename SpatialKeyTraits<TSpatialKey>::ScalarType;


		double extent;
		double maxBoxHeight;
		Box<VectorType> boundingBox;
		Catch::SimplePcg32 randomGenerator;
		uniform_real_distribution<ScalarType> distributionHeight;


		DatasetMaker(ScalarType extent = 0, ScalarType maxBoxHeight = 0, int randomSeed = 0)
			: extent{ std::max(extent, 1.0) }
			, maxBoxHeight{ std::max(maxBoxHeight, 0.000001) }
			, boundingBox{ { 0, 0 }, { extent, extent } }
			, randomGenerator(randomSeed > 0 ? randomSeed : DefaultRandomSeed)
			, distributionHeight{ 0.0, maxBoxHeight }
		{
		}

		[[nodiscard]] Dataset<TSpatialKey> Make(std::string name, int datasetSize, ScalarType skewPower = 0, ScalarType averageBoxAspect = 1)
		{
			ASSERT(datasetSize > 0);
			ASSERT(averageBoxAspect >= 1);

			uniform_real_distribution distributionPosition{ 0.0, 1.0 };
			uniform_real_distribution distributionAspect{ averageBoxAspect / 2, averageBoxAspect * 2 };

			vector<Feature<TSpatialKey>> data{ size_t(datasetSize) };
			for (auto i = 0; i < datasetSize; ++i)
			{
				auto y = distributionPosition(randomGenerator);
				if (skewPower > 1)
				{
					y = pow(y, skewPower);
				}

				auto const center = extent * VectorType{ distributionPosition(randomGenerator), y };
				if constexpr (SpatialKeyIsPoint<TSpatialKey>)
				{
					data[i] = { i, center };
				}
				else
				{
					data[i] = { i, MakeBox(center, distributionAspect) };
				}
			}

			return Dataset{ std::move(name), data };
		}

		[[nodiscard]] Dataset<TSpatialKey> MakeIslands(int datasetSize, ScalarType islandRadiusFactor = 0)
		{
			ASSERT(datasetSize > 0);
			islandRadiusFactor = islandRadiusFactor > 0 ? min(0.1, islandRadiusFactor) : 0.01;
			auto const islandRadius = extent * islandRadiusFactor;

			uniform_int_distribution distributionIslandIndex{ 0, 2 };
			array positions = { -islandRadius, -islandRadius / 2, 0.0, islandRadius / 2, islandRadius };
			array weights = { 0.0, 0.1, 1.0, 0.1, 0.0 };
			piecewise_linear_distribution<> distributionOffset{ positions.begin(), positions.end(), weights.begin() };
			uniform_real_distribution distributionAspect{ 0.5, 2.0 };

			vector<Feature<TSpatialKey>> data{ size_t(datasetSize) };
			array islandCenters = { islandRadius, extent / 2, extent - islandRadius };
			for (auto i = 0; i < datasetSize; ++i)
			{
				auto const island = distributionIslandIndex(randomGenerator);
				auto const islandCenter = islandCenters[island];
				auto const center = VectorType{ std::clamp(islandCenter + distributionOffset(randomGenerator), 0.0, extent), std::clamp(islandCenter + distributionOffset(randomGenerator), 0.0, extent) };
				if constexpr (SpatialKeyIsPoint<TSpatialKey>)
				{
					data[i] = { i, center };
				}
				else
				{
					data[i] = { i, MakeBox(center, distributionAspect) };
				}
			}

			return Dataset{ DatasetName_Islands, data };
		}

		[[nodiscard]] Box<VectorType> MakeBox(VectorType const& center, uniform_real_distribution<ScalarType>& distributionAspect)
		{
			static_assert(SpatialKeyIsBox<TSpatialKey>, "Not implemented for this type");
			auto const halfHeight = distributionHeight(randomGenerator) / 2;
			auto const halfWidth = halfHeight * distributionAspect(randomGenerator);
			auto const v = VectorType{ halfWidth, halfHeight };
			return Intersect(Box<VectorType>{ center - v, center + v }, boundingBox);
		}
	};
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

	static constexpr std::string_view ShapeFileExtension = ".shp";

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
			cout << "Skipped " << path.filename().string() << ", data doesn't match spatial key " << SpatialKeyTraits<TSpatialKey>::GetName() << '\n';
			return;
		}

		auto const sizeRange = GetDatasetSizeRange();
		if (auto const minSize = GetDatasetSizeFromOrder(sizeRange.first); shapeFile.GetObjectCount() < minSize)
		{
			cout << "Skipped " << path.filename().string() << " (" << shapeFile.GetObjectCount() << " < " << minSize << ")\n";
			return;
		}

		auto const maxSize = GetDatasetSizeFromOrder(sizeRange.second - 1);
		auto data = shapeFile.GetKeys<TSpatialKey>(maxSize);
		currentSet_ = make_shared<Dataset<TSpatialKey>>(path.filename().string(), std::move(data));
	}
};


static constexpr auto DatasetKey = "Dataset";


template <typename TSpatialKey>
struct SyntheticDatasetGenerator : Generators::State<Dataset<TSpatialKey>>
{
	int maxSize = 0;


	int Run()
	{
		using namespace Generators;

		for(;; this->Next())
		{
			switch (this->CurrentStage())
			{
			case Stage_Start:
				maxSize = GetDatasetSizeFromOrder(GetDatasetSizeRange().second - 1);
				if (IsSelected(DatasetKey, DatasetName_Uniform, 0))
				{
					DatasetMaker<TSpatialKey> maker{ 10, 0.01 };
					return this->Next(maker.Make(DatasetName_Uniform, maxSize));
				}

				break;

			case 1:
				if (IsSelected(DatasetKey, DatasetName_Skewed, 0))
				{
					DatasetMaker<TSpatialKey> maker{ 10, 0.001 };
					return this->Next(maker.Make(DatasetName_Skewed, maxSize, 4));
				}

				break;

			case 2:
				if (IsSelected(DatasetKey, DatasetName_Islands, 0))
				{
					DatasetMaker<TSpatialKey> maker{ 1000, 0.01 };
					return this->Next(maker.MakeIslands(maxSize, 0.01));
				}

				break;

			case 3:
				if constexpr (SpatialKeyIsBox<TSpatialKey>)
				{
					if (IsSelected(DatasetKey, DatasetName_Aspect, 0))
					{
						DatasetMaker<TSpatialKey> maker{ 10, 0.0005 };
						return this->Next(maker.Make(DatasetName_Aspect, maxSize, 0, 100));
					}

					break;
				}

				[[fallthrough]];

				// TODO: more synthetic datasets

			default:
				return this->Finish();
			}
		}
	}
};


template <typename T>
struct IsSpatialIndex
{
	static constexpr auto value = !std::is_void_v<typename T::IndexType>;
};

template <typename TSpatialKey>
using IndicesToTest = MakeFilteredTypeList<IsSpatialIndex,
	StdVector<TSpatialKey>,
	//StdHashset<TSpatialKey>,
	//EmhashSet8<TSpatialKey>,
	BoostRtree<TSpatialKey>,
	GeosTemplateStrTree<TSpatialKey>,
	GeosQuadTree<TSpatialKey>,
	NanoflannStaticKdtree<TSpatialKey>,
	SpatialppKdtree<TSpatialKey>
>;


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
std::string GetFilename(Dataset<TSpatialKey> const& dataset)
{
	return dataset.GetName() + "-" + string(SpatialKeyTraits<TSpatialKey>::GetName()) + char('0' + SpatialKeyTraits<TSpatialKey>::Dimensions) + "-" + to_string(dataset.GetSize());
}

template <typename TSpatialKey>
void SaveImage(Dataset<TSpatialKey> const& dataset)
{
	auto const filename = GetFilename<TSpatialKey>(dataset);
	auto const filepath = GetOutputPath() / filesystem::path{ filename + ".png" };
	if (exists(filepath))
	{
		return;
	}

	static constexpr auto ImageSize = 1024;
	auto datasetImage = Image(ImageSize, ImageSize);
	Draw(datasetImage, dataset);
	datasetImage.Encode(filepath.string());
}

template <typename TSpatialKey>
void SaveShapefile(Dataset<TSpatialKey> const& dataset)
{
	auto const filename = GetFilename<TSpatialKey>(dataset);
	ShapeFile::Write(GetOutputPath() / filesystem::path{ filename + ".shp" }, dataset.GetKeys());
}

struct TestContextBase
{
	Timings timings{ 2 * Timings::UsPerSecond };

	PerfRecord perfRecord{ GetCatchTestName(), GetConfig().GetValue("RunId", "") };

	shared_ptr<atomic<int64_t>> allocatorStats = make_shared<atomic<int64_t>>();

	ResultVerifier verifier;

	bool const resetResults = GetConfig().GetValue("Reset", 0) == 1;
};


static constexpr auto QueriesPerAxis = 66;

template <class TVector>
class QueryIterator
{
	using ScalarType = typename VectorTraits<TVector>::ScalarType;
	static constexpr auto Dimensions = int(VectorTraits<TVector>::Dimensions);
	static constexpr auto Epsilon = 1e-8;


	std::array<int, Dimensions> index_{ -1 };

	int samplesPerAxis_ = 10;

	Box<TVector> datasetBounds_;

	TVector datasetSizes_;

	ScalarType size_;

	Box<TVector> box_;

public:

	using value_type = Box<TVector>;
	using pointer = value_type const*;
	using reference = value_type const&;
	using iterator_category = std::forward_iterator_tag;
	using difference_type = ptrdiff_t;

	QueryIterator() = default;

	QueryIterator(TVector datasetSample, Box<TVector> const& datasetBounds, int samplesPerAxis, ScalarType size)
		: samplesPerAxis_{ samplesPerAxis }
		, datasetBounds_{ datasetBounds }
		, datasetSizes_{ datasetBounds_.Sizes() }
		, size_{ size }
		// Start with a query that coincides with an element of the dataset, to test this scenario, and to guarantee we always find at least one element
		, box_{ Box<TVector>::FromMinAndSize(datasetSample, size_) }
	{
		index_[0] = 0;
		index_[Dimensions - 1] = -1;

	}

	[[nodiscard]] Iterable<QueryIterator> MakeRange() const
	{
		return { *this, QueryIterator{} };
	}

	[[nodiscard]] QueryIterator SetSize(ScalarType newSize) const
	{
		auto result = *this;
		result.size_ = newSize;
		return result;
	}

	reference operator* () const
	{
		return box_;
	}

	pointer operator-> () const noexcept
	{
		return &box_;
	}

	QueryIterator& operator++()
	{
		if (index_[0] >= 0)
		{
			for (auto dim = Dimensions - 1; dim >= 0; --dim)
			{
				++index_[dim];
				if (index_[dim] < samplesPerAxis_)
				{
					for (auto rest = dim + 1; rest < Dimensions; ++rest)
					{
						index_[rest] = 0;
					}

					break;
				}

				if (dim == 0)
				{
					index_ = { -1 };
					box_ = {};
					return *this;
				}
			}
		}

		TVector corner{};
		for (auto dim = 0; dim < Dimensions; ++dim)
		{
			corner[dim] = datasetBounds_.Min()[dim] + index_[dim] * datasetSizes_[dim] / (samplesPerAxis_ - 2);
		}

		box_ = Box<TVector>::FromMinAndSize(corner, size_);
		return *this;
	}

	[[nodiscard]] friend bool operator==(QueryIterator const& a, QueryIterator const& b) noexcept
	{
		return a.index_[0] < 0 && b.index_[0] < 0 || a.index_ == b.index_;
	}

	[[nodiscard]] friend bool operator!=(QueryIterator const& a, QueryIterator const& b) noexcept
	{
		return !(a == b);
	}
};


template <typename TSpatialKey>
struct TestContext : TestContextBase
{
	static constexpr auto Dimensions = SpatialKeyTraits<TSpatialKey>::Dimensions;
	using ScalarType = typename SpatialKeyTraits<TSpatialKey>::ScalarType;
	using VectorType = typename SpatialKeyTraits<TSpatialKey>::VectorType;
	using BoxType = typename SpatialKeyTraits<TSpatialKey>::BoxType;


	Dataset<TSpatialKey> const* dataset;


	explicit TestContext(Dataset<TSpatialKey> const& dataset)
		: dataset(&dataset)
	{
		if constexpr (SpatialKeyTraits<TSpatialKey>::VectorTraitsType::Name == VectorTraits<Vector2>::Name)
		{
			SaveImage(dataset);
			if (StartsWith(dataset.GetName(), "Synthetic"))
			{
				SaveShapefile(dataset);
			}
		}

		cout << dataset.GetName() << '\t' << dataset.GetSize() << '\n';
	}

	double StoreResults(string_view testName, string_view spatialIndexName, bool allSupported)
	{
		pair<int64_t, int64_t> change{};

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
					action.second.failed,
					&change
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

		return change.second > 0 ? change.second * 100.0 / change.first : -1;
	}
};

constexpr auto QueryNearestCount = 15;

template <typename TSpatialKey>
struct Test_Load_Query_Destroy
{
	static constexpr string_view Name = "Load-Query-Destroy";

	template <class TIndex>
	static pair<int, bool> Run(TestContext<TSpatialKey>& test, TIndex const&)
	{
		static_assert(std::is_move_constructible_v<TIndex>);

		auto allSupported = true;

		auto queryBoxResult = -1.0;
		Timings::ActionStats* statsQueryBox = nullptr;

		auto queryNearestResult = -1.0;
		Timings::ActionStats* statsQueryNearest = nullptr;

		auto const& dataset = *test.dataset;

		QueryIterator firstQuery{ GetLowBound(dataset.GetData().back().spatialKey), dataset.GetBoundingBox(), QueriesPerAxis, dataset.GetSmallestExtent() / (QueriesPerAxis - 2) };

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

			if (TIndex::QueryBox(index, *firstQuery) < 0)
			{
				// Box query not supported
				queryBoxResult = -1;
				allSupported = false;
			}
			else
			{
				queryBoxResult = test.timings.Record(
					"Query Box",
					[&firstQuery, &index]
					{
						auto totalFound = 0;
						auto queryCount = 0;
						//auto emptyCount = 0;
						TheQueryStats.Clear();
						for (auto const& query : firstQuery.MakeRange())
						{
							auto const found = TIndex::QueryBox(index, query);
							totalFound += found;
							++queryCount;
							//if (found == 0)
							//{
							//	++emptyCount;
							//}
						}

						ASSERT(queryCount = QueriesPerAxis * QueriesPerAxis * 2);
						//ASSERT(emptyCount < 3 * queryCount / 4);
						return totalFound;
					}, &statsQueryBox);
			}

			if (TIndex::QueryNearest(index, firstQuery->Min(), 3) < 0)
			{
				// Nearest query not supported
				queryNearestResult = -1;
				allSupported = false;
			}
			else
			{
				queryNearestResult = test.timings.Record(
					"Query Nearest",
					[&firstQuery, &index]
					{
						auto nearestResult = 0.0;
						for (auto const& query : firstQuery.MakeRange())
						{
							nearestResult += TIndex::QueryNearest(index, query.Min(), QueryNearestCount);
						}

						return nearestResult;
					}, &statsQueryNearest);
			}

			test.timings.Record("Destroy", test.allocatorStats, [&index]
				{
					[[maybe_unused]] auto toKill = std::move(index);
					return 0;
				});
		}

		ASSERT(queryBoxResult > 0);

		auto const failureCount =
			test.verifier.Check(queryBoxResult, 0, statsQueryBox)
			+ test.verifier.Check(queryNearestResult, 1, statsQueryNearest);
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
			cout << "\t\t" << "Skipped " << spatialIndex.Name << " (does not support removal)" << '\n';
			return { -1, false };
		}
	}

	template <class TIndex>
	static pair<int, bool> Run_(TestContext<TSpatialKey>& test, TIndex const& spatialIndex)
	{
		auto queryBoxResult = -1.0;
		Timings::ActionStats* statsQueryBox = nullptr;

		auto const& dataset = *test.dataset;

		QueryIterator firstQuery{ GetLowBound(dataset.GetData().back().spatialKey), dataset.GetBoundingBox(), QueriesPerAxis, dataset.GetSmallestExtent() / (QueriesPerAxis - 2) };

		while (test.timings.NextIteration())
		{
			*test.allocatorStats = 0;
			auto index = TIndex::MakeEmptyIndex(test.allocatorStats);

			test.timings.Record(
				"Insert",
				test.allocatorStats,
				[&dataset, &index]
				{
					for (auto const& feature : dataset.GetData())
					{
						TIndex::Insert(index, &feature);
					}
				});

			if (!TIndex::Erase(index, &dataset.GetData()[0]))
			{
				cout << "\t\t" << spatialIndex.Name << '\t' << "skipped, removing failed\n";
				return { -1, false };
			}

			// Remove some elements, both to measure erasing speed and disbalance the index
			test.timings.Record(
				"Erase",
				test.allocatorStats,
				[&dataset, &index]
				{
					auto const dataSetSize = dataset.GetSize();
					for (auto i = 0; i < dataSetSize; i += 5)
					{
						TIndex::Erase(index, &dataset.GetData()[i]);
					}
				});

			// Return the erased elements back, both to make query results comparable to the other tests and to disbalance the index even more
			test.timings.Record(
				"Reinsert",
				test.allocatorStats,
				[&dataset, &index]
				{
					auto const dataSetSize = dataset.GetSize();
					for (auto i = 0; i < dataSetSize; i += 5)
					{
						TIndex::Insert(index, &dataset.GetData()[i]);
					}
				});

			// Then give indices that need re-balancing a chance to do it
			test.timings.Record(
				"Rebalance",
				test.allocatorStats,
				[&index]
				{
					TIndex::Rebalance(index);
				});

			queryBoxResult = test.timings.Record(
				"Query Box",
				[&firstQuery, &index]
				{
					auto total = 0;
					for (auto const& query : firstQuery.MakeRange())
					{
						total += TIndex::QueryBox(index, query);
					}

					return total;
				}, &statsQueryBox);
		}

		ASSERT(queryBoxResult > 0);

		auto const failureCount = test.verifier.Check(queryBoxResult, 0, statsQueryBox);
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
			auto const changeFactor = test.StoreResults(TestToRun<TSpatialKey>::Name, index.Name, allSupported);
			if (failures >= 0)
			{
				cout << "\t\t" << test.timings.BestIterationTime() << '\t' << index.Name;
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

				if (!TheQueryStats.IsEmpty())
				{
					cout << "\tComparisons: " << TheQueryStats.ScalarComparisonsCount << " + " << TheQueryStats.BoxOverlapsCount << " + " << TheQueryStats.ObjectOverlapsCount << " = "
						<< TheQueryStats.ScalarComparisonsCount + TheQueryStats.BoxOverlapsCount + TheQueryStats.ObjectOverlapsCount;
				}

				cout << '\n';
				failuresCount += failures;
			}
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

	WarnInDebugBuild();

	create_directory(GetOutputPath());

	if (GetDataDirectory().empty())
	{
		cout << "\n'data' directory not found at root, no datasets will be loaded\n";
	}

	auto const configFilePath = GetOutputPath() / (string(SpatialIndexCompareTestName) + ".cfg");
	if (is_regular_file(configFilePath))
	{
		GetConfig().ReadFile(configFilePath, false);
	}

	auto totalFailures = 0;

	Stopwatch const timer;

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

			auto const sizeRange = GetDatasetSizeRange();

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

					TestContext test{ dataset };

					totalFailures += RunTest<SpatialKeyType, Test_Load_Query_Destroy>(test);

					totalFailures += RunTest<SpatialKeyType, Test_Insert_Erase_Query>(test);

					test.perfRecord.Save();
				}
			}
		});

	cout << "\nTotal running time (s): " << ( timer.ElapsedMilliseconds() + 900 ) / 1000 << '\n';

	REQUIRE(totalFailures == 0);
}
