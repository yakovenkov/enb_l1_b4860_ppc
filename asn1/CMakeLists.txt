file(GLOB ASN1_COMMON_SOURCES "*.c")
add_library(asn1_common ${ASN1_COMMON_SOURCES})

target_link_libraries(asn1_common "-lm")