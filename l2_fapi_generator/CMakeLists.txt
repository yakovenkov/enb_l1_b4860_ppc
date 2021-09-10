include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

include_directories(${PROJECT_SOURCE_DIR}/asn1/common)

file(GLOB PHY_SOURCES "phy/*.c")

set(SOURCES
	${PHY_SOURCES}
	l2_fapi_generator.c
	l2_fapi_generator_app.c
)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/asn1)
include_directories(${PROJECT_SOURCE_DIR}/l2_fapi_generator/include)
include_directories(${PROJECT_SOURCE_DIR}/lib/config/include)

asn1_add_module_library(l2_fapi_asn1_config
        ASN1C_OPTIONS -R -pdu=all -fcompound-names -gen-PER -no-gen-OER -no-gen-example
        MODULES asn1/l2_fapi_generator.asn)

add_executable(l2_fapi_generator_dl
        ${SOURCES}
)

target_link_libraries(l2_fapi_generator_dl
        ASN1::l2_fapi_asn1_config
        asn1_common
        fapi_b4860_v1
        "-lm -lpthread"
)
set(LINKER_FLAGS "-Wl -V")
set(LD_FLAGS ${LINKER_FLAGS})

if(NOT "${L2_FAPI_GENERATORS_INSTALL_DIR}" STREQUAL "")
        add_custom_command(TARGET l2_fapi_generator_dl POST_BUILD
        COMMAND cp -p ${CMAKE_CURRENT_BINARY_DIR}/l2_fapi_generator_dl "${L2_FAPI_GENERATORS_INSTALL_DIR}"
                )
endif()

