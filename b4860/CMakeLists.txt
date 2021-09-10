include_directories("${CMAKE_CURRENT_SOURCE_DIR}/libb4860/ipc/include")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/libb4860/kernel")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/libb4860/fsl_shm/include")

include_directories("${CMAKE_SOURCE_DIR}")

add_subdirectory(libb4860)

asn1_add_module_library(fapi_b4860_config_asn1
	ASN1C_OPTIONS -R -pdu=all -fcompound-names -gen-PER -no-gen-OER -no-gen-example
	GLOBAL_TARGET
	MODULES asn1/fapi_b4860_config.asn)

set(B4860_SOURCES
	"${CMAKE_CURRENT_SOURCE_DIR}/fapi_b4860.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/fapi_b4860_cpri_eth.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/fapi_b4860_cfg.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/p8.c"
	)

add_custom_target(b4860_version_header ALL
 	COMMAND ${CMAKE_COMMAND}
 	-Dlocal_dir="${CMAKE_CURRENT_SOURCE_DIR}"
	-Dtemplate_in="${CMAKE_SOURCE_DIR}/cmake/modules/build_version.h.in"
	-Dtemplate_out="${CMAKE_CURRENT_BINARY_DIR}/fapi_b4860_v1_version.h"
	-Dprefix="FAPI_LIB_"
	-P "${CMAKE_SOURCE_DIR}/cmake/modules/BuildVersion.cmake"
)

if(ENABLE_B4860_DIRECT_LOG)
	add_definitions(-DENABLE_B4860_DIRECT_LOG)
endif()

if(ENABLE_CPRI_ETH_FDD)
	add_definitions(-DENABLE_CPRI_ETH_FDD)
endif()

add_library(b4860
	${B4860_SOURCES}
	$<TARGET_OBJECTS:b4860ipc>
	)

message("FAPI Version include dir: ${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(b4860 PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

add_dependencies(b4860 b4860ipc ASN1::fapi_b4860_config_asn1)
add_dependencies(b4860 b4860_version_header)

target_link_libraries(b4860
	b4860ipc
	ASN1::fapi_b4860_config_asn1
	)

set(FAPI_LIBRARIES
	b4860
	PARENT_SCOPE
	)
