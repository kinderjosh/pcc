#include "lexer.h"
#include "token.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

Lex *lex_init(char *file) {
    FILE *f = fopen(file, "r");
    if (f == NULL) {
        fprintf(stderr, "%s: Error: No such file exists.\n", file);
        exit(EXIT_FAILURE);
    }

    char *src = calloc(1, sizeof(char));
    char c;
    size_t len = 0;

    while ((c = fgetc(f)) != EOF) {
        src[len++] = c;
        src = realloc(src, (len + 1) * sizeof(char));
    }

    src[len] = '\0';
    fclose(f);

    Lex *lex = malloc(sizeof(Lex));
    lex->file = file;
    lex->src = src;
    lex->src_len = len;
    lex->ch = src[0];
    lex->pos = 0;
    lex->ln = lex->col = 1;
    return lex;
}

void lex_del(Lex *lex) {
    free(lex->src);
    free(lex);
}

void lex_step(Lex *lex) {
    if (lex->pos >= lex->src_len)
        return;

    if (lex->ch == '\n') {
        lex->ln++;
        lex->col = 1;
    } else
        lex->col++;

    lex->ch = lex->src[++lex->pos];
}

char lex_peek(Lex *lex, int offset) {
    if (lex->pos + offset >= lex->src_len)
        return lex->src[lex->src_len - 1];
    else if ((int)lex->pos + offset < 1)
        return lex->src[0];
    
    return lex->src[lex->pos + offset];
}

Tok *lex_step_with(Lex *lex, TokType type, char *value) {
    Tok *tok = tok_init(type, strdup(value), lex->ln, lex->col);

    for (size_t i = 0; i < strlen(value); i++)
        lex_step(lex);

    return tok;
}

Tok *lex_next(Lex *lex) {
    while (isspace(lex->ch))
        lex_step(lex);

    if (lex->ch == '/' && lex_peek(lex, 1) == '*') {
        lex_step(lex);
        lex_step(lex);

        while (lex->ch != '\0' && (lex->ch != '*' || lex_peek(lex, 1) != '/'))
            lex_step(lex);

        lex_step(lex);
        lex_step(lex);
        return lex_next(lex);
    }

    char *value;
    size_t len = 0;

    if (isalpha(lex->ch) || lex->ch == '_') {
        value = calloc(1, sizeof(char));

        while (isalpha(lex->ch) || lex->ch == '_' || isdigit(lex->ch)) {
            value[len++] = lex->ch;
            value = realloc(value, (len + 1) * sizeof(char));
            lex_step(lex);
        }

        value[len] = '\0';
        return tok_init(TOK_ID, value, lex->ln, lex->col - len);
    } else if (isdigit(lex->ch) || (lex->ch == '-' && isdigit(lex_peek(lex, 1)))) {
        value = calloc(1, sizeof(char));
        bool is_float = false;

        while (isdigit(lex->ch) || (lex->ch == '-' && len < 1) || (lex->ch == '.' && !is_float && isdigit(lex_peek(lex, 1)))) {
            if (lex->ch == '.')
                is_float = true;

            value[len++] = lex->ch;
            value = realloc(value, (len + 1) * sizeof(char));
            lex_step(lex);
        }

        value[len] = '\0';
        return tok_init(is_float ? TOK_FLOAT : TOK_INT, value, lex->ln, lex->col - len);
    } else if (lex->ch == '\'') {
        size_t col = lex->col;
        lex_step(lex);
        char *value = calloc(32, sizeof(char));

        if (lex->ch == '\\') {
            lex_step(lex);

            switch (lex->ch) {
                case 'n':
                    strcpy(value, "10");
                    break;
                case 't':
                    strcpy(value, "9");
                    break;
                case 'r':
                    strcpy(value, "13");
                    break;
                case '0':
                    strcpy(value, "0");
                    break;
                case '\'':
                case '"':
                case '\\':
                    sprintf(value, "%d", (int)lex->ch);
                    break;
                default:
                    fprintf(stderr, "%s:%zu:%zu: Error: Unsupported escape sequence '\\%c'.\n", lex->file, lex->ln, col, lex->ch);
                    exit(EXIT_FAILURE);
            }
        } else
            sprintf(value, "%d", (int)lex->ch);

        lex_step(lex);

        if (lex->ch != '\'') {
            fprintf(stderr, "%s:%zu:%zu: Error: Unclosed character constant.\n", lex->file, lex->ln, col);
            exit(EXIT_FAILURE);
        }

        lex_step(lex);
        return tok_init(TOK_INT, value, lex->ln, lex->col);
    }

    switch (lex->ch) {
        case '(': return lex_step_with(lex, TOK_LPAREN, "(");
        case ')': return lex_step_with(lex, TOK_RPAREN, ")");
        case '{': return lex_step_with(lex, TOK_LBRACE, "{");
        case '}': return lex_step_with(lex, TOK_RBRACE, "}");
        case ';': return lex_step_with(lex, TOK_SEMI, ";");
        case ',': return lex_step_with(lex, TOK_COMMA, ",");
        case '=': return lex_step_with(lex, TOK_EQUAL, "=");
        case '+': return lex_step_with(lex, TOK_PLUS, "+");
        case '-': return lex_step_with(lex, TOK_MINUS, "-");
        case '*': return lex_step_with(lex, TOK_STAR, "*");
        case '/': return lex_step_with(lex, TOK_SLASH, "/");
        case '%': return lex_step_with(lex, TOK_PERCENT, "%%");
        case '\0': return tok_init(TOK_EOF, strdup("<eof>"), lex->ln, lex->col);
        default: break;
    }

    fprintf(stderr, "%s:%zu:%zu: Error: Unknown character '%c'.\n", lex->file, lex->ln, lex->col, lex->ch);
    exit(EXIT_FAILURE);
}