#include "gser_tokens.h"

const char gser_token_as_str[][32] = 
{
    "GSER_TOKEN_OPEN_BRACKET",
    "GSER_TOKEN_CLOSE_BRACKET",
    "GSER_TOKEN_SPACE",
	"GSER_TOKEN_TEXT", 
	"GSER_TOKEN_COLON",
	"GSER_TOKEN_COMMA",
	"GSER_TOKEN_BSTRING",
	"GSER_TOKEN_HSTRING",
	"GSER_TOKEN_PRINTABLE_STRING"
};

const char* gser_get_token_name(gser_token_e token)
{
	if (token < GSER_NUM_OF_TOKENS)
		return gser_token_as_str[token];
	else
		return "Undefined\n";
}

int gser_get_line(const char* msg_start, const char* msg_current_pos)
{
	int line = 1;
	while (msg_start != msg_current_pos && *msg_start != 0)
	{
		if (*msg_start == '\n')
			line++;
		msg_start++;
	}
	return line;
}