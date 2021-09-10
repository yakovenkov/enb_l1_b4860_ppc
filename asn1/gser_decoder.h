
#ifndef	_GSER_DECODER_H_
#define	_GSER_DECODER_H_

#include <asn_application.h>
#include <gser_tokens.h>

#ifdef __cplusplus
extern "C" {
#endif

struct asn_TYPE_descriptor_s;	/* Forward declaration */

char* gser_decoder_get_error_str(void);

/*
 * The XER decoder of any ASN.1 type. May be invoked by the application.
 * Decodes CANONICAL-XER and BASIC-XER.
 */
asn_dec_rval_t gser_decode(
    const struct asn_codec_ctx_s *opt_codec_ctx,
    const struct asn_TYPE_descriptor_s *type_descriptor,
    void **struct_ptr,  /* Pointer to a target structure's pointer */
    const void *buffer, /* Data to be decoded */
    size_t size         /* Size of data buffer */
);

/*
 * Type of the type-specific XER decoder function.
 */
typedef asn_dec_rval_t(gser_type_decoder_f)(
    const asn_codec_ctx_t *opt_codec_ctx,
    const struct asn_TYPE_descriptor_s *type_descriptor, void **struct_ptr,
    const char *opt_mname, /* Member name */
    const void *buf_ptr, size_t size);

/*******************************
 * INTERNALLY USEFUL FUNCTIONS *
 *******************************/

/*
 * Generalized function for decoding the primitive values.
 * Used by more specialized functions, such as OCTET_STRING_decode_gser_utf8
 * and others. This function should not be used by applications, as its API
 * is subject to changes.
 */
asn_dec_rval_t gser_decode_general(
    const asn_codec_ctx_t *opt_codec_ctx,
    asn_struct_ctx_t *ctx, /* Type decoder context */
    void *struct_key,      /* Treated as opaque pointer */
    const char *xml_tag,   /* Expected XML tag name */
    const void *buf_ptr, size_t size,
    int (*opt_unexpected_tag_decoder)(void *struct_key, const void *chunk_buf,
                                      size_t chunk_size),
    ssize_t (*body_receiver)(void *struct_key, const void *chunk_buf,
                             size_t chunk_size, int have_more));



#define gser_next_token(state,buffer,size,token) gser_next_token_debug(state,buffer,size,token, __FILE__, __LINE__)
ssize_t
gser_next_token_debug(int *state_context, const void *buffer, size_t size, gser_token_e *token, const char* file, int line);

/*
 * Get the number of bytes consisting entirely of XER whitespace characters.
 * RETURN VALUES:
 * >=0:	Number of whitespace characters in the string.
 */
size_t gser_whitespace_span(const void *chunk_buf, size_t chunk_size);

#ifdef __cplusplus
}
#endif

#endif	/* _XER_DECODER_H_ */
