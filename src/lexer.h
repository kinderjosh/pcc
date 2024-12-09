#ifndef LEXER_H
#define LEXER_H

#include "token.h"
#include <stdio.h>

typedef struct {
    char *file;
    char *src;
    char ch;
    size_t src_len;
    size_t pos;
    size_t ln;
    size_t col;
} Lex;

Lex *lex_init(char *file);
void lex_del(Lex *lex);
Tok *lex_next(Lex *lex);

#endif