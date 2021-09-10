#include <asn_system.h>
#include "gser_support.h"

#include <stdbool.h>

static const int
_charclass[256] = {
	0,0,0,0,0,0,0,0, 0,1,1,0,1,1,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	1,0,0,0,0,0,0,0, 0,0,0,0,0,0,3,0,
	2,2,2,2,2,2,2,2, 2,2,0,0,0,0,0,0,	/* 01234567 89       */
	0,3,3,3,3,3,3,3, 3,3,3,3,3,3,3,3,	/*  ABCDEFG HIJKLMNO */
	3,3,3,3,3,3,3,3, 3,3,3,0,0,0,0,0,	/* PQRSTUVW XYZ      */
	0,3,3,3,3,3,3,3, 3,3,3,3,3,3,3,3,	/*  abcdefg hijklmno */
	3,3,3,3,3,3,3,3, 3,3,3,0,0,0,0,0	/* pqrstuvw xyz      */
};
#define WHITESPACE(c)	(_charclass[(unsigned char)(c)] == 1)
#define ALNUM(c)	(_charclass[(unsigned char)(c)] >= 2)
#define ALPHA(c)	(_charclass[(unsigned char)(c)] == 3)

/* Aliases for characters, ASCII/UTF-8 */
#define	CQUOTE	           0x22	/* '"' */
#define	CDASH	           0x2d	/* '-' */
#define CHAR_SQUOTE        0x27 /* ''' */
#define	CHAR_OPEN_BRACKET  0x7b	/* '{' */
#define	CHAR_CLOSE_BRACKET 0x7d	/* '}' */
#define CHAR_COLON         0x3A /* ':' */
#define CHAR_COMMA         0x2C /* ',' */

/* Parser states */
typedef enum {
	STATE_INITIAL,
	STATE_TEXT,
	STATE_WHITESPACE,
	STATE_STRING,
	STATE_STRING_END,
	STATE_COMMENT
} pstate_e;

ssize_t gser_parse(int *state_context, const void *gserbuf, size_t size, gser_token_e* token) 
{
	pstate_e state = (pstate_e)*state_context;
	pstate_e comment_state;
	const char *chunk_start = (const char *)gserbuf;
	const char *p = chunk_start;
	const char *end = p + size;

	bool is_error_occured = false;
	bool is_token_found = false;
	bool last_char_is_token = false;
	
	if(p == end)
		return 0;

	if (*p == '\0')
	{
		return 0;
	}

	if (p == end)
	{
		return 0;
	}

	static int token_id = 0;
	int start_quote = 0;

	for (; (p < end) && !is_token_found && !is_error_occured; p++)
	{
		int C = *(const unsigned char *)p;

		switch(state)
		{
		case STATE_INITIAL:
		{
			if (WHITESPACE(C))
			{
				state = STATE_WHITESPACE;
				*token = GSER_TOKEN_SPACE;
				last_char_is_token = true;
				break;
			}

			if(C == CDASH)
			{
				char cnext = *(p + 1);
				if(cnext == CDASH)
				{
					// Comment '--' found
					comment_state = state;
					state = STATE_COMMENT;
					break;
				}
			}

			if (ALNUM(C) || C == CDASH)
			{
				state = STATE_TEXT;
				break;
			}

			if  ((C == CHAR_SQUOTE) || (C == CQUOTE))
			{
				state = STATE_STRING;
				start_quote = C;
				break;
			}

			if (C == CHAR_OPEN_BRACKET)
			{
				last_char_is_token = true;
				is_token_found = true;
				*token = GSER_TOKEN_OPEN_BRACKET;
				break;
			}

			if (C == CHAR_CLOSE_BRACKET)
			{
				last_char_is_token = true;
				is_token_found = true;
				*token = GSER_TOKEN_CLOSE_BRACKET;
				break;
			}

			if (C == CHAR_COLON)
			{
				last_char_is_token = true;
				is_token_found = true;
				*token = GSER_TOKEN_COLON;
				break;
			}

			if (C == CHAR_COMMA)
			{
				last_char_is_token = true;
				is_token_found = true;
				*token = GSER_TOKEN_COMMA;
				break;
			}
		}

		case STATE_WHITESPACE:
			if(!WHITESPACE(C))
			{
				/* Check for consecutive comment lines */
				if(C == CDASH)
				{
					char cnext = *(p + 1);
					if(cnext == CDASH)
					{
						// Comment '--' found
						comment_state = state;
						state = STATE_COMMENT;
						break;
					}
				}

				last_char_is_token = false;
				is_token_found = true;
				*token = GSER_TOKEN_SPACE;
				state = STATE_INITIAL;
			}
			else
			{
				last_char_is_token = true;
			}
			break;

		case STATE_TEXT:
			if (!(ALNUM(C) || C==CDASH))
			{
				last_char_is_token = false;
				is_token_found = true;
				*token = GSER_TOKEN_TEXT;
				state = STATE_INITIAL;
			}
			break;

		case STATE_STRING:
			if (C == start_quote)
			{
				state = STATE_STRING_END;
			}
			break;

		case STATE_STRING_END:
			if (C=='B')
			{
				last_char_is_token = true;
				is_token_found = true;
				*token = GSER_TOKEN_BSTRING;
				state = STATE_INITIAL;
			}
			else if (C == 'H')
			{
				last_char_is_token = true;
				is_token_found = true;
				*token = GSER_TOKEN_HSTRING;
				state = STATE_INITIAL;
			}
			else
			{
				last_char_is_token = false;
				is_token_found = true;
				*token = GSER_TOKEN_PRINTABLE_STRING;
				state = STATE_INITIAL;
			}
			break;

		case STATE_COMMENT:
			if(C == 0x0a || C == 0x0d)
			{
				//state = comment_state;
				last_char_is_token = true;
				is_token_found = true;
				*token = GSER_TOKEN_SPACE;
				state = STATE_INITIAL;
			}
			break;

		default:
			break;
		}
	}

	*state_context = (int)state;

	token_id++;

	if (is_error_occured)
	{
		return 0;
	} 
	else
	{
		size_t char_span = p - (const char *)gserbuf;
		return (last_char_is_token) ? (char_span) : (char_span - 1);
	}

}