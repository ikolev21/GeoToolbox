# Copyright 2024-2026 Ivan Kolev
#
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

add_executable( GeoToolbox.PerfTest
	../MemoryTracker.cpp

	AlgLib.hpp
	Boost.hpp
	Geos.hpp
	NanoflannAdapter.hpp
	SpatialIndexStd.cpp
	SpatialIndexTest.cpp
	SpatialIndexWrapper.hpp
	TestTools.cpp TestTools.hpp
	TidwallRtree.hpp
)

if( WIN32 )
	set_source_files_properties( SpatialIndexTest.cpp PROPERTIES COMPILE_FLAGS "/bigobj" )
endif()

target_link_libraries( GeoToolbox.PerfTest PRIVATE ExtraWarnings MsvcNoDeprecation MsvcCppConformance GeoToolbox Catch2::Catch2 GeoToolbox.LodePNG ) # Eigen3
target_include_directories( GeoToolbox.PerfTest PRIVATE ${INCLUDE_DIR} ${FETCHCONTENT_BASE_DIR} )
target_compile_definitions( GeoToolbox.PerfTest PRIVATE
	CMAKE_BINARY_DIR=\"${CMAKE_BINARY_DIR}\"
	RUNTIME_ENVIRONMENT_ID=\"${GeoToolbox_RUNTIME_ENVIRONMENT_ID}\"
	)
set_property( TARGET GeoToolbox.PerfTest PROPERTY VS_USER_PROPS ${CMAKE_CURRENT_BINARY_DIR}/../Msvc.props )

# Try to do without this, split code into more files
# if( WIN32 )
	# target_compile_options( GeoToolbox.PerfTest PRIVATE "$<$<CONFIG:Debug>:/bigobj>" )
# endif()

option( GeoToolbox_ENABLE_BOOST "Include tests with Boost Geometry, must be installed separately" OFF )

if( GeoToolbox_ENABLE_BOOST )
	if( CMAKE_VERSION VERSION_GREATER 3.29 )
		# Boost must be already installed somewhere. Set Boost_DIR if needed
		cmake_policy( SET CMP0167 OLD )
	endif()
	find_package( Boost REQUIRED )
	target_sources( GeoToolbox.PerfTest PRIVATE Boost.cpp )
	target_compile_definitions( GeoToolbox.PerfTest PRIVATE ENABLE_BOOST )
	target_link_libraries( GeoToolbox.PerfTest PRIVATE Boost::boost )
endif()


option( GeoToolbox_ENABLE_GEOS "Include tests with the GEOS library, auto-downloaded from https://libgeos.org/usage/download/" OFF )

set( BUILD_SHARED_LIBS OFF )

if( GeoToolbox_ENABLE_GEOS )

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
	if( CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND WIN32 )
		target_compile_options( geos PRIVATE -Wno-unknown-argument )
		target_compile_options( geos_c PRIVATE -Wno-unknown-argument )
		target_compile_options( GeoToolbox.PerfTest PRIVATE -Wno-unknown-argument )
	endif()
endif()

option( GeoToolbox_ENABLE_NANOFLANN "Include tests with the nanoflann library, auto-downloaded from https://github.com/jlblancoc/nanoflann" ON )

if( GeoToolbox_ENABLE_NANOFLANN )
	FetchContent_Declare(
		Nanoflann
		GIT_REPOSITORY https://github.com/jlblancoc/nanoflann.git
		GIT_TAG        v1.9.0
	)

	set( NANOFLANN_BUILD_EXAMPLES OFF CACHE INTERNAL "" )
	set( NANOFLANN_BUILD_TESTS OFF CACHE INTERNAL "" )

	FetchContent_MakeAvailable(Nanoflann)

	target_compile_definitions( GeoToolbox.PerfTest PRIVATE ENABLE_NANOFLANN )
	target_link_libraries( GeoToolbox.PerfTest PRIVATE nanoflann::nanoflann )
	target_sources( GeoToolbox.PerfTest PRIVATE ${FETCHCONTENT_BASE_DIR}/nanoflann-src/include/nanoflann.hpp )
endif()

option( GeoToolbox_ENABLE_ALGLIB "Include tests with the ALGLIB C++ library, auto-downloaded from https://www.alglib.net/download.php" OFF )

if( GeoToolbox_ENABLE_ALGLIB )
	set( _alglibFile alglib-4.05.0.cpp.gpl.zip )
	file(
		DOWNLOAD https://www.alglib.net/translator/re/${_alglibFile} ${FETCHCONTENT_BASE_DIR}/${_alglibFile}
		SHOW_PROGRESS
		EXPECTED_HASH SHA1=a13b118fc957cb811243c6362699aff08d9ef1bf )
	file(
		ARCHIVE_EXTRACT
		INPUT ${FETCHCONTENT_BASE_DIR}/${_alglibFile}
		DESTINATION ${FETCHCONTENT_BASE_DIR} )

	set(_p ${FETCHCONTENT_BASE_DIR}/alglib-cpp/src)
	add_library( AlgLib STATIC ${_p}/alglibinternal.cpp ${_p}/alglibmisc.cpp ${_p}/ap.cpp ${_p}/linalg.cpp )
	#target_compile_options( AlgLib PRIVATE /W1 )
	target_link_libraries( GeoToolbox.PerfTest PRIVATE AlgLib )
	target_compile_definitions( GeoToolbox.PerfTest PRIVATE ENABLE_ALGLIB )
endif()

option( GeoToolbox_ENABLE_TIDWALL_RTREE "Include tests with Josh Baker's (tidwall) R-tree implementation in C, auto-downloaded from https://github.com/tidwall/rtree.c" ON )
set( GeoToolbox_TIDWALL_DIMENSIONS 2 CACHE STRING "Tidwall R-Tree defines the number of dimensions with a macro at compile-time" )

if( GeoToolbox_ENABLE_TIDWALL_RTREE )
	FetchContent_Declare(
		TidwallRtree
		GIT_REPOSITORY https://github.com/tidwall/rtree.c.git
		GIT_TAG        v0.5.3
	)

	FetchContent_MakeAvailable(TidwallRtree)

	set(_p ${FETCHCONTENT_BASE_DIR}/tidwallrtree-src)
	add_library( TidwallRtree STATIC ${_p}/rtree.c ${_p}/rtree.h )
	target_compile_definitions( TidwallRtree PRIVATE DIMS=${GeoToolbox_TIDWALL_DIMENSIONS} )
	if( 1 )
		target_compile_definitions( TidwallRtree PRIVATE RTREE_NOATOMICS )
	else()
		if( CMAKE_GENERATOR_TOOLSET MATCHES ^v14 )
			target_compile_options( TidwallRtree PRIVATE "/experimental:c11atomics" )
		endif()
	endif()
	target_link_libraries( GeoToolbox.PerfTest PRIVATE TidwallRtree )
	target_compile_definitions( GeoToolbox.PerfTest PRIVATE ENABLE_TIDWALL_RTREE=${GeoToolbox_TIDWALL_DIMENSIONS} )
endif()


if( 0 )
# No active support, code design doesn't seem efficient, much slower in most use cases, I don't see any point in including this one

option( GeoToolbox_ENABLE_LIBSPATIALINDEX "Include tests with the libspatialindex library, auto-downloaded from https://github.com/libspatialindex/" OFF )

if( GeoToolbox_ENABLE_LIBSPATIALINDEX )

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

if( EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/private )
	add_subdirectory( private )
endif()
