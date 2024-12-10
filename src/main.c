#include "parser.h"
#include "ast.h"
#include "emit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <dirent.h>

void test(char *file) {
    AST *root = prs_file(file);
    free(emit_ast(root));
    ast_del(root);
}

int main(int argc, char **argv) {
    char *file = NULL;
    char *out = "a.out";
    char *test_dir = NULL;
    bool assemble = true;
    bool link = true;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options...] <input file>\n"
                   "Options:\n"
                   "  -c                   Output only object files.\n"
                   "  -o <output file>     Place the output into <output file>.\n"
                   "  -t <test directory>  (Development only) Test each file in <test directory>.\n"
                   "  -S                   Output only assembly files.\n", argv[0]);
            return EXIT_SUCCESS;
        } else if (strcmp(argv[i], "-c") == 0)
            link = false;
        else if (strcmp(argv[i], "-o") == 0) {
            if (i == argc - 1) {
                fprintf(stderr, "pcc: Error: Missing argument <output file> to option '-o'.\n");
                return EXIT_FAILURE;
            }

            out = argv[++i];
        } else if (strcmp(argv[i], "-t") == 0) {
            if (i == argc - 1) {
                fprintf(stderr, "pcc: Error: Missing argument <test directory> to option '-t'.\n");
                return EXIT_FAILURE;
            }

            test_dir = argv[++i];
        } else if (strcmp(argv[i], "-S") == 0)
            assemble = link = false;
        else if (i == argc - 1 && test_dir == NULL)
            file = argv[i];
        else {
            fprintf(stderr, "pcc: Error: Unknown argument '%s'.\n", argv[i]);
            return EXIT_FAILURE;
        }
    }

    if (file == NULL && test_dir == NULL) {
        fprintf(stderr, "pcc: Error: Missing argument <input file>.\n");
        return EXIT_FAILURE;
    } else if (test_dir != NULL) {
        if (test_dir[strlen(test_dir) - 1] == '/')
            test_dir[strlen(test_dir) - 1] = '\0';

        DIR *dr = opendir(test_dir);
        if (dr == NULL) {
            fprintf(stderr, "pcc: Error: Failed to open test directory '%s'.\n", test_dir);
            return EXIT_FAILURE;
        }

        struct dirent *de;
        char *path = calloc(1, sizeof(char));

        while ((de = readdir(dr)) != NULL) {
            if (strcmp(de->d_name, "..") == 0 || strcmp(de->d_name, ".") == 0)
                continue;

            path = realloc(path, (strlen(test_dir) + strlen(de->d_name) + 2) * sizeof(char));
            sprintf(path, "%s/%s", test_dir, de->d_name);

            printf("Testing '%s'...\n", path);
            test(path);
            printf("Test passed.\n");
        }

        free(path);
        closedir(dr);
        return EXIT_SUCCESS;
    }

    AST *root = prs_file(file);
    char *code = emit_ast(root);
    ast_del(root);

    char *outasm;
    char *outbase;

    if (strchr(file, '/') != NULL) {
        char *filecpy = strdup(file);
        char *tok = strtok(filecpy, "/");
        char *prev_tok = tok;

        while (tok != NULL) {
            prev_tok = tok;
            tok = strtok(NULL, "/");
        }

        outbase = strdup(prev_tok);
        free(filecpy);
    } else
        outbase = strdup(file);

    if (strchr(outbase, '.') != NULL) {
        char *tok = strtok(outbase, ".");

        outasm = calloc(strlen(tok) + 5, sizeof(char));
        sprintf(outasm, "%s.asm", tok);
    } else {
        outasm = calloc(strlen(file) + 5, sizeof(char));
        sprintf(outasm, "%s.asm", outbase);
    }

    FILE *f = fopen(outasm, "w");
    if (f == NULL) {
        fprintf(stderr, "%s: Error: Failed to write to file '%s'.\n", file, outasm);
        return EXIT_FAILURE;
    }

    fputs(code, f);
    fclose(f);
    free(code);

    if (!assemble) {
        free(outasm);
        free(outbase);
        return EXIT_SUCCESS;
    }

    char *nasm = calloc((strlen(outasm) * 2) + strlen(outbase) + 64, sizeof(char));
    sprintf(nasm, "nasm -felf64 %s -o %s.o && rm %s", outasm, outbase, outasm);

    if (system(nasm) != 0) {
        fprintf(stderr, "%s: Error: Failed to assemble.\n", file);
        exit(EXIT_FAILURE);
    }

    if (!link) {
        free(nasm);
        free(outasm);
        free(outbase);
        return EXIT_SUCCESS;
    }

    nasm = realloc(nasm, ((strlen(outbase) * 2) + strlen(out) + 64) * sizeof(char));
    sprintf(nasm, "ld -emain_ %s.o -o %s && rm %s.o", outbase, out, outbase);

    if (system(nasm) != 0) {
        fprintf(stderr, "%s: Error: Failed to link.\n", file);
        exit(EXIT_FAILURE);
    }

    free(nasm);
    free(outasm);
    free(outbase);
    return EXIT_SUCCESS;
}
