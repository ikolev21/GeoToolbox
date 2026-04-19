# GeoToolbox

A collection of tools that can help with the development of geometry/geodesy applications in C++

## Requirements

* C++17 compiler
* Recent CMake

The rest are optional, downloaded automatically by CMake when enabled:

* [Catch2](https://github.com/catchorg/Catch2/) for unit tests
* [Eigen](https://eigen.tuxfamily.org) for vectors
* [Shapelib](https://download.osgeo.org/shapelib/) to load datasets
* [LodePNG](https://github.com/lvandeve/lodepng) to visualize datasets

Tested on Windows (MSVC 2026 18.5, Clang 20), Rocky Linux 9 (Clang 20, GCC 15)

## Spatial index performance comparison

Currently, the focus of this project is to make a flexible performance comparison of the best in-memory spatial index implementations in C++.

The major libraries tested are:

- [Boost.Geometry 1.90](https://www.boost.org/doc/libs/1_90_0/libs/geometry/doc/html/geometry/reference/spatial_indexes.html) R-Tree
- [Nanoflann 1.9.0](https://github.com/jlblancoc/nanoflann) k-d tree
- [GEOS 3.13.0](https://libgeos.org/) STR-tree
- [tidwall](https://github.com/tidwall/rtree.c) R-Tree implementation in C

Other libraries and spatial indices have been tried and dropped due to lower quality or end of support:

- [Spatial C++ Library 2.1.8](https://spatial.sourceforge.net/)
- [Alglib 4.05](https://www.alglib.net/other/nearestneighbors.php) k-d Tree
- Other GEOS indices (k-d tree, Quad tree, vertex sequence packed R-tree)

They are compared to an `std::vector` container without any spatial indexing.

Two test scenarios are executed:

- Bulk-load all elements, then run a list of nearest element or range (box window) queries
- Insert all elements one by one, erase some of them, reinsert those back, then run a list of range queries

Individual operations (load, insert, erase, query, destroy) are measured separately and recorded, along with the total running time.

These parameters can be varied:

* Spatial key type: point or box, 2D or 3D, float or double element type
* Vector primitive type: `std::array` or Eigen dense vector
* Datasets:
  * synthetic:
    * uniform distribution
    * skewed distribution over one of the axes, (x, y<sup>4</sup>)
    * islands (keys are gathered around a few distant centers)
    * polygon (elements aranged in two concentric circles)
    * (for boxes) skewed aspect, averaging around 100x1
  * Real-world: loaded from ESRI shape or Wavefront OBJ files
* The size of the dataset (by power of 10, up to one million).

### Supported features by spatial index

| Index \ Feature | Point keys | Box keys | Dimensions | Bulk-load | Insert | Erase | Range query | Nearest query |
| --- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| Boost R-Tree | + | + | any | + | + | + | + | + |
| Nanoflann | + | +<sup>1</sup> | any | | + <sup>2</sup> | | +<sup>3</sup> | for points only |
| GEOS STR-tree | +<sup>4</sup> | + | 2 | + | | | + | |
| tidwall R-tree | +<sup>4</sup> | + | any (recompilation) | + | + | + | + | |

<sup>1</sup>: Nanoflann only works with N-dimensional points. To work with boxes, they can be represented as 2N dimensional points, storing both the lower and upper limit along each axis.
This requires writing custom implementations of the queries; currently just a range query implementation is included.

<sup>2</sup>: Nanoflann offers some support for modifying the index by simply attaching several trees together, this option is not tested here.

<sup>3</sup>: As of version 1.8.0 Nanoflann includes a range query, but the implementation here that was written for older Nanoflann versions turns out to be faster, so it's used instead

<sup>4</sup>: Intended to work with boxes only, but a point can be represented as a box with coinciding ends

### Results

Test results on an ASUS ROG Strix G16 with AMD Ryzen 9 9955HX3D 2.5GHz, running Windows 11 and Rocky 9, are uploaded [here](https://github.com/ikolev21/ikolev21.github.io)

Some conclusions that can be drawn:

* Nanoflann does best what it was designed for, proximity queries on point keys, except for the Polygon dataset (which is a known bad case for k-d trees)
* It also does very well when adapted to work with box keys
* In turn, GEOS STR-tree is best for what it was designed for, namely range queries over 2D boxes and points (when compiled with Clang, see the next point)
* A rather important detail about GEOS STR-tree is that it has some performance problem specifically with the VC++ compiler, its queries run 2-3 times slower than with GCC or Clang.
The official distribution of PostGIS on Windows is probably not compiled with VC++.
* Boost R-tree is a very good overall performer and most flexible (as the full row of +'s in the table above shows)
* The tidwall R-tree sits between GEOS and Boost, both in terms of performance and flexibility
