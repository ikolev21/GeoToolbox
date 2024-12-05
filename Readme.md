# GeoToolbox

A collection of tools that can help with the development of geometry applications in C++

## Requirements

* A C++17 compliant compiler
* CMake 3.18

The rest are optional, downloaded automatically by CMake when enabled:

* [Catch2](https://github.com/catchorg/Catch2/) for unit tests
* [Eigen](https://eigen.tuxfamily.org) for vectors
* [Shapelib](https://download.osgeo.org/shapelib/) to load datasets
* [LodePNG](https://github.com/lvandeve/lodepng) to visualize datasets

Tested on Windows with MSVC 2022 17.10.6 and Rocky Linux 8 with Clang 17 and GCC 13

## Spatial index performance comparison

Currently, the focus of this project is to make a flexible performance comparison of the best in-memory spatial index implementations in C++.

These spatial index implementations are included:

- [Boost.Geometry 1.86](https://www.boost.org/doc/libs/1_86_0/libs/geometry/doc/html/geometry/reference/spatial_indexes.html) R-Tree
- [Nanoflann 1.6.1](https://github.com/jlblancoc/nanoflann) k-d tree
- [GEOS 3.13.0](https://libgeos.org/) STR-tree
- GEOS 3.13.0 quad-tree
- [Spatial C++ Library 2.1.8](https://spatial.sourceforge.net/) k-d tree

They are compared to `std::vector` and `std::unordered_set` containers without any spatial indexing.

Two test scenarios are executed:

- Bulk-load all elements, then run a list of nearest element and box window queries
- Insert all elements one by one, erase 20% of them, reinsert those back, then run just the box window queries

Individual operations (load, insert, erase, query, destroy) are measured separately and recorded, along with the total running time.

These parameters can be varied:

* Spatial key type: point or box, just 2D for now
* Vector primitive type: `std::array` / Eigen dense vector
* Datasets:
  * synthetic: random-generated with uniform distribution
  * Real-world: loaded from ESRI shape file
* The size of the dataset (by power of 10).

### Supported features by spatial index

| Index \ Feature | Point keys | Box keys | Bulk-load | Insert | Erase | Box window query | Nearest query |
| --- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| Boost R-Tree | + | + | + | + | + | + | + |
| Nanoflann | + | +<sup>1</sup> | + | | | +<sup>1</sup> | for points only |
| GEOS STR-tree | +<sup>2</sup> | + | + | | | + | |
| GEOS quad-tree | +<sup>2</sup> | + | + | | | + | |
| Spatial C++ k-d tree | + | <sup>4</sup> | + | +<sup>3</sup> | +<sup>3</sup> | + | + |

<sup>1</sup>: Nanoflann only works with N-dimensional points, and only offers a nearest query implementation.
To work with boxes, they can be represented as 2*N-dimensional points, storing both the lower and upper limit along each axis.
This requires writing custom implementations of the queries; currently just a window query implementation is included.
Also, Nanoflann offers crude support for modifying the index by simply attaching several trees together, this option is not tested here.

<sup>2</sup>: GEOS works with boxes only, but any point can be represented as a box with coinciding ends

<sup>3</sup>: Requires doing explicit rebalancing after a batch of insert/erase operations

<sup>4</sup>: Spatial C++ claims to support indexing of boxes, but its query results didn't match the other indices, so box keys were not included

### Results

This is an example extracted from the test results on a Dell Precision 5560 with i7-11850H 2.50GHz, code compiled with MSVC 2022 17.10.

It's from the Load-Query-Destroy scenario with 1 million boxes of the [Maricopa County parcels](https://hub.arcgis.com/datasets/dbf139379db946e1b10a2f15672c142d/about) dataset

| Operation | Spatial Index | Time |
| --- | --- | ---: |
| Bulk Load | Boost R-tree 1_86 | 126167 |
| Bulk Load | GEOS 3.13.0 QuadTree | 187083 |
| Bulk Load | GEOS 3.13.0 TemplateSTRTree | 193315 |
| Bulk Load | nanoflann 0x161 Static | 150867 |
| Bulk Load | std::unordered_set | 180460 |
| Bulk Load | std::vector | 1233 |
| Window Query | Boost R-tree 1_86 | 163 |
| Window Query | GEOS 3.13.0 QuadTree | 525 |
| Window Query | GEOS 3.13.0 TemplateSTRTree | 332 |
| Window Query | nanoflann 0x161 Static | 230 |
| Window Query | std::unordered_set | 1350289 |
| Window Query | std::vector | 55169 |
| Destroy | Boost R-tree 1_86 | 5408 |
| Destroy | GEOS 3.13.0 QuadTree | 30723 |
| Destroy | GEOS 3.13.0 TemplateSTRTree | 11169 |
| Destroy | nanoflann 0x161 Static | 576 |
| Destroy | std::unordered_set | 57827 |
| Destroy | std::vector | 281 |

The full results can be found [here](https://github.com/ikolev21/ikolev21.github.io/blob/main/CompareSpatialIndices_IKDP5560_v143_x64.tsv)
