#ifndef ASN1_COMMON_GSER_TOKENS_H
#define ASN1_COMMON_GSER_TOKENS_H

typedef enum gser_token {
	GSER_TOKEN_OPEN_BRACKET,
	GSER_TOKEN_CLOSE_BRACKET,
	GSER_TOKEN_SPACE,
	GSER_TOKEN_TEXT,
	GSER_TOKEN_COLON,
	GSER_TOKEN_COMMA,
	GSER_TOKEN_BSTRING,
	GSER_TOKEN_HSTRING,
	GSER_TOKEN_PRINTABLE_STRING,
	GSER_NUM_OF_TOKENS,
} gser_token_e;

const char* gser_get_token_name(gser_token_e token);
int gser_get_line(const char* msg_start, const char* msg_current_pos);

#endif /* ASN1_COMMON_GSER_TOKENS_H */
