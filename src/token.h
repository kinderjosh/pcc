#ifndef TOKEN_H
#define TOKEN_H

#include <stdio.h>

typedef enum {
    TOK_EOF,
    TOK_ID,
    TOK_INT,
    TOK_FLOAT,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_SEMI,
    TOK_COMMA,
    TOK_EQUAL,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_PLUS_EQ,
    TOK_MINUS_EQ,
    TOK_STAR_EQ,
    TOK_SLASH_EQ,
    TOK_PERCENT_EQ,
    TOK_LT,
    TOK_LTE,
    TOK_GT,
    TOK_GTE,
    TOK_NOT_EQ,
    TOK_EQ_EQ,
    TOK_AND,
    TOK_OR
} TokType;

typedef struct {
    TokType type;
    char *value;
    size_t ln;
    size_t col;
} Tok;

Tok *tok_init(TokType type, char *value, size_t ln, size_t col);
void tok_del(Tok *tok);

#endif