#ifndef	_GSER_ENCODER_H_
#define	_GSER_ENCODER_H_

#include <asn_application.h>

#ifdef __cplusplus
extern "C" {
#endif

struct asn_TYPE_descriptor_s;	/* Forward declaration */

/* Flags used by the gser_encode() and (*gser_type_encoder_f), defined below */
enum gser_encoder_flags_e {
	/* Mode of encoding */
	GSER_F_BASIC	= 0x01,	/* BASIC-XER (pretty-printing) */
	GSER_F_CANONICAL	= 0x02	/* Canonical XER (strict rules) */
};

/*
 * The XER encoder of any type. May be invoked by the application.
 * Produces CANONICAL-XER and BASIC-XER depending on the (gser_flags).
 */
asn_enc_rval_t gser_encode(const struct asn_TYPE_descriptor_s *type_descriptor,
                          const void *struct_ptr, /* Structure to be encoded */
                          enum gser_encoder_flags_e gser_flags,
                          asn_app_consume_bytes_f *consume_bytes_cb,
                          void *app_key /* Arbitrary callback argument */
);

/*
 * The variant of the above function which dumps the BASIC-XER (XER_F_BASIC)
 * output into the chosen file pointer.
 * RETURN VALUES:
 * 	 0: The structure is printed.
 * 	-1: Problem printing the structure.
 * WARNING: No sensible errno value is returned.
 */
int gser_fprint(FILE *stream, const struct asn_TYPE_descriptor_s *td,
               const void *struct_ptr);

/*
 * A helper function that uses XER encoding/decoding to verify that:
 * - Both structures encode into the same BASIC XER.
 * - Both resulting XER byte streams can be decoded back.
 * - Both decoded structures encode into the same BASIC XER (round-trip).
 * All of this verifies equivalence between structures and a round-trip.
 * ARGUMENTS:
 *  (opt_debug_stream)  - If specified, prints ongoing details.
 */
enum gser_equivalence_e {
    GSER_EQ_SUCCESS,          /* The only completely positive return value */
    GSER_EQ_FAILURE,          /* General failure */
    GSER_EQ_ENCODE1_FAILED,   /* First sructure XER encoding failed */
    GSER_EQ_ENCODE2_FAILED,   /* Second structure XER encoding failed */
    GSER_EQ_DIFFERENT,        /* Structures encoded into different XER */
    GSER_EQ_DECODE_FAILED,    /* Decode of the XER data failed */
    GSER_EQ_ROUND_TRIP_FAILED /* Bad round-trip */
};
enum gser_equivalence_e gser_equivalent(
    const struct asn_TYPE_descriptor_s *type_descriptor, const void *struct1,
    const void *struct2, FILE *opt_debug_stream);

/*
 * Type of the generic XER encoder.
 */
typedef asn_enc_rval_t(gser_type_encoder_f)(
    const struct asn_TYPE_descriptor_s *type_descriptor,
    const void *struct_ptr, /* Structure to be encoded */
    int ilevel,             /* Level of indentation */
    enum gser_encoder_flags_e gser_flags,
    asn_app_consume_bytes_f *consume_bytes_cb, /* Callback */
    void *app_key                              /* Arbitrary callback argument */
);

#ifdef __cplusplus
}
#endif

#endif	/* _XER_ENCODER_H_ */
