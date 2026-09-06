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
#include "KdBoxTreeAdapter.hpp"
#include "NanoflannAdapter.hpp"
// ReSharper disable once CppUnusedIncludeDirective
#include "SpatialIndexAdapter.hpp"
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

constexpr auto OpNameQueryRange = "Query Range";
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
	return array{ static_cast<unique_ptr<SpatialIndexAdapter<TSpatialKey>>>(make_unique<TIndex<TSpatialKey>>())... };
}

template <typename TSpatialKey>
auto const IndicesToTest = MakeIndicesToTest<TSpatialKey
	, StdVectorAdapter // Too slow, enable only to verify the results of the other participants
	, NanoflannKdtreeAdapter
	, KdBoxTreeAdapter
	, GeosTemplateStrTreeAdapter
	//, GeosKdTreeAdapter	// Always slower than TemplateStrTree
	//, GeosQuadTreeAdapter	// Always slower than TemplateStrTree
	//, GeosVertexSequencePackedRtreeAdapter	// In rare cases is just a bit faster than TemplateStrTree, (much) slower in 
	, TidwallRtreeAdapter
	, BoostRtreeAdapter
	//, AlglibKdtreeAdapter	// works with double only and needs conversion from float, not implemented yet. Query times are consistently worse than all other indices
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
	string const DatasetName_Clusters = "Synthetic_Clusters";
	string const DatasetName_Polygon = "Synthetic_Polygon";
	string const DatasetName_Parcels = "Synthetic_Parcels";

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

		// Cluster populations follow Zipf's law, which is what spreads the clustering over several scales rather than putting it all at one. A cluster's radius follows from the share of the
		// extent's volume it covers rather than from a fraction of the extent, so that the density inside it, and with it the number of features a query returns, is the same in any number of
		// dimensions
		[[nodiscard]] Dataset<TSpatialKey> MakeClusters(int datasetSize, int clusterCount, ScalarType largestClusterVolumeShare)
		{
			ASSERT(datasetSize > 0);
			ASSERT(clusterCount > 0);
			ASSERT(largestClusterVolumeShare > 0 && largestClusterVolumeShare <= 1);

			constexpr auto dimensions = SpatialKeyTraits<TSpatialKey>::Dimensions;

			auto const count = size_t(clusterCount);
			uniform_real_distribution<ScalarType> distributionPosition{ ScalarType(0), ScalarType(1) };
			vector<double> populations(count);
			vector<ScalarType> radii(count);
			vector<VectorType> centers(count);
			for (auto k = 0; k < clusterCount; ++k)
			{
				populations[size_t(k)] = 1.0 / (k + 1);
				auto const radius = extent * ScalarType(pow(double(largestClusterVolumeShare) / (k + 1), 1.0 / dimensions)) / 2;
				// Keeping a center a radius away from the boundary is what lets the offsets below go unclamped, and a clamp would pile features onto the boundary
				auto const span = extent - 2 * radius;
				radii[size_t(k)] = radius;
				centers[size_t(k)] = VectorTraits<VectorType>::FromArray(
					MakeRandomArray([&] { return radius + span * distributionPosition(randomGenerator); }, make_index_sequence<size_t(dimensions)>()));
			}

			// Drawn over a unit radius and scaled per cluster, the profile of a cluster being the same at every size
			array offsetPositions = { ScalarType(-1), ScalarType(-0.5), ScalarType(0), ScalarType(0.5), ScalarType(1) };
			array offsetWeights = { ScalarType(0), ScalarType(0.1), ScalarType(1), ScalarType(0.1), ScalarType(0) };
			piecewise_linear_distribution<ScalarType> distributionOffset{ offsetPositions.begin(), offsetPositions.end(), offsetWeights.begin() };
			discrete_distribution<int> distributionCluster{ populations.begin(), populations.end() };
			uniform_real_distribution<ScalarType> distributionAspect{ ScalarType(0.5), ScalarType(2) };

			vector<Feature<TSpatialKey>> data{ size_t(datasetSize) };
			for (auto i = 0; i < datasetSize; ++i)
			{
				auto const cluster = size_t(distributionCluster(randomGenerator));
				auto const radius = radii[cluster];
				auto const center = centers[cluster] + VectorTraits<VectorType>::FromArray(
					MakeRandomArray([&] { return radius * distributionOffset(randomGenerator); }, make_index_sequence<size_t(dimensions)>()));

				if constexpr (SpatialKeyIsPoint<TSpatialKey>)
				{
					data[i] = { i, center };
				}
				else
				{
					data[i] = { i, MakeRandomBox(randomGenerator, center, boundingBox, distributionHeight, distributionAspect) };
				}
			}

			return Dataset{ DatasetName_Clusters, data };
		}
	};
}


// Fixed for the same reason as QueryRandomSeed
constexpr auto DatasetShuffleRandomSeed = 5081;

// Data read from a file comes in the order it was stored in, which for real data is usually a spatial one. That order is worth something to an index that builds by inserting the elements one
// by one and nothing to one that sorts them itself, so it is taken away here and all of them start from the same ground. The other datasets are random by construction, except Polygon, which
// is shuffled where it is generated
template <typename TSpatialKey>
void ShuffleKeys(vector<TSpatialKey>& keys)
{
	Catch::SimplePcg32 randomGenerator{ DatasetShuffleRandomSeed };
	shuffle(keys.begin(), keys.end(), randomGenerator);
}

// For the synthetic sets generated in a spatial order rather than at random. The ids are put back in the order of the array, which some of the indices rely on
template <typename TSpatialKey>
void ShuffleFeatures(vector<Feature<TSpatialKey>>& data, int count)
{
	Catch::SimplePcg32 randomGenerator{ DatasetShuffleRandomSeed };
	shuffle(data.begin(), data.begin() + count, randomGenerator);
	for (auto i = 0; i < count; ++i)
	{
		data[i].id = i;
	}
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
	ShuffleKeys(data);
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

		ShuffleKeys(verts);
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

		// Generated along its two circles, which would leave the indices that build by insertion working on a sorted input while the rest gain nothing
		ShuffleFeatures(dataset.data_, newSize);
	}
};


// A cadastre: the extent divided into as many cells as the dataset has features, by recursively splitting the longest axis of a cell at a random share of it between SplitRange and
// 1 - SplitRange, the whole of it then turned by RotationDegrees, so that the cells still cover the extent exactly and overlap nowhere while the boxes around them overlap as real ones do -
// neither of which any other set here has. Their sizes spread as the recursion deepens - 1900x between the 5th and the 95th percentile at 10^5 - the multi-scale property none of them has either
template <typename TSpatialKey>
struct DatasetParcels : Dataset<TSpatialKey>
{
	using KeyTraits = SpatialKeyTraits<TSpatialKey>;
	using ScalarType = typename KeyTraits::ScalarType;
	using BoxType = typename KeyTraits::BoxType;

	static constexpr auto Extent = ScalarType(10);

	// The smallest share of a side a split can take. It is the one knob and it moves two things at once: the aspect ratio of a cell stays under 1 / SplitRange, and the spread of the cell sizes
	// grows as it falls, since a cell's volume is the product of the split fractions above it
	static constexpr auto SplitRange = ScalarType(0.1);

	// The angle between the cadastre and the coordinate axes. Real parcels rarely line up with them, and once they do not, the box around a cell reaches past it into its neighbours - the one
	// property of real data no synthetic set here had. The angle is small because the box of a turned cell is at most cot(angle) times longer than it is wide, so a larger one would trade the
	// elongated boxes away, and the overlap does not need it: at 5 degrees a box already overlaps 6 others, at 45 only 8.5
	static constexpr auto RotationDegrees = 5.0;

	explicit DatasetParcels(int maxSize)
		: Dataset<TSpatialKey>{ DatasetName_Parcels, std::vector<TSpatialKey>(size_t(maxSize)) }
	{
		this->onSizeChange_ = OnSizeChange;
	}

	static void Split(DatasetParcels& dataset, Catch::SimplePcg32& randomGenerator, uniform_real_distribution<ScalarType>& distributionSplit, BoxType const& cell, int count, int firstIndex)
	{
		if (count == 1)
		{
			dataset.data_[firstIndex] = { firstIndex, cell };
			return;
		}

		auto const axis = int(MaximumValue(cell.Sizes()).second);
		auto const position = cell.Min()[axis] + cell.Size(axis) * distributionSplit(randomGenerator);
		auto const lowCount = count / 2;
		Split(dataset, randomGenerator, distributionSplit, cell.GetReducedFromAbove(axis, position), lowCount, firstIndex);
		Split(dataset, randomGenerator, distributionSplit, cell.GetReducedFromBelow(axis, position), count - lowCount, firstIndex + lowCount);
	}

	// The box around a cell once the cadastre is turned about the center of its extent, which in three dimensions leaves the third axis where it is, the cells being extruded rather than tilted
	static BoxType GetRotatedBounds(BoxType const& cell, ScalarType sine, ScalarType cosine)
	{
		auto const pivot = Extent / 2;
		auto const center = cell.Center();
		auto const sizes = cell.Sizes();
		auto const x = (center[0] - pivot) * cosine - (center[1] - pivot) * sine + pivot;
		auto const y = (center[0] - pivot) * sine + (center[1] - pivot) * cosine + pivot;
		auto const halfWidth = (sizes[0] * std::abs(cosine) + sizes[1] * std::abs(sine)) / 2;
		auto const halfHeight = (sizes[0] * std::abs(sine) + sizes[1] * std::abs(cosine)) / 2;

		auto min = cell.Min();
		auto max = cell.Max();
		min[0] = x - halfWidth;
		min[1] = y - halfHeight;
		max[0] = x + halfWidth;
		max[1] = y + halfHeight;
		return { min, max };
	}

	static void OnSizeChange(Dataset<TSpatialKey>& datasetBase, int newSize)
	{
		auto& dataset = static_cast<DatasetParcels&>(datasetBase);
		dataset.SetSize_(newSize);

		// Generated whole at every size, a part of a tessellation not being one
		Catch::SimplePcg32 randomGenerator{ DatasetMaker<TSpatialKey>::DefaultRandomSeed };
		uniform_real_distribution<ScalarType> distributionSplit{ SplitRange, ScalarType(1) - SplitRange };
		Split(dataset, randomGenerator, distributionSplit, BoxType::Square(Extent), newSize, 0);

		// Turned as one piece, so that the cells keep tiling the extent exactly while the boxes around them, which is all an index ever sees of them, overlap
		auto const sine = ScalarType(std::sin(RotationDegrees * Pi / 180));
		auto const cosine = ScalarType(std::cos(RotationDegrees * Pi / 180));
		for (auto i = 0; i < newSize; ++i)
		{
			auto& key = dataset.data_[i].spatialKey;
			key = GetRotatedBounds(key, sine, cosine);
		}

		dataset.boundingBox_ = {};

		// Generated in the order of the recursion, which is a spatial one
		ShuffleFeatures(dataset.data_, newSize);
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
				if (IsSelected(DatasetKey, DatasetName_Clusters, 0))
				{
					DatasetMaker<TSpatialKey> maker{ 1000, ScalarType(0.01) };
					return this->Next(maker.MakeClusters(maxSize, 32, ScalarType(0.005)));
				}

				break;

			case 3:
				if (IsSelected(DatasetKey, DatasetName_Polygon, 0))
				{
					DatasetPolygon<TSpatialKey> polygon{ maxSize };
					return this->Next(std::move(polygon));
				}

				break;

			case 4:
				// Box keys only: the cells are what this set is for, and their centers would be one more evenly spread point set
				if constexpr (SpatialKeyIsBox<TSpatialKey>)
				{
					if (IsSelected(DatasetKey, DatasetName_Parcels, 0))
					{
						DatasetParcels<TSpatialKey> parcels{ maxSize };
						return this->Next(std::move(parcels));
					}
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
	if (!IsSelected("StoreDatasetFormat", "shp", -1, false))
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

// The corners of a box as a mesh writer wants them, and the six quads over them, as zero-based indices
constexpr auto BoxCornerCount = 8;
constexpr int BoxQuads[][4] = { { 0, 1, 2, 3 }, { 4, 5, 6, 7 }, { 0, 3, 5, 4 }, { 3, 2, 6, 5 }, { 2, 1, 7, 6 }, { 1, 0, 4, 7 } };

template <typename TBox>
array<Vector3f, BoxCornerCount> GetBoxCorners(TBox const& box)
{
	auto const a = Convert<Vector3f>(box.Min());
	auto const b = Convert<Vector3f>(box.Max());
	return { Vector3f{ a[0], a[1], b[2] }, a, Vector3f{ b[0], a[1], a[2] }, Vector3f{ b[0], a[1], b[2] },
		Vector3f{ a[0], b[1], b[2] }, b, Vector3f{ b[0], b[1], a[2] }, Vector3f{ a[0], b[1], a[2] } };
}

template <typename TSpatialKey>
void SaveObj(Dataset<TSpatialKey> const& dataset)
{
	if (!IsSelected("StoreDatasetFormat", "obj", -1, false))
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

	auto const& keys = dataset.GetKeys();
	for (auto const& key : keys)
	{
		// OBJ has no way to say "a point", so a point key needs a surrogate box, and no constant size can suit datasets whose extents span five orders of magnitude. SavePly below is the
		// way out of that
		BoxType box;
		if constexpr (SpatialKeyIsPoint<TSpatialKey>)
		{
			box = BoxType::FromCenterAndSize(key, ScalarType(0.01));
		}
		else
		{
			box = key;
		}

		for (auto const& corner : GetBoxCorners(box))
		{
			file << "v  " << corner[0] << ' ' << corner[1] << ' ' << corner[2] << '\n';
		}
	}

	file << '\n';
	for (auto startIndex = 0LL, keyIndex = 0LL; keyIndex < Size(keys); ++keyIndex, startIndex += BoxCornerCount)
	{
		for (auto const& quad : BoxQuads)
		{
			file << "f " << startIndex + quad[0] + 1 << ' ' << startIndex + quad[1] + 1 << ' ' << startIndex + quad[2] + 1 << ' ' << startIndex + quad[3] + 1 << '\n';
		}
	}
}

// PLY carries a bare vertex list, so a point dataset exports as its keys and nothing else - no surrogate box and no size constant to pick. Box keys get the same eight corners and six quads
// as the OBJ export
template <typename TSpatialKey>
void SavePly(Dataset<TSpatialKey> const& dataset)
{
	if (!IsSelected("StoreDatasetFormat", "ply", -1, false))
	{
		return;
	}

	auto const filename = GetFilename<TSpatialKey>(dataset);
	auto const filepath = GetOutputPath() / filesystem::path{ filename + ".ply" };
	if (exists(filepath))
	{
		return;
	}

	std::ofstream file(filepath);
	if (!file)
	{
		return;
	}

	constexpr auto isPoint = SpatialKeyIsPoint<TSpatialKey>;
	auto const& keys = dataset.GetKeys();
	auto const keyCount = Size(keys);

	file << "ply\nformat ascii 1.0\n"
		"element vertex " << keyCount * (isPoint ? 1 : BoxCornerCount) << "\nproperty float x\nproperty float y\nproperty float z\n";
	if constexpr (!isPoint)
	{
		file << "element face " << keyCount * Size(BoxQuads) << "\nproperty list uchar int vertex_index\n";
	}

	file << "end_header\n";
	file.precision(std::numeric_limits<float>::max_digits10);	// The properties above are declared float, so this is what round-trips them and no more

	for (auto const& key : keys)
	{
		if constexpr (isPoint)
		{
			auto const point = Convert<Vector3f>(key);
			file << point[0] << ' ' << point[1] << ' ' << point[2] << '\n';
		}
		else
		{
			for (auto const& corner : GetBoxCorners(key))
			{
				file << corner[0] << ' ' << corner[1] << ' ' << corner[2] << '\n';
			}
		}
	}

	if constexpr (!isPoint)
	{
		for (auto startIndex = 0LL, keyIndex = 0LL; keyIndex < keyCount; ++keyIndex, startIndex += BoxCornerCount)
		{
			for (auto const& quad : BoxQuads)
			{
				file << "4 " << startIndex + quad[0] << ' ' << startIndex + quad[1] << ' ' << startIndex + quad[2] << ' ' << startIndex + quad[3] << '\n';
			}
		}
	}
}


struct TestContextBase
{
	Timings timings{ 2 * Timings::MsPerSecond };

	PerfRecord* perfRecord;

	string indexStats;

	bool const resetResults = GetConfig().Get<bool>("Reset");
};


// The grid queries form a lattice with the same number of samples along every axis, so the count per axis follows from the target total rather than the other way round. Keeping the total
// equal in every dimension is what makes the query timings comparable across dimensions - a fixed count per axis makes it grow as its power
constexpr auto GridQueryCount = 2000;

// Queries centred on features drawn from the dataset. A grid over the bounding box spends most of its queries in empty space on any clustered dataset, so on its own it measures how fast an
// index proves emptiness more than it measures query throughput. That is a real property, hence the grid stays, but as the smaller part of the query set
constexpr auto DataQueryCount = 6000;

// Fixed, so that every run asks the same questions. Deliberately not the seed the datasets are generated with, and deliberately not a configuration key: a conclusion that moves with the seed
// means the experiment is too small and should be made larger, not re-rolled
constexpr auto QueryRandomSeed = 4177;

constexpr auto QueryNearestCount = 15;

template <typename TSpatialKey>
struct TestContext : TestContextBase
{
	static constexpr auto Dimensions = SpatialKeyTraits<TSpatialKey>::Dimensions;
	using ScalarType = typename SpatialKeyTraits<TSpatialKey>::ScalarType;
	using VectorType = typename SpatialKeyTraits<TSpatialKey>::VectorType;
	using BoxType = typename SpatialKeyTraits<TSpatialKey>::BoxType;

	// The share of the dataset bounding box a query covers, cycled over the queries. Taking the Dimensions-th root of it gives the side of the query box, which keeps that share - and with it
	// the number of features a query returns on an evenly spread dataset - the same in any number of dimensions.
	static constexpr array QueryVolumeShares = { 1e-5, 1e-4, 1e-3 };

	// Divides the shares above. Without it a query centred on a feature of a clustered dataset returns a large part of the cluster it sits in, which measures result handling rather than the
	// traversal. It divides the share rather than the side of the box, so that it leaves the shares dimension-independent, as dividing a side by it would divide a share by its Dimensions-th
	// power. The value is the square of the side divisor it replaced, which leaves the 2D queries where they were
	static constexpr auto QueryVolumeReduction = 4096;


	Dataset<TSpatialKey> const* dataset;

	vector<BoxType> queries;
	vector<double> queryResults;

	// The queries in front of this index are the grid ones, the rest are centred on dataset features. Their result counts are reported separately
	int gridQueryCount = 0;


	explicit TestContext(Dataset<TSpatialKey> const& dataset, PerfRecord& record)
		: dataset(&dataset)
	{
		perfRecord = &record;

		if constexpr (StartsWith(SpatialKeyTraits<TSpatialKey>::VectorTraitsType::Name, "array"))
		{
			SaveObj(dataset);
			SavePly(dataset);
			if constexpr (SpatialKeyTraits<TSpatialKey>::Dimensions == 2)
			{
				SaveImage(dataset);
				SaveShapefile(dataset);
			}
		}

		auto const extent = dataset.GetMeanExtent();
		ASSERT(extent > 0);
		auto const querySizes = Transform(QueryVolumeShares, [extent](double share)
			{
				return ScalarType(double(extent) * pow(share / QueryVolumeReduction, 1.0 / Dimensions));
			});

		queries.reserve(size_t(GridQueryCount) + DataQueryCount);
		auto const queriesPerAxis = int( lround( pow( GridQueryCount, 1.0 / Dimensions ) ) );
		QueryIterator firstQuery{ GetLowBound(dataset.GetData().back().spatialKey), dataset.GetBoundingBox(), queriesPerAxis, querySizes };
		std::copy(firstQuery, QueryIterator<VectorType>{}, back_inserter(queries));
		gridQueryCount = int(Size(queries));

		Catch::SimplePcg32 randomGenerator{ QueryRandomSeed };
		uniform_int_distribution featureIndex{ 0, dataset.GetSize() - 1 };
		for (auto i = 0; i < DataQueryCount; ++i)
		{
			auto const center = SpatialKeyTraits<TSpatialKey>::GetCenter(dataset.GetData()[featureIndex(randomGenerator)].spatialKey);
			queries.push_back(BoxType::FromCenterAndSize(center, querySizes[i % Size(querySizes)]));
		}

		queryResults.reserve(queries.size());

		cout << dataset.GetName() << '\t' << dataset.GetSize() << '\n';
	}

	// Cross-index results are compared with a hybrid absolute/relative tolerance. The nearest-query metric (see SpatialIndexAdapter::QueryNearest) sums QueryNearestCount squared distances
	// computed in ScalarType, so on large-coordinate datasets the per-term rounding accumulates to a benign difference proportional to the sum's magnitude and the scalar precision. The
	// relative term (2 * QueryNearestCount epsilons, bounding the terms plus their accumulation) absorbs that; the absolute term keeps near-zero sums sane.
	static constexpr auto AbsoluteTolerance = 0.01;
	static constexpr auto RelativeTolerance = 2 * QueryNearestCount * double(std::numeric_limits<ScalarType>::epsilon());

	bool VerifyQueryResults(vector<double>&& results, string_view spatialIndexName, Timings::ActionStats* stats = nullptr)
	{
		if (queryResults.empty())
		{
			queryResults = std::move(results);
		}
		else
		{
			for (auto i = 0; i < Size(results); ++i)
			{
				auto const tolerance = std::max(AbsoluteTolerance, RelativeTolerance * std::max(abs(results[i]), abs(queryResults[i])));
				if (abs(results[i] - queryResults[i]) > tolerance)
				{
					cout << SetColorRed << std::fixed << "\t\t\tFAILED query index " << i << " for spatial index " << spatialIndexName
						<< ", expected result " << queryResults[i] << ", got " << results[i] << " (tolerance " << tolerance << ')' << ResetColor << '\n';
					if (stats != nullptr)
					{
						stats->failed = true;
					}

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

	// The count of features the range queries return, i.e. what the range query timings are the throughput of. It is a property of the dataset and the query set rather than of an index - all indices
	// run the same queries and are verified to return the same counts - but is recorded on every row to keep the rows self-contained
	[[nodiscard]] string PrintQueryResultCounts() const
	{
		AggregateStats<int> gridCounts;
		AggregateStats<int> dataCounts;
		for (auto i = 0; i < Size(queryResults); ++i)
		{
			(i < gridQueryCount ? gridCounts : dataCounts).AddValue(int(queryResults[i]));
		}

		return "Results# grid: " + gridCounts.Print() + ", data: " + dataCounts.Print();
	}

	// Returns the change factor compared to the previous best time
	double StoreResults(string_view testName, string_view spatialIndexName)
	{
		pair<double, double> accumulatedOldAndNewBestTimes{};

		for (auto const& action : timings.GetAllActions())
		{
			auto entry = this->perfRecord->MakeEntry(*dataset, spatialIndexName, testName, action.first);
			PerfRecord::Stats stats{ ToMicroseconds(action.second.bestTime), action.second.memoryDelta/* == std::numeric_limits<int64_t>::max() ? 0 : action.second.memoryDelta*/, action.second.failed };
			if (auto const queryStats = static_cast<QueryStats*>(action.second.extra.get()))
			{
				stats.queryVisitedNodes = queryStats->VisitedNodesCount;
				stats.queryObjectTests = queryStats->ObjectTestsCount;
			}

			// Only for range queries: a k-nearest query returns QueryNearestCount features by definition
			if (string_view{ action.first } == OpNameQueryRange )
			{
				stats.info = PrintQueryResultCounts();
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
			PerfRecord::Stats stats{ ToMicroseconds(timings.BestIterationTime()) };
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

		return accumulatedOldAndNewBestTimes.second > 0 ? accumulatedOldAndNewBestTimes.second * 100.0 / accumulatedOldAndNewBestTimes.first : -1;
	}
};

template <typename TSpatialKey>
struct TestScenario
{
	virtual ~TestScenario() = default;

	[[nodiscard]] virtual std::string_view Name() const = 0;

	// Return -1 if the scenario is not supported, or the number of failures is supported
	[[nodiscard]] virtual int Run(TestContext<TSpatialKey>&, SpatialIndexAdapter<TSpatialKey> const&) const = 0;
};


template <typename TSpatialKey>
struct Test_Load_Query_Destroy : TestScenario<TSpatialKey>
{
	using BoxType = typename SpatialKeyTraits<TSpatialKey>::BoxType;
	using VectorType = typename SpatialKeyTraits<TSpatialKey>::VectorType;

	[[nodiscard]] virtual char const* GetOpName() const = 0;

	[[nodiscard]] int Run(TestContext<TSpatialKey>& test, SpatialIndexAdapter<TSpatialKey> const& wrapper) const override
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
						if (queryIndex >= Size(queryResults))
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

		return test.VerifyQueryResults(std::move(queryResults), wrapper.Name(), statsQuery) ? 0 : 1;
	}

	[[nodiscard]] virtual double RunQuery(SpatialIndexAdapter<TSpatialKey> const& wrapper, std::shared_ptr<void> const& spatialIndex, BoxType const& query) const = 0;
};

template <typename TSpatialKey>
struct Test_Load_QueryRange_Destroy final : Test_Load_Query_Destroy<TSpatialKey>
{
	using BoxType = typename SpatialKeyTraits<TSpatialKey>::BoxType;

	[[nodiscard]] std::string_view Name() const override
	{
		return "Load-QueryRange-Destroy";
	}

	[[nodiscard]] char const* GetOpName() const override
	{
		return OpNameQueryRange;
	}

	[[nodiscard]] double RunQuery(SpatialIndexAdapter<TSpatialKey> const& wrapper, std::shared_ptr<void> const& spatialIndex, BoxType const& query) const override
	{
		return wrapper.QueryRange(spatialIndex, query);
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

	[[nodiscard]] double RunQuery(SpatialIndexAdapter<TSpatialKey> const& wrapper, std::shared_ptr<void> const& spatialIndex, BoxType const& query) const override
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

	[[nodiscard]] int Run(TestContext<TSpatialKey>& test, SpatialIndexAdapter<TSpatialKey> const& wrapper) const override
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

	static int Run_(TestContext<TSpatialKey>& test, SpatialIndexAdapter<TSpatialKey> const& wrapper)
	{
		Timings::ActionStats* statsQueryRange = nullptr;

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
				OpNameQueryRange,
				[&]
				{
					auto queryIndex = 0;
					for (auto const& query : test.queries)
					{
						auto const result = wrapper.QueryRange(spatialIndex, query);
						if (queryIndex >= Size(queryResults))
						{
							queryResults.push_back(result);
						}

						++queryIndex;
					}
				}, &statsQueryRange);
			statsQueryRange->extra = make_shared<QueryStats>(TheQueryStats);
			TheQueryStats.Clear();

			if (!statsStored)
			{
				statsStored = true;
				test.indexStats = wrapper.GetIndexStats(spatialIndex);
			}
		}

		return test.VerifyQueryResults(std::move(queryResults), wrapper.Name(), statsQueryRange) ? 0 : 1;
	}
};

template <typename TSpatialKey>
int RunSpatialIndex(TestContext<TSpatialKey>& testContext, TestScenario<TSpatialKey> const& scenario, SpatialIndexAdapter<TSpatialKey> const& wrapper)
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

			totalFailures += RunScenario<SpatialKeyType>(testContext, Test_Load_QueryRange_Destroy<SpatialKeyType>{});

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
