# Catch

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.3.2
)

FetchContent_MakeAvailable(Catch2)

set_target_properties( Catch2 PROPERTIES FOLDER Catch2 )
set_target_properties( Catch2WithMain PROPERTIES FOLDER Catch2 )

if( WIN32 )
	set( _id $ENV{COMPUTERNAME} )
else()
	set( _id $ENV{HOSTNAME} )
endif()
if( NOT CMAKE_GENERATOR_TOOLSET STREQUAL "" )
	set( _id ${_id}_${CMAKE_GENERATOR_TOOLSET} )
else()
	set( _id ${_id}_${CMAKE_CXX_COMPILER_ID} )
endif()
if( NOT CMAKE_GENERATOR_PLATFORM STREQUAL "" )
	set( _id ${_id}_${CMAKE_GENERATOR_PLATFORM} )
endif()

string( TOLOWER ${_id} _id )

set( RUNTIME_ENVIRONMENT_ID ${_id} CACHE STRING "A string that identifies the runtime environment of tests, that's the machine name and the build toolset" )

string( REGEX MATCH "[a-zA-Z0-9_]+" _CHECK_ID ${RUNTIME_ENVIRONMENT_ID} )
if( NOT _CHECK_ID STREQUAL ${RUNTIME_ENVIRONMENT_ID} )
	message( FATAL_ERROR "RUNTIME_ENVIRONMENT_ID has invalid characters, only letters, digits and _ are allowed. Requested: ${RUNTIME_ENVIRONMENT_ID}, allowed ${_CHECK_ID}" )
endif()

# GeoToolbox.Test

add_executable( GeoToolbox.Test
	../Catch.natjmc

	GeometryTest.cpp
	ProfilingTest.cpp
	SpanTest.cpp
)

target_link_libraries( GeoToolbox.Test PRIVATE ExtraWarnings MsvcNoDeprecation MsvcCppConformance GeoToolbox Catch2::Catch2WithMain )
target_include_directories( GeoToolbox.Test PRIVATE ${FETCHCONTENT_BASE_DIR} )
set_property( TARGET GeoToolbox.Test PROPERTY VS_USER_PROPS ${CMAKE_CURRENT_BINARY_DIR}/../Msvc.props )

if( BUILD_TESTS_PERFORMANCE )
	add_subdirectory( Performance )
endif()
