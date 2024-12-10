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
    [TOK_COMMA] = "Comma",
    [TOK_EQUAL] = "Equal"
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