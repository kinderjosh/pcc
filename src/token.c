#include "token.h"
#include <stdio.h>
#include <stdlib.h>

const char *tok_types[] = {
    [TOK_EOF] = "<eof>",
    [TOK_ID] = "identifier",
    [TOK_INT] = "int",
    [TOK_FLOAT] = "float",
    [TOK_LPAREN] = "left paren",
    [TOK_RPAREN] = "right paren",
    [TOK_LBRACE] = "left brace",
    [TOK_RBRACE] = "right brace",
    [TOK_SEMI] = "semicolon",
    [TOK_COMMA] = "comma",
    [TOK_EQUAL] = "equal",
    [TOK_PLUS] = "plus",
    [TOK_MINUS] = "minus",
    [TOK_STAR] = "star",
    [TOK_SLASH] = "slash",
    [TOK_PERCENT] = "percent",
    [TOK_PLUS_EQ] = "plus equal",
    [TOK_MINUS_EQ] = "minus equal",
    [TOK_STAR_EQ] = "star equal",
    [TOK_SLASH_EQ] = "slash equal",
    [TOK_PERCENT_EQ] = "percent equal",
    [TOK_LT] = "less than",
    [TOK_LTE] = "less than or equal",
    [TOK_GT] = "greater than",
    [TOK_GTE] = "greater than or equal",
    [TOK_NOT_EQ] = "not equal",
    [TOK_EQ_EQ] = "equal equal",
    [TOK_AND] = "and",
    [TOK_OR] = "or",
    [TOK_LSQUARE] = "left square",
    [TOK_RSQUARE] = "right square",
    [TOK_STR] = "string",
    [TOK_AMP] = "ampersand"
};

Tok *tok_init(TokType type, char *value, size_t ln, size_t col) {
    Tok *tok = malloc(sizeof(Tok));
    tok->type = type;
    tok->value = value;
    tok->ln = ln;
    tok->col = col;
    return tok;
}

void tok_del(Tok *tok) {
    free(tok->value);
    free(tok);
}