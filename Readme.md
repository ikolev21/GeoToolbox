# GeoToolbox

A collection of tools that can help with the development of geometry applications in C++

## Requirements

* C++17 compiler
* Recent CMake

The rest are optional, downloaded automatically by CMake when enabled:

* [Catch2](https://github.com/catchorg/Catch2/) for unit tests
* [Eigen](https://eigen.tuxfamily.org) for vectors
* [Shapelib](https://download.osgeo.org/shapelib/) to load datasets
* [LodePNG](https://github.com/lvandeve/lodepng) to visualize datasets

Tested on Windows (MSVC 2022 17.10, Intel 2025, Clang 17) , Rocky Linux 8 (Clang 17, GCC 13), Ubuntu 24 (Clang 19, GCC 13)

## Spatial index performance comparison

Currently, the focus of this project is to make a flexible performance comparison of the best in-memory spatial index implementations in C++.

Included are:

- [Boost.Geometry 1.86](https://www.boost.org/doc/libs/1_86_0/libs/geometry/doc/html/geometry/reference/spatial_indexes.html) R-Tree
- [Nanoflann 1.6.1](https://github.com/jlblancoc/nanoflann) k-d tree
- [GEOS 3.13.0](https://libgeos.org/) STR-tree
- GEOS 3.13.0 quad-tree

([Spatial C++ Library 2.1.8](https://spatial.sourceforge.net/) k-d tree used to be included but was dropped, it performs generally slower and is no longer maintained)

They are compared to an `std::vector` container without any spatial indexing.

Two test scenarios are executed:

- Bulk-load all elements, then run a list of nearest element and box window queries
- Insert all elements one by one, erase some of them, reinsert those back, then run just the box window queries

Individual operations (load, insert, erase, query, destroy) are measured separately and recorded, along with the total running time.

These parameters can be varied:

* Spatial key type: point or box, just 2D for now
* Vector primitive type: `std::array` or Eigen dense vector
* Datasets:
  * synthetic:
    * uniform distribution
    * skewed distribution over one of the axes, (x, y<sup>4</sup>)
    * islands (keys are gathered around a few distant centers)
    * (for boxes) skewed aspect, averaging around 100x1
  * Real-world: loaded from ESRI shape file
* The size of the dataset (by power of 10, up to one million).

### Supported features by spatial index

| Index \ Feature | Point keys | Box keys | Bulk-load | Insert | Erase | Box window query | Nearest query |
| --- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| Boost R-Tree | + | + | + | + | + | + | + |
| Nanoflann | + | +<sup>1</sup> | + | | | +<sup>1</sup> | for points only |
| GEOS STR-tree | +<sup>2</sup> | + | + | | | + | |
| GEOS quad-tree | +<sup>2</sup> | + | + | | | + | |

<sup>1</sup>: Nanoflann only works with N-dimensional points, and only offers a nearest query implementation.
To work with boxes, they can be represented as 2N dimensional points, storing both the lower and upper limit along each axis.
This requires writing custom implementations of the queries; currently just a window query implementation is included.
Also, Nanoflann offers some support for modifying the index by simply attaching several trees together, this option is not tested here.

<sup>2</sup>: GEOS works with boxes only, but a point can be represented as a box with coinciding ends

### Results

This is an example extracted from the test results on a Dell Precision 5560 with i7-11850H 2.50GHz, running Windows 11 as main OS and Rocky 8 and Ubuntu 24 in VMs (the compilers used are the ones listed above under Requirements).

The scenario is Bulk-load/Query/Destroy with 100,000 boxes of the [Maricopa County parcels](https://hub.arcgis.com/datasets/dbf139379db946e1b10a2f15672c142d/) dataset
and 100,000 points of the [Maricopa County parcel points](https://hub.arcgis.com/datasets/dbf139379db946e1b10a2f15672c142d/) dataset.

Times are in microseconds.

| Operation | Spatial Key | Dataset Name | Spatial Index | Windows<br/>VC 17.10 | Windows<br/>Intel | Windows<br/>Clang 17 | Rocky<br/>Clang 17 | Rocky<br/>GCC 13 | Ubuntu<br/>Clang 19 | Ubuntu<br/>GCC 13 |
| --- | :---: | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Bulk Load | point | Maricopa_Parcel_Points.shp | Boost R-tree 1_86 | 7965 | 7394 | 7500 | 7583 | 7934 | 5254 | 5082 |
| Bulk Load | point | Maricopa_Parcel_Points.shp | GEOS 3.13.0 QuadTree | 82135 | 85316 | 80451 | 166100 | 111546 | 76931 | 58776 |
| Bulk Load | point | Maricopa_Parcel_Points.shp | GEOS 3.13.0 TemplateSTRTree | 14506 | 15467 | 15417 | 19225 | 16356 | 9630 | 9415 |
| Bulk Load | point | Maricopa_Parcel_Points.shp | nanoflann 0x161 Static | 4164 | 5273 | 5753 | 11108 | 6472 | 6026 | 3632 |
| Bulk Load | point | Maricopa_Parcel_Points.shp | std::vector | 30 | 21 | 17 | 42 | 59 | 46 | 24 |
| Bulk Load | box | Maricopa_Parcels.shp | Boost R-tree 1_86 | 8181 | 7503 | 7803 | 5580 | 6526 | 5686 | 5554 |
| Bulk Load | box | Maricopa_Parcels.shp | GEOS 3.13.0 QuadTree | 16537 | 16968 | 16934 | 9887 | 10024 | 13415 | 9831 |
| Bulk Load | box | Maricopa_Parcels.shp | GEOS 3.13.0 TemplateSTRTree | 14569 | 15528 | 15911 | 11059 | 10757 | 14958 | 9739 |
| Bulk Load | box | Maricopa_Parcels.shp | nanoflann 0x161 Static | 8077 | 9446 | 10148 | 11139 | 7521 | 13737 | 6639 |
| Bulk Load | box | Maricopa_Parcels.shp | std::vector | 35 | 21 | 20 | 46 | 18 | 34 | 42 |
| Destroy | point | Maricopa_Parcel_Points.shp | Boost R-tree 1_86 | 87 | 87 | 82 | 75 | 133 | 62 | 55 |
| Destroy | point | Maricopa_Parcel_Points.shp | GEOS 3.13.0 QuadTree | 61962 | 63637 | 60793 | 154378 | 126103 | 88786 | 96227 |
| Destroy | point | Maricopa_Parcel_Points.shp | GEOS 3.13.0 TemplateSTRTree | 1041 | 1075 | 1080 | 485 | 483 | 310 | 245 |
| Destroy | point | Maricopa_Parcel_Points.shp | nanoflann 0x161 Static | 1 | 2 | 2 | 5 | 3 | 1 | 1 |
| Destroy | point | Maricopa_Parcel_Points.shp | std::vector | 1 | 13 | 13 | 1 | 1 | 1 | 1 |
| Destroy | box | Maricopa_Parcels.shp | Boost R-tree 1_86 | 92 | 93 | 91 | 52 | 51 | 61 | 56 |
| Destroy | box | Maricopa_Parcels.shp | GEOS 3.13.0 QuadTree | 1551 | 1570 | 1595 | 758 | 885 | 1306 | 739 |
| Destroy | box | Maricopa_Parcels.shp | GEOS 3.13.0 TemplateSTRTree | 1028 | 1058 | 1099 | 339 | 317 | 530 | 332 |
| Destroy | box | Maricopa_Parcels.shp | nanoflann 0x161 Static | 3 | 4 | 4 | 3 | 3 | 3 | 2 |
| Destroy | box | Maricopa_Parcels.shp | std::vector | 2 | 11 | 53 | 1 | 1 | 1 | 1 |
| Query Box | point | Maricopa_Parcel_Points.shp | Boost R-tree 1_86 | 1459 | 1804 | 1764 | 2601 | 2982 | 1803 | 1754 |
| Query Box | point | Maricopa_Parcel_Points.shp | GEOS 3.13.0 QuadTree | 38655 | 39406 | 39221 | 38925 | 36354 | 19059 | 20047 |
| Query Box | point | Maricopa_Parcel_Points.shp | GEOS 3.13.0 TemplateSTRTree | 6432 | 6033 | 6087 | 2775 | 1995 | 1855 | 1438 |
| Query Box | point | Maricopa_Parcel_Points.shp | nanoflann 0x161 Static | 370 | 318 | 341 | 723 | 332 | 360 | 305 |
| Query Box | point | Maricopa_Parcel_Points.shp | std::vector | 353266 | 363043 | 266285 | 705262 | 606751 | 388087 | 328821 |
| Query Box | box | Maricopa_Parcels.shp | Boost R-tree 1_86 | 1826 | 2162 | 2169 | 2196 | 2350 | 2174 | 2221 |
| Query Box | box | Maricopa_Parcels.shp | GEOS 3.13.0 QuadTree | 10013 | 10219 | 10149 | 9384 | 9800 | 12918 | 8354 |
| Query Box | box | Maricopa_Parcels.shp | GEOS 3.13.0 TemplateSTRTree | 7744 | 7237 | 7679 | 2154 | 1834 | 3492 | 1758 |
| Query Box | box | Maricopa_Parcels.shp | nanoflann 0x161 Static | 2045 | 1893 | 1906 | 2050 | 2152 | 2962 | 1878 |
| Query Box | box | Maricopa_Parcels.shp | std::vector | 410274 | 486090 | 382583 | 611785 | 532875 | 781690 | 451438 |
| Query Nearest | point | Maricopa_Parcel_Points.shp | Boost R-tree 1_86 | 10394 | 11751 | 12160 | 15225 | 15291 | 10097 | 9780 |
| Query Nearest | point | Maricopa_Parcel_Points.shp | nanoflann 0x161 Static | 4779 | 4356 | 4832 | 9290 | 6607 | 4444 | 4082 |
| Query Nearest | point | Maricopa_Parcel_Points.shp | std::vector | 632371 | 514588 | 408039 | 1014266 | 1133295 | 680790 | 553794 |
| Query Nearest | box | Maricopa_Parcels.shp | Boost R-tree 1_86 | 12096 | 14084 | 14110 | 13955 | 13629 | 13453 | 13184 |
| Query Nearest | box | Maricopa_Parcels.shp | std::vector | 1489319 | 1227473 | 624614 | 933611 | 999070 | 1170178 | 875091 |

The full results can be found [here](https://github.com/ikolev21/ikolev21.github.io)

Some conclusions that can be drawn:

* Nanoflann does best what it was designed for, queries on point keys, significantly outperforming the others
* It also does very well when working with box keys, competing for the first place
* Boost R-tree is a very good overall performer and most flexible
* GEOS quad-tree performs consistently worse than STRTree, maybe not worth considering
* GEOS STR-tree is on the same level as Boost R-tree, but has higher load cost (and even some non-zero destroy cost)
* A rather important detail about GEOS STR-tree is that it has some performance problem specifically on Windows, its queries run 3 times slower than on Linux.  
The official distribution of PostGIS on Windows is probably not compiled with any of the compilers tried here (it's maybe MinGW), but it still leaves some doubt about running PostGIS on Windows...

