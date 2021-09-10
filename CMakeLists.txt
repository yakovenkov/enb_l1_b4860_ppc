########################################################################
# Prevent in-tree builds
########################################################################
if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
	message(FATAL_ERROR "Prevented in-tree build. This is bad practice.")
endif(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})

########################################################################
# Project setup
########################################################################
cmake_minimum_required(VERSION 3.20)
project(enb_nr)

set(CMAKE_C_STANDARD 11)

message( STATUS "CMAKE_SYSTEM: " ${CMAKE_SYSTEM} )
message( STATUS "CMAKE_SYSTEM_PROCESSOR: " ${CMAKE_SYSTEM_PROCESSOR} )
message( STATUS "CMAKE_CXX_COMPILER: " ${CMAKE_CXX_COMPILER} )

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake/modules")

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
	message(STATUS "Build type not specified: defaulting to Release.")
endif(NOT CMAKE_BUILD_TYPE)
set(CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE} CACHE STRING "")

########################################################################
# Options
########################################################################
option(BUILD_STATIC    "Attempt to statically link external deps" OFF)
option(RPATH           "Enable RPATH"                             OFF)
option(FAPI_TYPE       "FAPI type (B4860)"          "B4860")
set(GCC_ARCH native CACHE STRING "GCC compile for specific architecture.")

# Генератор сообщений L2 FAPI
option(ENABLE_L2_FAPI_GEN_4G "Enable L2 FAPI 4G Generator" ON)

option(ENABLE_B4860_DIRECT_LOG "Enable B4860 DSP direct log" ON)

option(ENABLE_CPRI_ETH_FDD "Enable YD/T 2341-2014 for FDD support" OFF)

########################################################################
# Find compiler
########################################################################
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message("-- Compiler is clang(++)")
    set(CMAKE_COMPILER_IS_CLANG 1)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message("-- Compiler is GNU ")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS "4.9.0")
        message(FATAL_ERROR "Insufficient gcc version")
    endif ()
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Intel" OR CMAKE_CXX_COMPILER_ID MATCHES "IntelLLVM")
    message("-- Compiler is icc(++)")
    set(CMAKE_COMPILER_IS_ICC 1)
endif ()

########################################################################
# Find dependencies
########################################################################
find_package(Threads REQUIRED)

include(CheckFunctionExistsMath)

########################################################################
# ASN.1 cmake module
########################################################################
cmake_policy(SET CMP0057 NEW)
cmake_policy(SET CMP0054 NEW)
include(asn1/FindASN1C)
include(cmake/modules/asn1/ASN1.cmake)

########################################################################
# Install Dirs
########################################################################
if (NOT CMAKE_INSTALL_LIBDIR)
	include(GNUInstallDirs)
endif (NOT CMAKE_INSTALL_LIBDIR)

# Fall back to just "lib" if the item provided by GNUInstallDirs doesn't exist
if (NOT EXISTS "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
	message(STATUS "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR} does not exist. Defaulting install location to ${CMAKE_INSTALL_PREFIX}/lib.")
	set(CMAKE_INSTALL_LIBDIR lib)
endif()

set(RUNTIME_DIR bin)
set(LIBRARY_DIR ${CMAKE_INSTALL_LIBDIR})
set(INCLUDE_DIR include fapi)
set(DOC_DIR "share/doc/${CPACK_PACKAGE_NAME}")
set(DATA_DIR share/${CPACK_PACKAGE_NAME})

########################################################################
# Compiler specific setup
########################################################################
macro(ADD_C_COMPILER_FLAG_IF_AVAILABLE flag have)
	include(CheckCCompilerFlag)
	check_c_compiler_flag(${flag} ${have})
	if(${have})
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${flag}")
	endif(${have})
endmacro(ADD_C_COMPILER_FLAG_IF_AVAILABLE)

MESSAGE( STATUS "CMAKE_SYSTEM_PROCESSOR: " ${CMAKE_SYSTEM_PROCESSOR} )
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "IntelLLVM")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pipe -Wall -Wno-comment -Wno-reorder -Wno-unused-but-set-variable -Wno-unused-variable -Werror=return-type")
	SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DB4860 -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -std=gnu++0x")

	if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -DDEBUG_MODE")
	elseif(${CMAKE_BUILD_TYPE} STREQUAL "RelWithDebInfo")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Ofast -ffast-math -fno-math-errno -funroll-loops")
	else()
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Ofast -ffast-math -fno-math-errno -funroll-loops")
	endif()
endif(CMAKE_CXX_COMPILER_ID MATCHES "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "IntelLLVM")

ADD_C_COMPILER_FLAG_IF_AVAILABLE("-Werror=incompatible-pointer-types" HAVE_ERROR_INCOMPATIBLE)

if(CMAKE_C_COMPILER_ID MATCHES "GNU" OR CMAKE_C_COMPILER_ID MATCHES "Clang" OR CMAKE_C_COMPILER_ID MATCHES "IntelLLVM")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pipe -Wall -Wno-comment -Wno-write-strings -Wno-format-extra-args -Winline -Wno-unused-result -Wno-format -Werror=return-type -D_GNU_SOURCE")
	SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DB4860 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE")

	if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O0 -DDEBUG_MODE")
	elseif(${CMAKE_BUILD_TYPE} STREQUAL "RelWithDebInfo")
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -Ofast -ffast-math -fno-math-errno -funroll-loops")
	else()
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Ofast -ffast-math -fno-math-errno -funroll-loops")
	endif()

	if(NOT WIN32)
		ADD_C_COMPILER_FLAG_IF_AVAILABLE(-fvisibility=hidden HAVE_VISIBILITY_HIDDEN)
	endif(NOT WIN32)
endif(CMAKE_C_COMPILER_ID MATCHES "GNU" OR CMAKE_C_COMPILER_ID MATCHES "Clang" OR CMAKE_C_COMPILER_ID MATCHES "IntelLLVM")

# Symbols visibility is hidden by default
cmake_policy(SET CMP0063 NEW)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_C_VISIBILITY_PRESET hidden)

message(STATUS "CMAKE_C_FLAGS is ${CMAKE_C_FLAGS}")
message(STATUS "CMAKE_CXX_FLAGS is ${CMAKE_CXX_FLAGS}")

########################################################################
# Add -fPIC property to all targets
########################################################################
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

########################################################################
# Print summary
########################################################################
message(STATUS "Using install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "Building for version: ${VERSION}")

#HEADERS#
include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/fapi)
include_directories(${PROJECT_SOURCE_DIR}/asn1)
include_directories(${PROJECT_SOURCE_DIR}/lib)

add_subdirectory(asn1)
add_subdirectory(b4860)

add_library(fapi_b4860_v1 SHARED
		"fapi_scheduler.c"
		${FAPI_SOURCES}
		${PROJECT_SOURCE_DIR}/lib/core/block_queue.c
		${PROJECT_SOURCE_DIR}/lib/core/thread_helper.c
		${PROJECT_SOURCE_DIR}/lib/core/time.c
		${PROJECT_SOURCE_DIR}/lib/core/file.c
		${PROJECT_SOURCE_DIR}/lib/core/log.c
		$<TARGET_OBJECTS:b4860>
		$<TARGET_OBJECTS:b4860ipc>
)

set_target_properties(fapi_b4860_v1 PROPERTIES
		LINK_FLAGS "-Wl,--export-dynamic")

target_link_libraries(fapi_b4860_v1
		asn1_common
)

ADD_CUSTOM_COMMAND(TARGET fapi_b4860_v1
		POST_BUILD
		COMMAND   ${CMAKE_STRIP} --strip-all --discard-all *.so )


if (ENABLE_L2_FAPI_GEN_4G)
	message(STATUS "Enable L2 FAPI 4G Generator")
	add_definitions(-DL2_FAPI_GENERATOR)
	add_subdirectory(l2_fapi_generator)
endif()
	