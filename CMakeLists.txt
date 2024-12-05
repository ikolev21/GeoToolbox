cmake_minimum_required( VERSION 3.18 )

project( GeoToolbox )

include( CMakeDependentOption )
include( FetchContent )

set( CMAKE_CXX_STANDARD 17 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )
set( CMAKE_CXX_EXTENSIONS off )
if( MSVC )
	set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP /JMC" )
endif()

set_property( GLOBAL PROPERTY USE_FOLDERS ON )

option( BUILD_TESTS "Build tests" OFF )
cmake_dependent_option( BUILD_TESTS_PERFORMANCE "Performance tests" ON "BUILD_TESTS" ON )
option( ENABLE_CODEANALYSIS "Enable code analysis tools in Release build" ON )
option( ENABLE_EIGEN "Use Eigen for linear algebra" OFF )
option( ENABLE_LODEPNG "Include LodePNG for PNG image writing" OFF )
option( ENABLE_SHAPEFILE "Include ESRI Shapefile reader" OFF )

if( BUILD_TESTS_PERFORMANCE )
	set( ENABLE_LODEPNG ON CACHE INTERNAL "" )
	set( ENABLE_SHAPEFILE ON CACHE INTERNAL "" )
endif()


add_library( ExtraWarnings INTERFACE IMPORTED GLOBAL )

if( CMAKE_COMPILER_IS_GNUCXX )
	# No -Wextra
	target_compile_options( ExtraWarnings INTERFACE -Werror -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-sign-compare )
elseif( CMAKE_CXX_COMPILER_ID MATCHES "Clang" )
	# No -Weverything
	target_compile_options( ExtraWarnings INTERFACE -Werror -Wall -Wextra -Wshadow -Wdouble-promotion
	-Wno-unknown-pragmas -Wno-sign-conversion -Wno-sign-compare -Wno-logical-op-parentheses )
elseif( MSVC )
	# No /Wall
	# 4068: unknown pragma
	target_compile_options( ExtraWarnings INTERFACE /W4 /WX /wd4068 )
endif()

add_library( MsvcNoDeprecation INTERFACE )
add_library( MsvcCppConformance INTERFACE )
if( MSVC )
	target_compile_definitions( MsvcNoDeprecation INTERFACE -D_SECURE_SCL_DEPRECATE=0 -D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_DEPRECATE -D_CRT_SECURE_NO_WARNINGS -D_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING )
	target_compile_options( MsvcCppConformance INTERFACE /permissive- /Zc:__cplusplus )
endif()

# LodePNG - 2024-04-15

if( ENABLE_LODEPNG )
	set( LodePNG_Changeset ed3c5f14b136c936d615ee3b38aaa7e7e642f12c )
	if( NOT EXISTS ${FETCHCONTENT_BASE_DIR}/lodepng )
		file(
			DOWNLOAD https://github.com/lvandeve/lodepng/archive/${LodePNG_Changeset}.zip ${FETCHCONTENT_BASE_DIR}/lodepng-${LodePNG_Changeset}.zip
			SHOW_PROGRESS
			EXPECTED_HASH MD5=23fa29234267e6760560c1224ca191a4 )
		file(
			ARCHIVE_EXTRACT
			INPUT ${FETCHCONTENT_BASE_DIR}/lodepng-${LodePNG_Changeset}.zip
			DESTINATION ${FETCHCONTENT_BASE_DIR} )
		file( RENAME ${FETCHCONTENT_BASE_DIR}/lodepng-${LodePNG_Changeset} ${FETCHCONTENT_BASE_DIR}/lodepng )
	endif()
	add_library( GeoToolbox.LodePNG OBJECT ${FETCHCONTENT_BASE_DIR}/lodepng/lodepng.cpp )
endif()

# Shapefile

if ( ENABLE_SHAPEFILE )
	set( ShapelibVersion 1.5.0 )
	file(
		DOWNLOAD https://download.osgeo.org/shapelib/shapelib-${ShapelibVersion}.zip ${FETCHCONTENT_BASE_DIR}/shapelib-${ShapelibVersion}.zip
		SHOW_PROGRESS
		EXPECTED_HASH MD5=427AB6583E6B7F968C1B34666584975F )
	file(
		ARCHIVE_EXTRACT
		INPUT ${FETCHCONTENT_BASE_DIR}/shapelib-${ShapelibVersion}.zip
		DESTINATION ${FETCHCONTENT_BASE_DIR} )
	add_library(
		GeoToolbox.Shapefile OBJECT
		${FETCHCONTENT_BASE_DIR}/shapelib-${ShapelibVersion}/shapefil.h
		${FETCHCONTENT_BASE_DIR}/shapelib-${ShapelibVersion}/shpopen.c
		${FETCHCONTENT_BASE_DIR}/shapelib-${ShapelibVersion}/safileio.c )
endif()

# Eigen

if( ENABLE_EIGEN )
	file(
		DOWNLOAD https://gitlab.com/libeigen/eigen/-/archive/3.4/eigen-3.4.zip ${FETCHCONTENT_BASE_DIR}/eigen-3.4.zip
		SHOW_PROGRESS
		EXPECTED_HASH MD5=976dea978e5deb31ddcf22ee83145918 )
	file(
		ARCHIVE_EXTRACT
		INPUT ${FETCHCONTENT_BASE_DIR}/eigen-3.4.zip
		DESTINATION ${FETCHCONTENT_BASE_DIR} )
	add_library( Eigen3 INTERFACE )
	target_include_directories( Eigen3 INTERFACE ${FETCHCONTENT_BASE_DIR}/eigen-3.4 )
endif()

set( HeadersDir ${CMAKE_CURRENT_SOURCE_DIR}/include/GeoToolbox )

add_library( GeoToolbox
	${HeadersDir}/Asserts.hpp
	${HeadersDir}/Config.hpp src/Config.cpp
	${HeadersDir}/DescribeStruct.hpp
	${HeadersDir}/GeometryTools.hpp
	${HeadersDir}/Image.hpp src/Image.cpp
	${HeadersDir}/Profiling.hpp
	${HeadersDir}/Span.hpp
	${HeadersDir}/SpatialTools.hpp
	${HeadersDir}/StlExtensions.hpp

	${HeadersDir}/GeoToolbox.natvis
	
	Readme.md
)

target_link_libraries( GeoToolbox PRIVATE ExtraWarnings MsvcNoDeprecation MsvcCppConformance )
if ( ENABLE_EIGEN )
	target_link_libraries( GeoToolbox PUBLIC Eigen3 )
	target_compile_definitions( GeoToolbox PUBLIC ENABLE_EIGEN )
	if( WIN32 )
		target_sources( GeoToolbox PRIVATE ${FETCHCONTENT_BASE_DIR}/eigen-3.4/debug/msvc/eigen.natvis )
	endif()
endif()
if ( ENABLE_LODEPNG )
	target_link_libraries( GeoToolbox PRIVATE GeoToolbox.LodePNG )
	target_compile_definitions( GeoToolbox PRIVATE ENABLE_LODEPNG )
endif()
if ( ENABLE_SHAPEFILE )
	target_sources( GeoToolbox PRIVATE ${HeadersDir}/ShapeFile.hpp src/ShapeFile.cpp )
	target_link_libraries( GeoToolbox PRIVATE GeoToolbox.Shapefile )
endif()
target_include_directories( GeoToolbox PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include )
target_include_directories( GeoToolbox PRIVATE ${FETCHCONTENT_BASE_DIR} )

configure_file( Msvc.props.template Msvc.props )
set_property( TARGET GeoToolbox PROPERTY VS_USER_PROPS ${CMAKE_CURRENT_BINARY_DIR}/Msvc.props )


option( DATASET_Maricopa_Parcel_Points "1.7M parcel points, https://hub.arcgis.com/datasets/dbf139379db946e1b10a2f15672c142d/about" OFF )
option( DATASET_Maricopa_Parcels "1.7M parcel polygons, https://hub.arcgis.com/datasets/dbf139379db946e1b10a2f15672c142d/about" OFF )
option( DATASET_Maricopa_Streets "290K lines, https://hub.arcgis.com/datasets/dbf139379db946e1b10a2f15672c142d/about" OFF )
# option( DATASET_Miami_Dade_Building "200K buildings, https://hub.arcgis.com/datasets/6f3f2173669b492da5e872d58b86cd4e/about" OFF )
# option( DATASET_Miami_Dade_Property_Boundary "500K parcels, https://hub.arcgis.com/maps/ed0468f5e579464b84727a4ab614fd40" OFF )
# option( DATASET_Miami_Dade_Property_Point "700K parcel points, https://hub.arcgis.com/maps/bf92e51f90a8426cae904ebc15018067" OFF )
# option( DATASET_Miami_Dade_Street "100K lines, https://hub.arcgis.com/datasets/a7e9449c8ec04bd39fb952e66e341442_0" OFF )

set( _datasetDownloadDir ${CMAKE_CURRENT_BINARY_DIR}/../data )

function( AddDataset _name _hash )
	#set( _baseUrl "https://ikolev21.github.io/datasets" )
	set( _baseUrl "http://ikolev.com/files/Datasets" )
	if( DATASET_${_name} )
		message( STATUS "Adding dataset ${_baseUrl}/${_name}.7z" )
		file(
			DOWNLOAD ${_baseUrl}/${_name}.7z ${_datasetDownloadDir}/${_name}.7z
			SHOW_PROGRESS
			EXPECTED_HASH SHA256=${_hash} )
		file(
			ARCHIVE_EXTRACT
			INPUT ${_datasetDownloadDir}/${_name}.7z
			DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/data )
	endif()
endfunction()

AddDataset( Maricopa_Parcel_Points d0a47ec800d1d404a91edefc26bcc2e117c364aac3b7b3d28142649ae7e13ffe )
AddDataset( Maricopa_Parcels dfbffe33a9e188e4510e6988fd7d90a39577370d6d1d912c8e69fe3a669b5749 )
AddDataset( Maricopa_Streets 05e199d796e0ad527fbff09d918bfb1772c224edfaef8789fb3a466cdd448918 )


if( BUILD_TESTS )
	add_subdirectory( test )
endif()
