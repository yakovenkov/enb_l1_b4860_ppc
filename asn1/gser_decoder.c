#include <asn_application.h>
#include <asn_internal.h>
#include <gser_support.h>		/* XER/XML parsing support */
#include <gser_decoder.h>

int gser_decoder_trace = 0;

char* gser_decoder_get_error_str()
{
	return gser_decoder_error_str;
}

/*
 * Decode the XER encoding of a given type.
 */
asn_dec_rval_t
gser_decode(const asn_codec_ctx_t *opt_codec_ctx,
           const asn_TYPE_descriptor_t *td, void **struct_ptr,
           const void *buffer, size_t size) {
    asn_codec_ctx_t s_codec_ctx;

	gser_decoder_str = (char*)buffer;
	gser_decoder_is_error_str_filled = 0;
	snprintf(gser_decoder_error_str, GSER_DECODER_ERROR_STR_LEN, "Unspecified");
	
	/*
	 * Stack checker requires that the codec context
	 * must be allocated on the stack.
	 */
	if(opt_codec_ctx) {
		if(opt_codec_ctx->max_stack_size) {
			s_codec_ctx = *opt_codec_ctx;
			opt_codec_ctx = &s_codec_ctx;
		}
	} else {
		/* If context is not given, be security-conscious anyway */
		memset(&s_codec_ctx, 0, sizeof(s_codec_ctx));
		s_codec_ctx.max_stack_size = ASN__DEFAULT_STACK_MAX;
		opt_codec_ctx = &s_codec_ctx;
	}
	size_t consumed_self = 0;
	asn_dec_rval_t error_rv;
	error_rv.code = RC_FAIL;

	char* p = (char *)buffer;
	p += gser_whitespace_span(p, size);

	consumed_self = ((p - (char*)buffer));
	size_t bytes_to_parse_left = size - consumed_self;
	
	asn_dec_rval_t rv = td->op->gser_decoder(opt_codec_ctx, td, struct_ptr, 0, p, bytes_to_parse_left);
	if (rv.code == RC_OK)
	{
		rv.consumed += consumed_self;
		return rv;
	}

	error_rv.consumed = (p - (char*)buffer);
	return error_rv;
}

static uint8_t b[1024];
static int token_idx = 0;
static void debug_token(gser_token_e type, const void* _data, size_t _size)
{
	memset(b, 0, sizeof(b));
	memcpy(b, _data, _size);
	token_idx++;
	GSER_PRINTF("token[%i] %s , len %i,  [ %s ]\n", token_idx, gser_get_token_name(type), _size, b);
}

/*
 * Fetch the next token from the XER/XML stream.
 */
ssize_t
gser_next_token_debug(int *state_context, const void *buffer, size_t size, gser_token_e *token, const char* file, int line) 
{
	ssize_t rv = gser_parse(state_context, buffer, size, token);
	//GSER_PRINTF("GSER_NEXT_TOKEN %s : %i\n", file, line);
	if (size > 0)
		debug_token(*token, buffer, rv);
	return rv;
}

#undef	ADVANCE
#define	ADVANCE(num_bytes)	do {				\
		size_t num = (num_bytes);			\
		buf_ptr = ((const char *)buf_ptr) + num;	\
		size -= num;					\
		consumed_myself += num;				\
	} while(0)

#undef	RETURN
#define	RETURN(_code)	do {					\
		rval.code = _code;				\
		rval.consumed = consumed_myself;		\
		if(rval.code != RC_OK)				\
			ASN_DEBUG("Failed with %d", rval.code);	\
		return rval;					\
	} while(0)

#define	GSER_GOT_BODY(chunk_buf, chunk_size, size)	do {	\
		ssize_t converted_size = body_receiver		\
			(struct_key, chunk_buf, chunk_size,	\
				(size_t)chunk_size < size);	\
		if(converted_size == -1) RETURN(RC_FAIL);	\
		if(converted_size == 0				\
			&& size == (size_t)chunk_size)		\
			RETURN(RC_WMORE);			\
		chunk_size = converted_size;			\
	} while(0)
#define	GSER_GOT_EMPTY()	do {					\
	if(body_receiver(struct_key, 0, 0, size > 0) == -1)	\
			RETURN(RC_FAIL);			\
	} while(0)

/*
 * Generalized function for decoding the primitive values.
 */
asn_dec_rval_t
gser_decode_general(const asn_codec_ctx_t *opt_codec_ctx,
	asn_struct_ctx_t *ctx,	/* Type decoder context */
	void *struct_key,
	const char *xml_tag,	/* Expected XML tag */
	const void *buf_ptr, size_t size,
	int (*opt_unexpected_tag_decoder)
		(void *struct_key, const void *chunk_buf, size_t chunk_size),
	ssize_t (*body_receiver)
		(void *struct_key, const void *chunk_buf, size_t chunk_size,
			int have_more)
	) {

	asn_dec_rval_t rval;
	ssize_t consumed_myself = 0;

	(void)opt_codec_ctx;

	for(;;) {
		gser_token_e token;
		ssize_t token_size;

		token_size = 0;
		do
		{
			ADVANCE(token_size);
			token_size = gser_next_token(&ctx->context, buf_ptr, size, &token);
		} while(token_size > 0 && (token == GSER_TOKEN_SPACE));

		if (token_size < 0)
			RETURN(RC_FAIL);

		if (token == GSER_TOKEN_TEXT || 
		    token == GSER_TOKEN_BSTRING ||
			token == GSER_TOKEN_HSTRING ||
			token == GSER_TOKEN_PRINTABLE_STRING)
		{
			GSER_GOT_BODY(buf_ptr, token_size, size);
			ADVANCE(token_size);
			RETURN(RC_OK);
		}
		if (token == GSER_TOKEN_CLOSE_BRACKET)
		{
			//Пустое подмножество, выходим из процедуры
			ADVANCE(token_size);
			RETURN(RC_EMPTY);
		}
		else
		{
			//invalid token
			RETURN(RC_FAIL); 
		}
	}

	RETURN(RC_FAIL);
}


size_t
gser_whitespace_span(const void *chunk_buf, size_t chunk_size) {
	const char *p = (const char *)chunk_buf;
	const char *pend = p + chunk_size;

	for(; p < pend; p++) {
		switch(*p) {
		/* X.693, #8.1.4
		 * HORISONTAL TAB (9)
		 * LINE FEED (10) 
		 * CARRIAGE RETURN (13) 
		 * SPACE (32)
		 */
		case 0x09: case 0x0a: case 0x0d: case 0x20:
			continue;
		default:
			break;
		}
		break;
	}
	return (p - (const char *)chunk_buf);
}