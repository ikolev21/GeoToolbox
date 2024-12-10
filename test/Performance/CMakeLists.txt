# Copyright 2024 Ivan Kolev
#
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

add_executable( GeoToolbox.PerfTest
	Boost.hpp
	Geos.hpp
	NanoflannAdaptor.hpp
	SpatialIndexStd.cpp
	SpatialIndexTest.cpp
	SpatialIndexWrapper.hpp
	Spatialpp.hpp
	TestTools.cpp TestTools.hpp
)

target_link_libraries( GeoToolbox.PerfTest PRIVATE ExtraWarnings MsvcNoDeprecation MsvcCppConformance GeoToolbox Catch2::Catch2 GeoToolbox.LodePNG GeoToolbox.Shapefile ) # Eigen3
target_include_directories( GeoToolbox.PerfTest PRIVATE ${INCLUDE_DIR} ${FETCHCONTENT_BASE_DIR} )
target_compile_definitions( GeoToolbox.PerfTest PRIVATE
	CMAKE_BINARY_DIR=\"${CMAKE_BINARY_DIR}\"
	RUNTIME_ENVIRONMENT_ID=\"${RUNTIME_ENVIRONMENT_ID}\"
	)
set_property( TARGET GeoToolbox.PerfTest PROPERTY VS_USER_PROPS ${CMAKE_CURRENT_BINARY_DIR}/../Msvc.props )

# Try to do without this, split code into more files
# if( WIN32 )
	# target_compile_options( GeoToolbox.PerfTest PRIVATE "$<$<CONFIG:Debug>:/bigobj>" )
# endif()

option( ENABLE_BOOST "Include tests with Boost Geometry, must be installed separately" OFF )

if( ENABLE_BOOST )
	# Boost must be already installed somewhere. Set Boost_INCLUDE_DIR if needed
	find_package( Boost REQUIRED )
	target_sources( GeoToolbox.PerfTest PRIVATE Boost.cpp )
	target_compile_definitions( GeoToolbox.PerfTest PRIVATE ENABLE_BOOST )
	target_link_libraries( GeoToolbox.PerfTest PRIVATE Boost::boost )
endif()


option( ENABLE_SPATIALCPP "Include tests with the Spatial C++ library, auto-downloaded from https://spatial.sourceforge.net/" OFF )

if( ENABLE_SPATIALCPP )
	file(
		DOWNLOAD https://master.dl.sourceforge.net/project/spatial/spatial_2.1.8.zip?viasf=1 ${FETCHCONTENT_BASE_DIR}/spatial_2.1.8.zip
		SHOW_PROGRESS
		EXPECTED_HASH MD5=60437F845A3D7D9A1ED7EF63707280C4 )
	file(
		ARCHIVE_EXTRACT
		INPUT ${FETCHCONTENT_BASE_DIR}/spatial_2.1.8.zip
		DESTINATION ${FETCHCONTENT_BASE_DIR} )
	add_library( SpatialCpp INTERFACE )
	if( WIN32 )
		target_compile_definitions( SpatialCpp INTERFACE _SILENCE_CXX17_OLD_ALLOCATOR_MEMBERS_DEPRECATION_WARNING )
	endif( WIN32 )

	target_sources( GeoToolbox.PerfTest PRIVATE Spatialpp.cpp )
	target_compile_definitions( GeoToolbox.PerfTest PRIVATE ENABLE_SPATIALCPP )
	target_link_libraries( GeoToolbox.PerfTest PRIVATE SpatialCpp )
endif()


option( ENABLE_GEOS "Include tests with the GEOS library, auto-downloaded from https://libgeos.org/usage/download/" OFF )

set( BUILD_SHARED_LIBS OFF )

if( ENABLE_GEOS )

	FetchContent_Declare(
	  Geos
	  URL https://download.osgeo.org/geos/geos-3.13.0.tar.bz2
	  URL_HASH SHA1=0510841658bbfb503295e6687befa0611cfc7954
	  DOWNLOAD_NAME geos.tar.bz2
	  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
	)
	FetchContent_MakeAvailable(Geos)

	target_compile_definitions( GeoToolbox.PerfTest PRIVATE ENABLE_GEOS )
	target_link_libraries( GeoToolbox.PerfTest PRIVATE geos )
	set_target_properties( geos PROPERTIES FOLDER GEOS )
	set_target_properties( geos_c PROPERTIES FOLDER GEOS )
	set_target_properties( ryu PROPERTIES FOLDER GEOS )
endif()

option( ENABLE_NANOFLANN "Include tests with the nanoflann library, auto-downloaded from https://github.com/jlblancoc/nanoflann" OFF )

if( ENABLE_NANOFLANN )
	FetchContent_Declare(
	  Nanoflann
	  GIT_REPOSITORY https://github.com/jlblancoc/nanoflann.git
	  GIT_TAG        v1.6.1
	)

	set( NANOFLANN_BUILD_EXAMPLES OFF CACHE INTERNAL "" )
	set( NANOFLANN_BUILD_TESTS OFF CACHE INTERNAL "" )

	FetchContent_MakeAvailable(Nanoflann)

	target_compile_definitions( GeoToolbox.PerfTest PRIVATE ENABLE_NANOFLANN )
	target_link_libraries( GeoToolbox.PerfTest PRIVATE nanoflann::nanoflann )
endif()

if( 0 )
# No active support, code design doesn't seem efficient, I don't see any point in including this one, I don't expect it can beat Boost R-tree

option( ENABLE_LIBSPATIALINDEX "Include tests with the libspatialindex library, auto-downloaded from https://github.com/libspatialindex/" OFF )

if( ENABLE_LIBSPATIALINDEX )

	FetchContent_Declare(
	  Libspatialindex 
	  GIT_REPOSITORY https://github.com/libspatialindex/libspatialindex.git
	  GIT_TAG        2.0.0
	)

	set( BUILD_TESTING OFF CACHE INTERNAL "" )
	FetchContent_MakeAvailable(Libspatialindex)

	target_compile_definitions( GeoToolbox.PerfTest PRIVATE ENABLE_LIBSPATIALINDEX )
	target_link_libraries( GeoToolbox.PerfTest PRIVATE spatialindex )
	set_target_properties( spatialindex PROPERTIES FOLDER Libspatialindex )
	set_target_properties( spatialindex_c PROPERTIES FOLDER Libspatialindex )
	set_target_properties( dist PROPERTIES FOLDER Libspatialindex )
endif()

endif()
