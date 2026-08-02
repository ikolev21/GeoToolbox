# GeoToolbox

A collection of tools that can help with the development of geometry/geodesy/geography applications in C++

## Requirements

* C++17 compiler
* Recent CMake

The rest are optional, downloaded automatically by CMake when enabled:

* [Catch2](https://github.com/catchorg/Catch2/) for unit tests
* [Eigen](https://eigen.tuxfamily.org) for vectors
* [Shapelib](https://download.osgeo.org/shapelib/) to load datasets
* [LodePNG](https://github.com/lvandeve/lodepng) to visualize datasets

Tested on Windows (MSVC 2026, Clang 22)

## Spatial index performance comparison

Currently, the focus of this project is to make a flexible performance comparison of the best in-memory spatial index implementations in C++.

The major libraries tested are:

- [Boost.Geometry 1.90](https://www.boost.org/doc/libs/1_90_0/libs/geometry/doc/html/geometry/reference/spatial_indexes.html) R<sup>*</sup>-tree
- [Nanoflann 1.10.1](https://github.com/jlblancoc/nanoflann) k-d tree
- [GEOS 3.14.1](https://libgeos.org/) STR-tree
- [tidwall](https://github.com/tidwall/rtree.c) R-tree implementation in C
- **KdBoxTree** - a custom spatial index provided by GeoToolbox  
It is what its name says, a static k-d tree that supports boxes. The boxes that intersect the splitting plane are pushed into a new node that gets split further down by the other axes.

Other libraries and spatial indices have been tried and dropped due to lower quality or end of support:

- [Spatial C++ Library 2.1.8](https://spatial.sourceforge.net/)
- [Alglib 4.05](https://www.alglib.net/other/nearestneighbors.php) k-d Tree
- Other GEOS indices (k-d tree, Quad tree, vertex sequence packed R-tree)

These are compared to an `std::vector`, i.e. a container without any indexing.

Two test scenarios are executed:

- Bulk-load all elements, then run a list of nearest element or range (box window) queries
- Insert all elements one by one, erase some of them, reinsert those back, then run a list of range queries

Individual operations (load, insert, erase, query, destroy) are measured separately and recorded, along with the total running time.

These parameters can be varied and filtered out with a runtime configuration:

* Spatial key type: point or box, `float` or `double` scalar type, dimensions (2 and 3 are tested, more are possible)
* Vector primitive type: `std::array` or Eigen dense vector
* Datasets:
  * synthetic:
    * uniform distribution
    * skewed distribution over one of the axes, like (x, y<sup>4</sup>)
    * islands (keys are gathered around a few distant centers)
    * polygon (keys are arranged in two concentric circles)
    * (for boxes) skewed aspect, averaging around 100x1
  * real-world: loaded from ESRI shape or Wavefront OBJ files
* The size of the dataset, by power of 10.

### Supported features by spatial index

| Index \ Feature | Point keys | Box keys | Scalar type | Dimensions | Bulk-load | Insert | Erase | Range query | Nearest query |
| --- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| Boost R<sup>*</sup>-tree | + | + | any | any | + | + | + | + | + |
| Nanoflann | + | +<sup>1</sup> | any | any | | + <sup>2</sup> | | +<sup>3</sup> | for points only |
| GEOS STR-tree | +<sup>4</sup> | + | double | 2 | + | | | + | |
| tidwall R-tree | +<sup>4</sup> | + | any<sup>5</sup> | any<sup>5</sup> | + | + | + | + | |
| KdBoxTree | + | + | any | any | + | | | + | + |

<sup>1</sup>: Nanoflann only works with N-dimensional points. To work with boxes, they can be represented as 2N dimensional points, storing both the lower and upper limit along each axis.
This requires writing custom implementations of the queries; currently just a range query implementation is included.

<sup>2</sup>: Nanoflann offers some support for modifying the index by simply attaching several trees together, this option is not tested here.

<sup>3</sup>: As of version 1.8.0 Nanoflann includes a range query, but the implementation here that was written for older Nanoflann versions turns out to be faster, so it's used instead

<sup>4</sup>: Intended to work with boxes only, but a point can be represented as a box with coinciding ends. Of course, this spends unnecessary memory and does redundant operations, so performance suffers.

<sup>5</sup>: Dimensions and scalar type are fixed by macros at compilation time, so it is possible to redefine the macros to compile the library (and its clients) for a single chosen dimension/type, which cannot be varied across the project

### Results

Test results on an ASUS ROG Strix G16 running Windows 11 are uploaded [here](https://github.com/ikolev21/ikolev21.github.io)

The fastest index per combination of key type and query type is given in the table below, averaged over all datasets and sizes (2D and 3D behave similarly, so they are not separated). Notable per-dataset exceptions are in italics.

| Key type \ Query | Range (box window) | Nearest (k closest) |
| --- | --- | --- |
| **Point** | KdBoxTree *(nanoflann ties at small sizes)* | nanoflann *(KdBoxTree on the Polygon dataset, and at 10<sup>6</sup>)* |
| **Box** | KdBoxTree *(GEOS matches or edges it on large low-clustering 2D sets, ~10<sup>5</sup>)* | KdBoxTree *(only it and Boost implement it)* |

Some conclusions that can be drawn:

* No single index wins everything, but **KdBoxTree** is the most well-rounded: it is the quickest to bulk-load, the fastest at range queries on both point and box keys, and the fastest at nearest queries on box keys, trailing only nanoflann on the point proximity query it was not specialized for, but at the same time fixing nanoflann's weakness on the Polygon dataset. It also offers flexibility, ease of use, and a clean C++17 implementation.
* **Nanoflann** does best what it was designed for, proximity queries on point keys, and uses by far the least memory. That lead holds for typical point clouds but fades on the clustered Islands and structured Polygon datasets, and at the largest sizes (10<sup>6</sup>), where KdBoxTree overtakes it. It is not competitive for range queries, though it can be adapted to box keys by treating each box as a 2N-dimensional point (range queries only).
* **GEOS** STR-tree is strong at what it was designed for, range queries over 2D boxes, matching or slightly beating KdBoxTree on large low-clustering 2D box sets around 10<sup>5</sup> elements. It is limited to 2D `double` keys, does no nearest queries, and treats points as degenerate boxes (so it is slower on point keys).
* A rather important detail about the GEOS indices is that they have a performance problem on Windows with the MSVC compiler, queries run 2-3 times slower than with Clang.  
This is caused by a missed optimization in MSVC, [reported here](https://developercommunity.visualstudio.com/t/MSVC-O2-lowers-std::islessequal-to-_dpc/11129102).  
The effect is recorded in the results file `test/Performance/testResults/CompareSpatialIndices_GEOS.tsv`.  
Included is a patch `patches/geos-3.14.1/Envelope.h.diff` that works around the problem, bringing MSVC on par with Clang. The results uploaded at the link above have the patch applied.  
Hopefully the official distribution of PostgreSQL+PostGIS on Windows is **not** compiled with MSVC.
* Boost R-tree lags behind in query performance, but is the most versatile (as the full row of +'s in the table above shows) and, together with tidwall, the only tree index here that supports dynamic insert/erase.
* The tidwall R-tree sits between GEOS and Boost, both in terms of performance and versatility.
* Memory usage (the index's own footprint, roughly constant per element) is by far the lowest in nanoflann - about 9 bytes per point and 13 per box, some 3.7-7x leaner than the rest - because it stores only an index into the caller's dataset, a limitation that may require an additional vector allocation that isn't measured here. The other indices work with pointers to the elements and impose no such restriction on the client's data.  
KdBoxTree is the next leanest overall (about 30 bytes per 2D point, 47 per box). It offers an option to store an extra copy of the spatial keys to speed up building and queries (the `StoreSpatialKeys` template flag). The results above are measured with the option enabled. Turning it off drops KdBoxTree to a pointer-only footprint, leaner than Boost, at the cost of roughly 1.2x slower builds and queries at the largest sizes.  
Boost is close behind at a flat ~43 bytes per element (points and boxes alike, since it always stores a bounding box - for box keys KdBoxTree's extra key copy actually edges just past it), then GEOS (~58) and tidwall (~67, the heaviest).

## History

* 2026-08-01
  - Added own KdBoxTree
  - Tracked and worked around GEOS's problem with MSVC
  - Nanoflann updated to 1.10.1
  - GEOS updated to 3.14.1
  - Small additions and improvements

* 2026-04-19 - Major update
  - Added support for 3 and more dimensions, included some 3D datasets
  - Added loading and storing datasets from/to Wavefront OBJ files
  - Added memory tracking
  - Added query statistics for some of the indices: number of visited nodes, object tests, scalar comparisons, and box comparisons (by applying patches to their libraries)
  - Added Polygon synthetic dataset (keys are arranged in two concentric circles)
  - Added tidwall R-tree index from https://github.com/tidwall/rtree.c.git
  - Added Alglib k-d tree from https://www.alglib.net
  - Removed Spatial++ index
  - Boost updated to 1.90
  - Nanoflann updated to 1.9.0
  - Some setup to make it easier to add proprietary spatial indices to the comparison
  - Tons of smaller additions and improvements

* 2025-02-19
  - Added more synthetic datasets
  - Various smaller additions and improvements

* 2024-12-10 - First working state
  - Basic linear algebra using std::array<>, with an option to switch to Eigen
  - Performance comparison of Nanoflann, Boost, Spatial++, and GEOS spatial indices, using datasets in ESRI shape files, and a uniform synthetic dataset
