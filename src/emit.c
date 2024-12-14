#include "emit.h"
#include "ast.h"
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>

#define SUB_RSP_SIZE (size_t)32

extern const char *ast_types[];
extern AST **sym_tab;
extern size_t sym_cnt;
extern Const **constants;
extern size_t constants_cnt;

enum {
    RAX,
    RBX,
    RCX,
    RDX,
    RSI,
    RDI,
    RSP,
    RBP,
    R8,
    R9,
    XMM
};

enum {
    QWORD,
    DWORD,
    WORD,
    BYTE
};

const char *words[] = {
    [QWORD] = "qword",
    [DWORD] = "dword",
    [WORD] = "word",
    [BYTE] = "byte"
};

const char *regs[][16] = {
    [RAX] = { "rax", "eax", "ax", "al" },
    [RBX] = { "rbx", "ebx", "bx", "bl" },
    [RCX] = { "rcx", "ecx", "cx", "cl" },
    [RDX] = { "rdx", "edx", "dx", "dl" },
    [RSI] = { "rsi", "esi", "si", "sil" },
    [RDI] = { "rdi", "edi", "di", "dil" },
    [RSP] = { "rsp", "esp", "sp", "spl" },
    [RBP] = { "rbp", "ebp", "bp", "bpl" },
    [R8] = { "r8", "r8d", "r8w", "r8b" },
    [R9] = { "r9", "r9d", "r9w", "r9b" },
    [XMM] = { "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7", "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15" }
};

const size_t int_params[] = { RDI, RSI, RDX, RCX, R8, R9 };

size_t rsp = 0;
size_t rsp_cap = 0;
size_t float_cnt = 0;
size_t label_cnt = 0;
size_t str_cnt = 0;
char *func_data;
char *sect_data;
bool float_math_result = false;

void globs_reset() {
    rsp = rsp_cap = float_cnt = label_cnt = str_cnt = 0;
    func_data = realloc(func_data, 1 * sizeof(char));
    func_data[0] = '\0';
}

size_t float_init(size_t bits, long double value) {
    char *data = calloc(128, sizeof(char));

    if (bits == 32)
        sprintf(data, ".f%zu:\n"
                      "    dd %f\n", float_cnt, (float)value);
    else
        sprintf(data, ".f%zu:\n"
                      "    dq %Lf\n", float_cnt, value);

    func_data = realloc(func_data, (strlen(func_data) + strlen(data) + 1) * sizeof(char));
    strcat(func_data, data);
    free(data);
    return float_cnt++;
}

char *label_init() {
    char *label = calloc(32, sizeof(char));
    sprintf(label, ".l%zu", label_cnt++);
    return label;
}

size_t str_init(char *value) {
    char *data = calloc(1, sizeof(char));
    char *next;

    for (size_t i = 0; i < strlen(value); i++) {
        next = calloc(16, sizeof(char));

        if (value[i] == '\\') {
            i++;

            switch (value[i]) {
                case 'n':
                    strcpy(next, "10");
                    break;
                case 't':
                    strcpy(next, "9");
                    break;
                case 'r':
                    strcpy(next, "13");
                    break;
                case '0':
                    strcpy(next, "0");
                    break;
                case '\'':
                case '"':
                case '\\':
                    sprintf(next, "%d", (int)value[i]);
                    break;
                default: assert(false);
            }
        } else
            sprintf(next, "%d", (int)value[i]);

        strcat(next, ",");

        data = realloc(data, (strlen(data) + strlen(next) + 1) * sizeof(char));
        strcat(data, next);
        free(next);
    }

    char *str = calloc(strlen(data) + 64, sizeof(char));
    sprintf(str, ".s%zu:\n"
                 "    db %s0\n", str_cnt, data);
    free(data);

    func_data = realloc(func_data, (strlen(func_data) + strlen(str) + 1) * sizeof(char));
    strcat(func_data, str);
    free(str);
    return str_cnt++;
}

unsigned int power_of_two(unsigned int x) {
    if (x == 0)
        return -1;

    int n = 0;
    while (x > 1) {
        x >>= 1;
        n++;
    }

    return n;
}

bool is_power_of_two(unsigned int x) {
    return x != 0 && (x & (x - 1)) == 0;
}

char *emit_arr(AST **arr, size_t cnt, bool fix_rbp) {
    char *code = calloc(1, sizeof(char));
    char *next;
    size_t beg_rsp = rsp;
    size_t beg_cap = rsp_cap;

    for (size_t i = 0; i < cnt; i++) {
        next = emit_ast(arr[i]);
        code = realloc(code, (strlen(code) + strlen(next) + 1) * sizeof(char));
        strcat(code, next);
        free(next);
    }

    if (rsp != beg_rsp && fix_rbp) {
        char *temp = calloc(strlen(code) + 128, sizeof(char));
        sprintf(temp, "%s    add rsp, %zu\n", code, rsp - beg_rsp);
        free(code);
        code = temp;

        rsp = beg_rsp;
        rsp_cap = beg_cap;
    }

    return code;
}

char *emit_root(AST *ast) {
    char *code = strdup("    section .text\n"
                        "    global main_\n");
    char *next;

    func_data = calloc(1, sizeof(char));
    sect_data = calloc(1, sizeof(char));

    for (size_t i = 0; i < ast->root.asts_cnt; i++) {
        next = emit_ast(ast->root.asts[i]);
        code = realloc(code, (strlen(code) + strlen(next) + 1) * sizeof(char));
        strcat(code, next);
        free(next);
    }

    if (strlen(sect_data) > 1) {
        code = realloc(code, (strlen(code) + strlen(sect_data) + 32) * sizeof(char));
        strcat(code, "    section .data\n");
        strcat(code, sect_data);
    }

    free(func_data);
    free(sect_data);
    free(sym_tab);
    sym_cnt = 0;

    for (size_t i = 0; i < constants_cnt; i++) {
        ast_del(constants[i]->value);
        free(constants[i]->name);
        free(constants[i]);
    }

    constants_cnt = 0;

    return code;
}

char *emit_func(AST *ast) {
    const char template[] = "%s_:\n"
                            "    push rbp\n"
                            "    mov rbp, rsp\n"
                            "%s"
                            "%s"
                            "%s";

    char *params = calloc(1, sizeof(char));

    if (ast->func.params_cnt > 0) {
        AST *param;
        char *next;
        char *rbp;
        size_t ints = 0;
        size_t floats = 0;

        for (size_t i = 0; i < ast->func.params_cnt; i++) {
            param = ast->func.params[i];
            rbp = calloc(32, sizeof(char));
            next = calloc(128, sizeof(char));

            if (strcmp(param->assign.type, "char") == 0) {
                sprintf(rbp, "[rbp-%zu]", ++rsp);

                if (ints > 5)
                    sprintf(next, "    mov al, byte [rbp+%zu]\n"
                                  "    mov byte %s, al\n", (ints - 4) * 8, rbp);
                else
                    sprintf(next, "    mov byte %s, %s\n", rbp, regs[int_params[ints++]][BYTE]);
            } else if (strcmp(param->assign.type, "int") == 0) {
                rsp += 4;
                sprintf(rbp, "[rbp-%zu]", rsp);

                if (ints > 5)
                    sprintf(next, "    mov eax, dword [rbp+%zu]\n"
                                  "    mov dword %s, eax\n", (ints - 4) * 8, rbp);
                else
                    sprintf(next, "    mov dword %s, %s\n", rbp, regs[int_params[ints++]][DWORD]);
            } else if (strcmp(param->assign.type, "float") == 0) {
                rsp += 4;
                sprintf(rbp, "[rbp-%zu]", rsp);

                if (floats > 14)
                    sprintf(next, "    mov eax, dword [rbp+%zu]\n"
                                  "    mov dword %s, eax\n", (ints - 4) * 8, rbp);
                else
                    sprintf(next, "    movss dword %s, %s\n", rbp, regs[XMM][++floats]);
            } else {
                rsp += 8;
                sprintf(rbp, "[rbp-%zu]", rsp);

                if (ints > 5)
                    sprintf(next, "    mov rax, qword [rbp+%zu]\n"
                                  "    mov qword %s, rax\n", (ints - 4) * 8, rbp);
                else
                    sprintf(next, "    mov qword %s, %s\n", rbp, regs[int_params[ints++]][QWORD]);
            }

            param->assign.rbp = rbp;

            params = realloc(params, (strlen(params) + strlen(next) + 1) * sizeof(char));
            strcat(params, next);
            free(next);
        }

        while (rsp > rsp_cap)
            rsp_cap += SUB_RSP_SIZE;

        char *temp = calloc(strlen(params) + 64, sizeof(char));
        sprintf(temp, "    sub rsp, %zu\n%s", rsp_cap, params);
        free(params);
        params = temp;
    }

    char *body = emit_arr(ast->func.body, ast->func.body_cnt, false);

    if (ast->func.ret == NULL) {
        body = realloc(body, (strlen(body) + 64) * sizeof(char));

        if (rsp > 0)
            strcat(body, "    leave\n");
        else
            strcat(body, "    pop rbp\n");

        if (strcmp(ast->func.name, "main") == 0)
            strcat(body, "    mov rax, 60\n"
                         "    xor rdi, rdi\n"
                         "    syscall\n");
        else
            strcat(body, "    ret\n");
    }

    char *code = calloc(strlen(template) + strlen(ast->func.name) + strlen(params) + strlen(body) + strlen(func_data) + 1, sizeof(char));
    sprintf(code, template, ast->func.name, params, body, func_data);
    free(params);
    free(body);

    globs_reset();
    return code;
}

char *emit_call(AST *ast);

char *emit_call_arg(AST *ast, char *param_type, char *loc, bool on_stack) {
    char *code;

    switch (ast->type) {
        case AST_INT:
            code = calloc(strlen(loc) + 128, sizeof(char));
            
            if (strcmp(param_type, "float") == 0) {
                if (on_stack)
                    sprintf(code, "    mov eax, %d\n"
                                  "    cvtsi2ss xmm0, eax\n"
                                  "    movss dword %s, xmm0\n", (int)ast->data.digit, loc);
                else
                    sprintf(code, "    mov eax, %d\n"
                                  "    cvtsi2ss %s, eax\n", (int)ast->data.digit, loc);
            } else {
                if (on_stack)
                    sprintf(code, "    mov dword %s, %d\n", loc, (int)ast->data.digit);
                else
                    sprintf(code, "    mov %s, %d\n", loc, (int)ast->data.digit);
            }
            break;
        case AST_FLOAT:
            code = calloc(strlen(loc) + 64, sizeof(char));

            if (on_stack)
                sprintf(code, "    movss xmm0, dword [.f%zu]\n"
                              "    movss dword %s, xmm0\n", float_init(32, ast->data.digit), loc);
            else
                sprintf(code, "    movss %s, dword [.f%zu]\n", loc, float_init(32, ast->data.digit));
            break;
        case AST_VAR: {
            AST *var = sym_find(AST_ASSIGN, ast->scope_def, ast->var.name);
            code = calloc(strlen(loc) + strlen(loc) + 128, sizeof(char));
            
            if (strcmp(param_type, "char") == 0 || strcmp(param_type, "int") == 0) {
                if (strcmp(var->assign.type, "char") == 0) {
                    if (on_stack)
                        sprintf(code, "    movsx eax, byte %s\n"
                                      "    mov dword %s, eax\n", var->assign.rbp, loc);
                    else
                        sprintf(code, "    movsx %s, byte %s\n", loc, var->assign.rbp);
                } else if (strcmp(var->assign.type, "int") == 0) {
                    if (on_stack)
                        sprintf(code, "    mov eax, dword %s\n"
                                      "    mov dword %s, eax\n", var->assign.rbp, loc);
                    else
                        sprintf(code, "    mov %s, dword %s\n", loc, var->assign.rbp);
                } else {
                    sprintf(code, "    cvttss2si %s, dword %s\n", loc, var->assign.rbp);
                }
            } else if (strcmp(param_type, "float") == 0) {
                if (strcmp(var->assign.type, "char") == 0) {
                    if (on_stack)
                        sprintf(code, "    movsx eax, byte %s\n"
                                      "    cvtsi2ss xmm0, eax\n"
                                      "    movss dword %s, xmm0\n", var->assign.rbp, loc);
                    else
                        sprintf(code, "    movsx eax, byte %s\n"
                                      "    cvtsi2ss %s, eax\n", var->assign.rbp, loc);
                } else if (strcmp(var->assign.type, "int") == 0) {
                    if (on_stack)
                        sprintf(code, "    cvtsi2ss xmm0, dword %s\n"
                                      "    movss dword %s, xmm0\n", var->assign.rbp, loc);
                    else
                        sprintf(code, "    cvtsi2ss %s, dword %s\n", loc, var->assign.rbp);
                } else {
                    if (on_stack)
                        sprintf(code, "    movss xmm0, dword %s\n"
                                      "    movss dword %s, xmm0\n", var->assign.rbp, loc);
                    else
                        sprintf(code, "    movss %s, dword %s\n", loc, var->assign.rbp);
                }
            } else {
                if (on_stack)
                    sprintf(code, "    mov rax, qword %s\n"
                                  "    mov qword %s, rax\n", var->assign.rbp, loc);
                else
                    sprintf(code, "    mov %s, qword %s\n", loc, var->assign.rbp);
            }
            break;
        }
        case AST_CALL: {
            code = emit_call(ast);
            char *call_type = sym_find(AST_FUNC, "<global>", ast->call.name)->func.type;
            char *store = calloc(strlen(loc) + 64, sizeof(char));

            if (strcmp(param_type, "char") == 0 || strcmp(param_type, "int") == 0) {
                if (strcmp(call_type, "float") == 0) {
                    if (on_stack)
                        sprintf(store, "    cvttss2si eax, xmm0\n"
                                       "    mov dword %s, eax\n", loc);
                    else
                        sprintf(store, "    cvttss2si %s, xmm0\n", loc);
                } else {
                    if (on_stack)
                        sprintf(store, "    mov dword %s, eax\n", loc);
                    else
                        sprintf(store, "    mov %s, eax\n", loc);
                }
            } else if (strcmp(param_type, "float") == 0) {
                if (strcmp(call_type, "float") == 0) {
                    if (on_stack)
                        sprintf(store, "    movss dword %s, xmm0\n", loc);
                    else
                        sprintf(store, "    movss %s, xmm0\n", loc);
                } else {
                    if (on_stack)
                        sprintf(store, "    cvtsi2ss xmm0, eax\n"
                                       "    movss dword %s, xmm0\n", loc);
                    else
                        sprintf(store, "    cvtsi2ss %s, eax\n", loc);
                }
            } else {
                if (on_stack)
                    sprintf(store, "    mov qword %s, rax\n", loc);
                else
                    sprintf(store, "    mov %s, rax\n", loc);
            }

            code = realloc(code, (strlen(code) + strlen(store) + 1) * sizeof(char));
            strcat(code, store);
            free(store);
            break;
        }
        case AST_MATH: {
            code = emit_ast(ast);
            char *store = calloc(strlen(loc) + 128, sizeof(char));

            if (strcmp(param_type, "char") == 0 || strcmp(param_type, "int") == 0) {
                if (float_math_result) {
                    if (on_stack)
                        sprintf(store, "    cvttss2si eax, xmm0\n"
                                       "    mov dword %s, eax\n", loc);
                    else
                        sprintf(store, "    cvttss2si %s, xmm0\n", loc);
                } else {
                    if (on_stack)
                        sprintf(store, "    mov dword %s, eax\n", loc);
                    else
                        sprintf(store, "    mov %s, eax\n", loc);
                }
            } else {
                if (float_math_result) {
                    if (on_stack)
                        sprintf(store, "    movss dword %s, xmm0\n", loc);
                    else
                        sprintf(store, "    movss %s, xmm0\n", loc);
                } else {
                    if (on_stack)
                        sprintf(store, "    cvtsi2ss xmm0, eax\n"
                                       "    movss dword %s, xmm0\n", loc);
                    else
                        sprintf(store, "    cvtsi2ss %s, eax\n", loc);
                }
            }

            code = realloc(code, (strlen(code) + strlen(store) + 1) * sizeof(char));
            strcat(code, store);
            free(store);
            break;
        }
        case AST_REF: {
            AST *ref_sym = sym_find(AST_ASSIGN, ast->scope_def, ast->ref.name);
            code = calloc(strlen(ref_sym->assign.rbp) + strlen(loc) + 128, sizeof(char));

            if (on_stack)
                sprintf(code, "    lea rax, %s\n"
                              "    mov qword %s, rax\n", loc, ref_sym->assign.rbp);
            else
                sprintf(code, "    lea %s, %s\n", loc, ref_sym->assign.rbp);
            break;
        }
        default: assert(false);
    }

    return code;
}

char *emit_math(AST *ast);

char *emit_call(AST *ast) {
    char *code = calloc(1, sizeof(char));
    char *next;
    char *loc;
    AST *sym = sym_find(AST_FUNC, "<global>", ast->call.name);
    AST *param;
    AST *arg;
    size_t ints = 0;
    size_t floats = 0;
    size_t beg_rsp = rsp;

    for (size_t i = 0; i < sym->func.params_cnt; i++) {
        param = sym->func.params[i];
        arg = ast->call.args[i];
        loc = calloc(64, sizeof(char));

        if (strcmp(param->assign.type, "char") == 0 || strcmp(param->assign.type, "int") == 0) {
            if (ints < 6)
                strcpy(loc, regs[int_params[ints++]][DWORD]);
            else {
                rsp += 8;
                sprintf(loc, "[rbp-%zu]", rsp);
            }
        } else if (strcmp(param->assign.type, "float") == 0)
            if (floats < 15)
                strcpy(loc, regs[XMM][++floats]);
            else {
                rsp += 8;
                sprintf(loc, "[rbp-%zu]", rsp);
            }
        else {
            if (ints < 6)
                strcpy(loc, regs[int_params[ints++]][QWORD]);
            else {
                rsp += 8;
                sprintf(loc, "[rbp-%zu]", rsp);
            }
        }

        if (arg->type == AST_CALL && i > 0) {
            char *temp;
            char *store_ints = calloc(1, sizeof(char));
            char *load_ints = calloc(1, sizeof(char));
            char *load;
            AST *temp_param;

            size_t beg_rsp = rsp;

            for (size_t j = 0; j < i; j++) {
                temp_param = sym->func.params[j];
                if (strcmp(temp_param->assign.type, "char") != 0 && strcmp(temp_param->assign.type, "int") != 0)
                    continue;

                temp = calloc(64, sizeof(char));
                sprintf(temp, "    push %s\n", regs[int_params[j]][QWORD]);

                store_ints = realloc(store_ints, (strlen(store_ints) + strlen(temp) + 1) * sizeof(char));
                strcat(store_ints, temp);

                sprintf(temp, "    pop %s\n", regs[int_params[j]][QWORD]);

                load = calloc(strlen(load_ints) + strlen(temp) + 1, sizeof(char));
                strcpy(load, temp);
                strcat(load, load_ints);
                free(load_ints);
                load_ints = load;
                free(temp);
            }

            char *store_floats = calloc(1, sizeof(char));
            char *load_floats = calloc(1, sizeof(char));

            size_t rsp_before_floats = rsp;
            size_t float_rsp_cap = 0;

            while (rsp > rsp_cap + float_rsp_cap)
                float_rsp_cap += SUB_RSP_SIZE;

            for (size_t j = 0; j < i; j++) {
                temp_param = sym->func.params[j];
                if (strcmp(temp_param->assign.type, "float") != 0)
                    continue;

                temp = calloc(64, sizeof(char));
                rsp += 8;
                sprintf(temp, "    movss dword [rbp-%zu], %s\n", rsp, regs[XMM][j + 1]);

                store_ints = realloc(store_ints, (strlen(store_ints) + strlen(temp) + 1) * sizeof(char));
                strcat(store_ints, temp);

                sprintf(temp, "    movss %s, dword [rbp-%zu]\n", regs[XMM][j + 1], rsp);

                load = calloc(strlen(load_ints) + strlen(temp) + 1, sizeof(char));
                strcpy(load, temp);
                strcat(load, load_ints);
                free(load_ints);
                load_ints = load;
                free(temp);
            }

            next = emit_call_arg(arg, param->assign.type, loc, strstr(loc, "[rbp-") != NULL ? true : false);

            temp = calloc(strlen(store_ints) + strlen(store_floats) + strlen(next) + strlen(load_floats) + strlen(load_ints) + 128, sizeof(char));
            
            if (rsp_before_floats > 0)
                sprintf(temp, "%s"
                              "    sub rsp, %zu\n"
                              "%s"
                              "%s"
                              "%s"
                              "    add rsp, %zu\n"
                              "%s", store_ints, rsp_before_floats, store_floats, next, load_floats, rsp_before_floats, load_ints);
            else
                sprintf(temp, "%s"
                              "%s"
                              "%s"
                              "%s"
                              "%s", store_ints, store_floats, next, load_floats, load_ints);

            free(store_ints);
            free(store_floats);
            free(load_ints);
            free(load_floats);

            code = realloc(code, (strlen(code) + strlen(temp) + 1) * sizeof(char));
            strcat(code, temp);
            free(temp);
            free(next);
            rsp = beg_rsp;
            free(loc);
            continue;
        }
        
        next = emit_call_arg(arg, param->assign.type, loc, strstr(loc, "[rbp-") != NULL ? true : false);
        code = realloc(code, (strlen(code) + strlen(next) + 1) * sizeof(char));
        strcat(code, next);
        free(next);
        free(loc);
    }

    code = realloc(code, (strlen(code) + strlen(ast->call.name) + 32) * sizeof(char));
    strcat(code, "    call ");
    strcat(code, ast->call.name);
    strcat(code, "_\n");

    if (rsp != beg_rsp) {
        char *temp = calloc(strlen(code) + 128, sizeof(char));
        sprintf(temp, "    sub rsp, %zu\n"
                      "%s"
                      "    add rsp, %zu\n", rsp - beg_rsp, code, rsp - beg_rsp);
        free(code);
        code = temp;
        rsp = beg_rsp;
    }

    return code;
}

char *emit_subscr(AST *ast);

char *emit_assign(AST *ast) {
    char *code;
    char *sub_rsp = NULL;
    AST *value;
    AST *sym;
    char *type;
    char *rbp;

    if (ast->type == AST_SUBSCR) {
        sym = sym_find(AST_ASSIGN, ast->scope_def, ast->subscr.name);
        type = strdup(sym->assign.type);
        type[strlen(type) - 1] = '\0';

        sub_rsp = emit_subscr(ast);
        rbp = calloc(128, sizeof(char));
        strcpy(rbp, sym->assign.rbp);
        rbp[strlen(rbp) - 1] = '\0';
        strcat(rbp, "+r10*");

        size_t size;
        if (strcmp(type, "char") == 0)
            size = 1;
        else if (strcmp(type, "int") == 0 || strcmp(type, "float") == 0)
            size = 4;
        else
            size = 8;

        char buf[64];
        sprintf(buf, "%zu", size);
        strcat(rbp, buf);
        strcat(rbp, "]");

        value = ast->subscr.value;
    } else {
        sym = sym_find(AST_ASSIGN, ast->scope_def, ast->assign.name);
        value = ast->assign.value;
        type = sym->assign.type;
        rbp = sym->assign.rbp;
    }

    if (ast->type == AST_ASSIGN && ast->assign.type != NULL) {
        size_t size;
        size_t cnt;
        char *var_type;

        if (ast->assign.arr_cap > 0) {
            var_type = strdup(type);
            var_type[strlen(var_type) - 1] = '\0';
            cnt = ast->assign.arr_cap;
        } else {
            var_type = strdup(type);
            cnt = 1;
        }

        if (strcmp(var_type, "char") == 0)
            size = cnt * 1;
        else if (strcmp(var_type, "int") == 0 || strcmp(var_type, "float") == 0)
            size = cnt * 4;
        else
            size = cnt * 8;

        free(var_type);

        rsp += size;
        rbp = calloc(32, sizeof(char));
        sprintf(rbp, "[rbp-%zu]", rsp);
        sym->assign.rbp = rbp;

        if (rsp > rsp_cap) {
            size_t before = rsp_cap;

            while (rsp > rsp_cap)
                rsp_cap += SUB_RSP_SIZE;

            sub_rsp = calloc(64, sizeof(char));
            sprintf(sub_rsp, "    sub rsp, %zu\n", rsp_cap - before);
        }
    }

    if (value == NULL)
        return sub_rsp != NULL ? sub_rsp : calloc(1, sizeof(char));

    switch (value->type) {
        case AST_INT:
            code = calloc(strlen(rbp) + 128, sizeof(char));
            
            if (strcmp(type, "char") == 0)
                sprintf(code, "    mov byte %s, %d\n", rbp, (int)value->data.digit);
            else if (strcmp(type, "int") == 0)
                sprintf(code, "    mov dword %s, %d\n", rbp, (int)value->data.digit);
            else
                sprintf(code, "    mov eax, %d\n"
                              "    cvtsi2ss xmm0, eax\n"
                              "    movss dword %s, xmm0\n", (int)value->data.digit, rbp);
            break;
        case AST_FLOAT:
            code = calloc(strlen(rbp) + 128, sizeof(char));
            sprintf(code, "    movss xmm0, dword [.f%zu]\n"
                          "    movss dword %s, xmm0\n", float_init(32, value->data.digit), rbp);
            break;
        case AST_VAR: {
            AST *var = sym_find(AST_ASSIGN, value->scope_def, value->var.name);
            code = calloc(strlen(rbp) + strlen(var->assign.rbp) + 128, sizeof(char));

            if (strcmp(type, "char") == 0) {
                if (strcmp(var->assign.type, "char") == 0)
                    sprintf(code, "    mov al, byte %s\n"
                                  "    mov byte %s, al\n", var->assign.rbp, rbp);
                else if (strcmp(var->assign.type, "int") == 0)
                    sprintf(code, "    mov eax, dword %s\n"
                                  "    mov byte %s, al\n", var->assign.rbp, rbp);
                else
                    sprintf(code, "    cvttss2si eax, dword %s\n"
                                  "    mov byte %s, al\n", var->assign.rbp, rbp);
            } else if (strcmp(type, "int") == 0) {
                if (strcmp(var->assign.type, "char") == 0)
                    sprintf(code, "    movsx eax, byte %s\n"
                                  "    mov dword %s, eax\n", var->assign.rbp, rbp);
                else if (strcmp(var->assign.type, "int") == 0)
                    sprintf(code, "    mov eax, dword %s\n"
                                  "    mov dword %s, eax\n", var->assign.rbp, rbp);
                else
                    sprintf(code, "    cvttss2si eax, dword %s\n"
                                  "    mov dword %s, eax\n", var->assign.rbp, rbp);
            } else if (strcmp(type, "float") == 0) {
                if (strcmp(var->assign.type, "char") == 0)
                    sprintf(code, "    movsx eax, byte %s\n"
                                  "    cvtsi2ss xmm0, eax\n"
                                  "    movss dword %s, xmm0\n", var->assign.rbp, rbp);
                else if (strcmp(var->assign.type, "int") == 0)
                    sprintf(code, "    cvtsi2ss xmm0, dword %s\n"
                                  "    movss dword %s, xmm0\n", var->assign.rbp, rbp);
                else
                    sprintf(code, "    movss xmm0, dword %s\n"
                                  "    movss dword %s, xmm0\n", var->assign.rbp, rbp);
            } else
                sprintf(code, "    mov rax, qword %s\n"
                              "    mov qword %s, rax\n", var->assign.rbp, rbp);
            break;
        }
        case AST_CALL: {
            code = emit_ast(value);
            char *call_type = sym_find(AST_FUNC, "<global>", value->call.name)->func.type;
            char *temp = calloc(strlen(rbp) + 64, sizeof(char));

            if (strcmp(type, "char") == 0) {
                if (strcmp(call_type, "float") == 0)
                    sprintf(temp, "    cvttss2si eax, xmm0\n"
                                  "    mov byte %s, al\n", rbp);
                else
                    sprintf(temp, "    mov byte %s, al\n", rbp);
            } else if (strcmp(type, "int") == 0) {
                if (strcmp(call_type, "float") == 0)
                    sprintf(temp, "    cvttss2si eax, xmm0\n"
                                  "    mov dword %s, eax\n", rbp);
                else
                    sprintf(temp, "    mov dword %s, eax\n", rbp);
            } else if (strcmp(type, "float") == 0) {
                if (strcmp(call_type, "float") == 0)
                    sprintf(temp, "    movss dword %s, xmm0\n", rbp);
                else
                    sprintf(temp, "    cvtsi2ss xmm0, eax\n"
                                  "    movss dword %s, xmm0\n", rbp);
            } else
                sprintf(temp, "    mov qword %s, rax\n", rbp);

            if (ast->type == AST_SUBSCR) {
                char *temp2 = calloc(strlen(code) + 64, sizeof(char));
                sprintf(temp2, "    push r10\n"
                               "%s"
                               "    pop r10\n", code);
                free(code);
                code = temp2;
            }

            code = realloc(code, (strlen(code) + strlen(temp) + 1) * sizeof(char));
            strcat(code, temp);
            free(temp);
            break;
        }
        case AST_MATH: {
            code = emit_ast(value);
            char *fix = calloc(strlen(rbp) + 128, sizeof(char));

            if (strcmp(type, "char") == 0) {
                if (float_math_result)
                    sprintf(fix, "    cvttss2si eax, xmm0\n"
                                 "    mov byte %s, al\n", rbp);
                else
                    sprintf(fix, "    mov byte %s, al\n", rbp);
            } else if (strcmp(type, "int") == 0) {
                if (float_math_result)
                    sprintf(fix, "    cvttss2si eax, xmm0\n"
                                 "    mov dword %s, eax\n", rbp);
                else
                    sprintf(fix, "    mov dword %s, eax\n", rbp);
            } else {
                if (float_math_result)
                    sprintf(fix, "    movss dword %s, xmm0\n", rbp);
                else
                    sprintf(fix, "    cvtsi2ss xmm0, eax\n"
                                 "    movss dword %s, xmm0\n", rbp);
            }

            if (ast->type == AST_SUBSCR) {
                char *temp = calloc(strlen(code) + 64, sizeof(char));
                sprintf(temp, "    push r10\n"
                              "%s"
                              "    pop r10\n", code);
                free(code);
                code = temp;
            }

            code = realloc(code, (strlen(code) + strlen(fix) + 1) * sizeof(char));
            strcat(code, fix);
            free(fix);
            break;
        }
        case AST_STR:
            code = calloc(strlen(rbp) + 64, sizeof(char));
            sprintf(code, "    mov rax, qword [.s%zu]\n"
                          "    mov qword %s, rax\n", str_init(value->data.str), rbp);
            break;
            /*
        case AST_ARR_LST: {
            char *base_type = strdup(sym->assign.type);
            base_type[strlen(base_type) - 1] = '\0';

            size_t size_each;
            if (strcmp(base_type, "char") == 0)
                size_each = 1;
            else if (strcmp(base_type, "int") == 0 || strcmp(base_type, "float") == 0)
                size_each = 4;
            else
                size_each = 8;

            AST *at;
            char *next;

            free(base_type);
            break;
        }
        */
        case AST_REF: {
            AST *ref = sym_find(AST_ASSIGN, value->scope_def, value->ref.name);
            code = calloc(strlen(ref->assign.rbp) + strlen(rbp) + 128, sizeof(char));
            sprintf(code, "    lea rax, %s\n"
                          "    mov qword %s, rax\n", ref->assign.rbp, rbp);
            break;
        }
        default: assert(false);
    }

    if (sub_rsp != NULL) {
        sub_rsp = realloc(sub_rsp, (strlen(sub_rsp) + strlen(code) + 1) * sizeof(char));
        strcat(sub_rsp, code);
        free(code);

        if (ast->type == AST_SUBSCR) {
            free(type);
            free(rbp);
        }

        return sub_rsp;
    }

    return code;
}

char *emit_ret(AST *ast) {
    char *code;
    char *type = sym_find(AST_FUNC, "<global>", ast->func_def)->func.type;
    AST *value = ast->ret.value;
    char ret[80];

    if (rsp > 0)
        strcpy(ret, "    leave\n");
    else
        strcpy(ret, "    pop rbp\n");

    if (strcmp(ast->func.name, "main") == 0)
        strcat(ret, "    mov rax, 60\n"
                    "    xor rdi, rdi\n"
                    "    syscall\n");
    else
        strcat(ret, "    ret\n");

    if (value == NULL)
        return strdup(ret);

    switch (value->type) {
        case AST_INT:
            code = calloc(64, sizeof(char));
            sprintf(code, "    mov eax, %d\n", (int)value->data.digit);

            if (strcmp(type, "float") == 0)
                strcat(code, "    cvtsi2ss xmm0, eax\n");
            break;
        case AST_FLOAT:
            code = calloc(64, sizeof(char));
            sprintf(code, "    movss xmm0, dword [.f%zu]\n", float_init(32, value->data.digit));
            break;
        case AST_VAR: {
            AST *var = sym_find(AST_ASSIGN, value->scope_def, value->var.name);
            code = calloc(strlen(var->assign.rbp) + 128, sizeof(char));

            if (strcmp(type, "char") == 0 || strcmp(type, "int") == 0) {
                if (strcmp(var->assign.type, "char") == 0)
                    sprintf(code, "    movsx eax, byte %s\n", var->assign.rbp);
                else if (strcmp(var->assign.type, "int") == 0)
                    sprintf(code, "    mov eax, dword %s\n", var->assign.rbp);
                else
                    sprintf(code, "    cvttss2si eax, dword %s\n", var->assign.rbp);
            } else if (strcmp(type, "float") == 0) {
                if (strcmp(var->assign.type, "char") == 0)
                    sprintf(code, "    movsx eax, byte %s\n"
                                  "    cvtsi2ss xmm0, eax\n", var->assign.rbp);
                else if (strcmp(var->assign.type, "int") == 0)
                    sprintf(code, "    cvtsi2ss xmm0, dword %s\n", var->assign.rbp);
                else
                    sprintf(code, "    movss xmm0, dword %s\n", var->assign.rbp);
            } else
                sprintf(code, "    mov rax, qword %s\n", var->assign.rbp);
            break;
        }
        case AST_CALL: {
            code = emit_ast(value);
            char *call_type = sym_find(AST_FUNC, "<global>", value->call.name)->func.type;
            char *temp = calloc(64, sizeof(char));

            if ((strcmp(type, "char") == 0 || strcmp(type, "int") == 0) && strcmp(call_type, "float") == 0)
                strcpy(temp, "    cvttss2si eax, xmm0\n");
            else if (strcmp(type, "float") == 0 && (strcmp(call_type, "char") == 0 || strcmp(call_type, "int") == 0))
                strcpy(temp, "    cvtsi2ss xmm0, eax\n");

            code = realloc(code, (strlen(code) + strlen(temp) + 1) * sizeof(char));
            strcat(code, temp);
            free(temp);
            break;
        }
        case AST_MATH: {
            code = emit_ast(value);

            if (strcmp(type, "float") == 0 && !float_math_result) {
                code = realloc(code, (strlen(code) + 32) * sizeof(char));
                strcat(code, "    cvtsi2ss xmm0, eax\n");
            } else if (strcmp(type, "float") != 0 && float_math_result) {
                code = realloc(code, (strlen(code) + 32) * sizeof(char));
                strcat(code, "    cvttss2si eax, xmm0\n");
            }
            break;
        }
        case AST_DEREF: {
            AST *sym = sym_find(AST_ASSIGN, value->scope_def, value->deref.name);
            char *base_type = strdup(sym->assign.type);
            base_type[strlen(base_type) - 1] = '\0';

            char rbp[64];
            strcpy(rbp, sym->assign.rbp);
            rbp[strlen(rbp) - 1] = '\0';

            code = emit_ast(value);
            char *temp = calloc(strlen(code) + strlen(rbp) + 128, sizeof(char));

            if (strcmp(type, "float") == 0) {
                if (strcmp(base_type, "char") == 0)
                    sprintf(temp, "%s    movsx eax, byte %s+r10*1]\n"
                                  "    cvtsi2ss xmm0, eax\n", code, rbp);
                else if (strcmp(base_type, "int") == 0)
                    sprintf(temp, "%s    cvtsi2ss xmm0, dword %s+r10*1]\n", code, rbp);
                else
                    sprintf(temp, "%s    movss xmm0, dword %s+r10*1]\n", code, rbp);
            } else {
                if (strcmp(base_type, "char") == 0)
                    sprintf(temp, "%s    movsx eax, byte %s+r10*1]\n", code, rbp);
                else if (strcmp(base_type, "int") == 0)
                    sprintf(temp, "%s    mov eax, dword %s+r10*1]\n", code, rbp);
                else
                    sprintf(temp, "%s    cvttss2si eax, dword %s+r10*1]\n", code, rbp);
            }

            free(base_type);
            free(code);
            code = temp;
            break;
        }
        case AST_REF: {
            AST *sym = sym_find(AST_ASSIGN, "<global>", value->ref.name);
            code = calloc(strlen(sym->assign.rbp) + 64, sizeof(char));
            sprintf(code, "    lea rax, %s\n", sym->assign.rbp);
            break;
        }
        default: assert(false);
    }

    code = realloc(code, (strlen(code) + strlen(ret) + 1) * sizeof(char));
    strcat(code, ret);
    return code;
}

char *emit_math_expr(AST *left, AST *right, TokType type) {
    char *code;
    char left_value[64];
    char right_value[64];
    char *setup = NULL;
    bool is_float = false;

    switch (left->type) {
        case AST_INT:
            code = calloc(64, sizeof(char));
            sprintf(code, "    mov eax, %d\n", (int)left->data.digit);
            break;
        case AST_FLOAT:
            code = calloc(64, sizeof(char));
            sprintf(code, "    movss xmm0, dword [.f%zu]\n", float_init(32, left->data.digit));
            is_float = true;
            break;
        case AST_VAR: {
            AST *var = sym_find(AST_ASSIGN, left->scope_def, left->var.name);
            code = calloc(strlen(var->assign.rbp) + 64, sizeof(char));

            if (strcmp(var->assign.type, "char") == 0)
                sprintf(code, "    movsx eax, byte %s\n", var->assign.rbp);
            else if (strcmp(var->assign.type, "int") == 0)
                sprintf(code, "    mov eax, dword %s\n", var->assign.rbp);
            else {
                sprintf(code, "    movss xmm0, dword %s\n", var->assign.rbp);
                is_float = true;
            }
            break;
        }
        case AST_CALL: {
            code = emit_ast(left);
            AST *sym = sym_find(AST_FUNC, "<global>", left->call.name);

            if (strcmp(sym->func.type, "float") == 0)
                is_float = true;
            break;
        }
        case AST_MATH_VAR: {
            is_float = left->math_var.is_float;

            if (left->math_var.stack_rbp != NULL) {
                code = calloc(strlen(left->math_var.stack_rbp) + 64, sizeof(char));

                if (left->math_var.is_float)
                    sprintf(code, "    movss xmm0, dword %s\n", left->math_var.stack_rbp);
                else
                    sprintf(code, "    mov eax, dword %s\n", left->math_var.stack_rbp);
                break;
            }

            code = calloc(1, sizeof(char));
            break;
        }
        case AST_DEREF: {
            code = emit_ast(left);
            AST *sym = sym_find(AST_ASSIGN, left->scope_def, left->deref.name);
            char *base_type = strdup(sym->assign.type);
            base_type[strlen(base_type) - 1] = '\0';

            char rbp[64];
            sprintf(rbp, "%s", sym->assign.rbp);
            rbp[strlen(rbp) - 1] = '\0';

            char *temp = calloc(strlen(code) + strlen(rbp) + 64, sizeof(char));

            if (strcmp(base_type, "char") == 0)
                sprintf(temp, "%s    movsx eax, byte %s+r10*1]\n", code, rbp);
            else if (strcmp(base_type, "int") == 0)
                sprintf(temp, "%s    mov eax, dword %s+r10*4]\n", code, rbp);
            else {
                sprintf(temp, "%s    movss xmm0, dword %s+r10*4]\n", code, rbp);
                is_float = true;
            }

            free(base_type);
            free(code);
            code = temp;
            break;
        }
        case AST_EXPR: return emit_math_expr(left->expr.value, right, type);
        case AST_MATH:
            code = emit_math(left);
            is_float = float_math_result;
            break;
        default: assert(false);
    }

    if (is_float)
        strcpy(left_value, "xmm0");
    else
        strcpy(left_value, "eax");

    switch (right->type) {
        case AST_INT:
            if (is_float) {
                setup = calloc(64, sizeof(char));
                sprintf(setup, "    mov eax, %d\n"
                               "    cvtsi2ss xmm1, eax\n", (int)right->data.digit);
                strcpy(right_value, "xmm1");
            } else
                sprintf(right_value, "%d", (int)right->data.digit);
            break;
        case AST_FLOAT:
            if (!is_float) {
                setup = calloc(32, sizeof(char));
                strcpy(setup, "    cvtsi2ss xmm0, eax\n");
                strcpy(left_value, "xmm0");
                is_float = true;
            }

            sprintf(right_value, "dword [.f%zu]", float_init(32, right->data.digit));
            break;
        case AST_VAR: {
            AST *var = sym_find(AST_ASSIGN, right->scope_def, right->var.name);

            if (is_float) {
                if (strcmp(var->assign.type, "char") == 0) {
                    setup = calloc(strlen(var->assign.rbp) + 64, sizeof(char));
                    sprintf(setup, "    movsx eax, byte %s\n"
                                   "    cvtsi2ss xmm1, eax\n", var->assign.rbp);
                    strcpy(right_value, "xmm1");
                } else if (strcmp(var->assign.type, "int") == 0) {
                    setup = calloc(strlen(var->assign.rbp) + 64, sizeof(char));
                    sprintf(setup, "    cvtsi2ss xmm1, dword %s\n", var->assign.rbp);
                    strcpy(right_value, "xmm1");
                } else
                    sprintf(right_value, "dword %s", var->assign.rbp);
            } else {
                if (strcmp(var->assign.type, "char") == 0) {
                    setup = calloc(strlen(var->assign.rbp) + 64, sizeof(char));
                    sprintf(setup, "    movsx ebx, byte %s\n", var->assign.rbp);
                    strcpy(right_value, "ebx");
                } else if (strcmp(var->assign.type, "int") == 0)
                    sprintf(right_value, "dword %s", var->assign.rbp);
                else {
                    setup = calloc(32, sizeof(char));
                    sprintf(setup, "    cvtsi2ss xmm0, eax\n");
                    strcpy(left_value, "xmm0");
                    sprintf(right_value, "dword %s", var->assign.rbp);
                    is_float = true;
                }
            }
            break;
        }
        case AST_CALL: {
            AST *sym = sym_find(AST_FUNC, "<global>", right->call.name);
            rsp += 8;
            setup = emit_ast(right);
            char *save;

            // Please forgive me for this awful looking code
            if (is_float) {
                if (rsp + 8 > rsp_cap) {
                    rsp_cap += 8;
                    save = calloc(strlen(setup) + 128, sizeof(char));
                    rsp_cap -= 8;

                    if (strcmp(sym->func.type, "float") == 0)
                        sprintf(save, "    sub rsp, 8\n"
                                      "    movss dword [rbp-%zu], xmm0\n"
                                      "%s"
                                      "    movss xmm1, xmm0\n"
                                      "    movss xmm0, dword [rbp-%zu]\n"
                                      "    add rsp, 8\n", rsp + 8, setup, rsp + 8);
                    else
                        sprintf(save, "    sub rsp, 8\n"
                                      "    movss dword [rbp-%zu], xmm0\n"
                                      "%s"
                                      "    movss xmm0, dword [rbp-%zu]\n"
                                      "    cvtsi2ss xmm1, eax\n"
                                      "    add rsp, 8\n", rsp + 8, setup, rsp + 8);
                } else {
                    save = calloc(strlen(setup) + 128, sizeof(char));

                    if (strcmp(sym->func.type, "float") == 0)
                        sprintf(save, "    movss dword [rbp-%zu], xmm0\n"
                                      "%s"
                                      "    movss xmm1, xmm0\n"
                                      "    movss xmm0, dword [rbp-%zu]\n", rsp + 8, setup, rsp + 8);
                    else
                        sprintf(save, "    movss dword [rbp-%zu], xmm0\n"
                                      "%s"
                                      "    movss xmm0, dword [rbp-%zu]\n"
                                      "    cvtsi2ss xmm1, eax\n", rsp + 8, setup, rsp + 8);
                }

                strcpy(left_value, "xmm0");
                strcpy(right_value, "xmm1");
            } else {
                rsp_cap += 8;
                save = calloc(strlen(setup) + 128, sizeof(char));
                rsp_cap -= 8;

                if (strcmp(sym->func.type, "float") == 0) {
                    sprintf(save, "    push rax\n"
                                  "%s"
                                  "    movss xmm1, xmm0\n"
                                  "    pop rax\n"
                                  "    cvtsi2ss xmm0, eax\n", setup);

                    strcpy(left_value, "xmm0");
                    strcpy(right_value, "xmm1");
                    is_float = true;
                } else {
                    sprintf(save, "    push rax\n"
                                  "%s"
                                  "    mov ebx, eax\n"
                                  "    pop rax\n", setup);
                    strcpy(right_value, "ebx");
                }
            }

            rsp -= 8;
            free(setup);
            setup = save;
            break;
        }
        case AST_MATH_VAR: {
            if (right->math_var.stack_rbp != NULL) {
                if (is_float) {
                    if (right->math_var.is_float)
                        sprintf(right_value, "dword %s", right->math_var.stack_rbp);
                    else {
                        setup = calloc(strlen(right->math_var.stack_rbp) + 64, sizeof(char));
                        sprintf(setup, "    cvtsi2ss xmm1, dword %s\n", right->math_var.stack_rbp);
                        strcpy(right_value, "xmm1");
                    }
                } else {
                    if (right->math_var.is_float) {
                        setup = calloc(64, sizeof(char));
                        strcpy(setup, "    cvtsi2ss xmm0, eax\n");
                        strcpy(left_value, "xmm0");
                        is_float = true;
                    }
                    
                    sprintf(right_value, "dword %s", right->math_var.stack_rbp);
                }
                break;
            }

            // It's ensured that xmm0 or eax won't have been corrupted by, for example, a call by left,
            // it is only in the reg if it will not be corrupted
            char *temp = calloc(strlen(code) + 64, sizeof(char));

            if (is_float) {
                if (right->math_var.is_float)
                    strcpy(temp, "    movss xmm1, xmm0\n");
                else
                    sprintf(temp, "    cvtsi2ss xmm1, eax\n");
                
                strcpy(right_value, "xmm1");
            } else {
                if (right->math_var.is_float) {
                    strcpy(temp, "    movss xmm1, xmm0\n");
                    code = realloc(code, (strlen(code) + 32) * sizeof(char));
                    strcat(code, "    cvtsi2ss xmm0, eax\n");

                    strcpy(left_value, "xmm0");
                    strcpy(right_value, "xmm1");
                } else {
                    sprintf(temp, "    mov ebx, eax\n");
                    strcpy(right_value, "ebx");
                }
            }

            strcat(temp, code);
            free(code);
            code = temp;
            break;
        }
        case AST_DEREF: {
            AST *sym = sym_find(AST_ASSIGN, right->scope_def, right->deref.name);
            char *base_type = strdup(sym->assign.type);
            base_type[strlen(base_type) - 1] = '\0';

            char rbp[64];
            sprintf(rbp, "%s", sym->assign.rbp);
            rbp[strlen(rbp) - 1] = '\0';

            char *temp;
            rsp += 8;

            if (strcmp(base_type, "char") == 0) {
                if (is_float) {
                    if (rsp + 8 > rsp_cap) {
                        rsp_cap += 8;
                        setup = emit_ast(right);
                        rsp_cap -= 8;
                        temp = calloc(strlen(setup) + strlen(rbp) + 256, sizeof(char));

                        sprintf(temp, "    sub rsp, 8\n"
                                      "    movss dword [rbp-%zu], xmm0\n"
                                      "%s"
                                      "    movsx eax, byte %s+r10*1]\n"
                                      "    cvtsi2ss xmm1, eax\n"
                                      "    movss xmm0, dword [rbp-%zu]\n"
                                      "    add rsp, 8\n", rsp + 8, setup, rbp, rsp + 8);

                    } else {
                        setup = emit_ast(right);
                        temp = calloc(strlen(setup) + strlen(rbp) + 128, sizeof(char));

                        sprintf(temp, "    movss dword [rbp-%zu], xmm0\n"
                                      "%s"
                                      "    movsx eax, byte %s+r10*1]\n"
                                      "    cvtsi2ss xmm1, eax\n"
                                      "    movss xmm0, dword [rbp-%zu]\n", rsp + 8, setup, rbp, rsp + 8);
                    }

                    strcpy(right_value, "xmm1");
                } else {
                    setup = emit_ast(right);
                    temp = calloc(strlen(setup) + strlen(rbp) + 256, sizeof(char));

                    sprintf(temp, "    push rax\n"
                                  "%s"
                                  "    movsx ebx, byte %s+r10*1]\n"
                                  "    pop rax\n", setup, rbp);

                    strcpy(right_value, "ebx");
                }
            } else if (strcmp(base_type, "int") == 0) {
                if (is_float) {
                    if (rsp + 8 > rsp_cap) {
                        rsp_cap += 8;
                        setup = emit_ast(right);
                        rsp_cap -= 8;
                        temp = calloc(strlen(setup) + strlen(rbp) + 256, sizeof(char));

                        sprintf(temp, "    sub rsp, 8\n"
                                      "    movss dword [rbp-%zu], xmm0\n"
                                      "%s"
                                      "    cvtsi2ss xmm1, dword %s+r10*4]\n"
                                      "    movss xmm0, dword [rbp-%zu]\n"
                                      "    add rsp, 8\n", rsp + 8, setup, rbp, rsp + 8);

                    } else {
                        setup = emit_ast(right);
                        temp = calloc(strlen(setup) + strlen(rbp) + 128, sizeof(char));

                        sprintf(temp, "    movss dword [rbp-%zu], xmm0\n"
                                      "%s"
                                      "    cvtsi2ss xmm1, dword %s+r10*4]\n"
                                      "    movss xmm0, dword [rbp-%zu]\n", rsp + 8, setup, rbp, rsp + 8);
                    }

                    strcpy(right_value, "xmm1");
                } else {
                    setup = emit_ast(right);
                    temp = calloc(strlen(setup) + strlen(rbp) + 256, sizeof(char));

                    sprintf(temp, "    push rax\n"
                                  "%s"
                                  "    pop rax\n", setup);

                    sprintf(right_value, "dword %s+r10*4]", rbp);
                }
            } else {
                if (is_float) {
                    if (rsp + 8 > rsp_cap) {
                        rsp_cap += 8;
                        setup = emit_ast(right);
                        rsp_cap -= 8;
                        temp = calloc(strlen(setup) + strlen(rbp) + 256, sizeof(char));

                        sprintf(temp, "    sub rsp, 8\n"
                                      "    movss dword [rbp-%zu], xmm0\n"
                                      "%s"
                                      "    movss xmm0, dword [rbp-%zu]\n"
                                      "    add rsp, 8\n", rsp + 8, setup, rsp + 8);

                    } else {
                        setup = emit_ast(right);
                        temp = calloc(strlen(setup) + strlen(rbp) + 128, sizeof(char));

                        sprintf(temp, "    movss dword [rbp-%zu], xmm0\n"
                                      "%s"
                                      "    movss xmm0, dword [rbp-%zu]\n", rsp + 8, setup, rsp + 8);
                    }

                    strcpy(right_value, "xmm1");
                } else {
                    setup = emit_ast(right);
                    temp = calloc(strlen(setup) + strlen(rbp) + 256, sizeof(char));

                    sprintf(temp, "    push rax\n"
                                  "%s"
                                  "    pop rax\n"
                                  "    cvtsi2ss xmm0, eax\n", setup);
                    strcpy(left_value, "xmm0");
                    is_float = true;

                }

                sprintf(right_value, "dword %s+r10*4]", rbp);
            }

            free(base_type);
            rsp -= 8;
            free(setup);
            setup = temp;
            break;
        }
        case AST_EXPR:
            free(code);
            return emit_math_expr(left, right->expr.value, type);
        case AST_MATH:
            char *temp;
            rsp += 8;

            if (is_float) {
                if (rsp + 8 > rsp_cap) {
                    rsp_cap += 8;
                    setup = emit_ast(right);
                    rsp_cap -= 8;
                    temp = calloc(strlen(setup) + 256, sizeof(char));

                    if (float_math_result)
                        sprintf(temp, "    sub rsp, 8\n"
                                      "    movss dword [rbp-%zu], xmm0\n"
                                      "%s"
                                      "    movss xmm1, xmm0\n"
                                      "    movss xmm0, dword [rbp-%zu]\n"
                                      "    add rsp, 8\n", rsp + 8, setup, rsp + 8);
                    else
                        sprintf(temp, "    sub rsp, 8\n"
                                      "    movss dword [rbp-%zu], xmm0\n"
                                      "%s"
                                      "    cvtsi2ss xmm1, eax\n"
                                      "    movss xmm0, dword [rbp-%zu]\n"
                                      "    add rsp, 8\n", rsp + 8, setup, rsp + 8);
                } else {
                    setup = emit_ast(right);
                    temp = calloc(strlen(setup) + 256, sizeof(char));

                    if (float_math_result)
                        sprintf(temp, "    movss dword [rbp-%zu], xmm0\n"
                                      "%s"
                                      "    movss xmm1, xmm0\n"
                                      "    movss xmm0, dword [rbp-%zu]\n", rsp + 8, setup, rsp + 8);
                    else
                        sprintf(temp, "    movss dword [rbp-%zu], xmm0\n"
                                      "%s"
                                      "    cvtsi2ss xmm1, eax\n"
                                      "    movss xmm0, dword [rbp-%zu]\n", rsp + 8, setup, rsp + 8);
                }

                strcpy(right_value, "xmm1");
            } else {
                rsp_cap += 8;
                setup = emit_ast(right);
                rsp_cap -= 8;
                temp = calloc(strlen(setup) + 256, sizeof(char));

                if (float_math_result) {
                    sprintf(temp, "    push rax\n"
                                  "%s"
                                  "    movss xmm1, xmm0\n"
                                  "    pop rax\n"
                                  "    cvtsi2ss xmm0, eax\n", setup);

                    strcpy(left_value, "xmm0");
                    strcpy(right_value, "xmm1");
                    is_float = true;
                } else {
                    sprintf(temp, "    push rax\n"
                                  "%s"
                                  "    mov ebx, eax\n"
                                  "    pop rax\n", setup);
                    strcpy(right_value, "ebx");
                }
            }

            rsp -= 8;
            free(setup);
            setup = temp;
            break;
        default: assert(false);
    }

    if (setup != NULL) {
        code = realloc(code, (strlen(code) + strlen(setup) + 1) * sizeof(char));
        strcat(code, setup);
        free(setup);
    }

    char *math = calloc(strlen(left_value) + strlen(right_value) + 256, sizeof(char));

    switch (type) {
        case TOK_PLUS:
            if (is_float)
                sprintf(math, "    addss %s, %s\n", left_value, right_value);
            else
                sprintf(math, "    add %s, %s\n", left_value, right_value);
            break;
        case TOK_MINUS:
            if (is_float)
                sprintf(math, "    subss %s, %s\n", left_value, right_value);
            else
                sprintf(math, "    sub %s, %s\n", left_value, right_value);
            break;
        case TOK_STAR:
            if (is_float)
                sprintf(math, "    mulss %s, %s\n", left_value, right_value);
            else {
                if (right->data.digit >= 0 && is_power_of_two((unsigned int)right->data.digit))
                    sprintf(math, "    sal %s, %u\n", left_value, power_of_two((unsigned int)right->data.digit));
                else
                    sprintf(math, "    imul %s, %s\n", left_value, right_value);
            }
            break;
        case TOK_SLASH:
            if (is_float) {
                sprintf(math, "    divss %s, %s\n", left_value, right_value);
                break;
            }

            if (right->data.digit >= 0 && is_power_of_two((unsigned int)right->data.digit))
                sprintf(math, "    sar %s, %u\n", left_value, power_of_two((unsigned int)right->data.digit));
            else
                if (strcmp(right_value, "ebx") == 0)
                    sprintf(math, "    cqo\n"
                                  "    idiv %s\n", right_value);
                else
                    sprintf(math, "    mov ebx, %s\n"
                                  "    cqo\n"
                                  "    idiv ebx\n", right_value);
            break;
        default:
            if (right->data.digit >= 0 && is_power_of_two((unsigned int)right->data.digit))
                sprintf(math, "    and %s, %u\n", left_value, (unsigned int)right->data.digit - 1);
            else
                sprintf(math, "    xor rdi, rdi\n"
                              "    idiv %s\n"
                              "    mov eax, edx\n", right_value);
            break;
    }

    code = realloc(code, (strlen(code) + strlen(math) + 1) * sizeof(char));
    strcat(code, math);
    free(math);

    float_math_result = is_float;
    return code;
}

char *emit_math(AST *ast) {
    char *code = calloc(1, sizeof(char));
    char *next;

    AST **expr = ast->math.expr;
    size_t expr_cnt = ast->math.expr_cnt;

    bool is_float = false;
    size_t oper_cnt = expr_cnt / 2;
    size_t order[oper_cnt];
    size_t order_cnt = 0;

    for (size_t i = 1; i < expr_cnt; i += 2) {
        if (expr[i]->oper.kind != TOK_PLUS && expr[i]->oper.kind != TOK_MINUS)
            order[order_cnt++] = i;
    }

    for (size_t i = 1; i < expr_cnt; i += 2) {
        if (expr[i]->oper.kind == TOK_PLUS || expr[i]->oper.kind == TOK_MINUS)
            order[order_cnt++] = i;
    }

    AST *left;
    AST *right;
    AST *oper;
    AST *next_left;
    size_t oper_pos;
    size_t next_oper_pos;
    int j;
    
    size_t beg_rsp = rsp;
    size_t beg_cap = rsp_cap;

    for (size_t i = 0; i < oper_cnt; i++) {
        oper_pos = order[i];
        oper = expr[oper_pos];
        oper->active = false;

        j = 0;
        left = expr[oper_pos - 1];
        while (!left->active)
            left = expr[oper_pos + --j];

        j = 0;
        right = expr[oper_pos + 1];
        while (!right->active)
            right = expr[oper_pos + ++j];

        assert(left != NULL);
        assert(right != NULL);

        next = emit_math_expr(left, right, oper->oper.kind);
        code = realloc(code, (strlen(code) + strlen(next) + 1) * sizeof(char));
        strcat(code, next);
        free(next);

        if (!is_float)
            is_float = float_math_result;

        right->active = false;

        if (i == oper_cnt - 1)
            break;

        ast_fields_del(left);
        left->type = AST_MATH_VAR;
        left->math_var.is_float = is_float;
        next_oper_pos = order[i + 1];

        if (abs((int)oper_pos - (int)next_oper_pos) > 2 && i != oper_cnt - 2) {
save_math_result:
            // Not used in the next expression, needs to be saved to the stack
            char *save = calloc(128, sizeof(char));
            rsp += 8;
            rsp_cap += 8;

            if (is_float)
                sprintf(save, "    movss dword [rbp-%zu], xmm0\n", rsp);
            else
                sprintf(save, "    mov dword [rbp-%zu], eax\n", rsp);

            code = realloc(code, (strlen(code) + strlen(save) + 1) * sizeof(char));
            strcat(code, save);
            free(save);

            left->math_var.stack_rbp = calloc(64, sizeof(char));
            sprintf(left->math_var.stack_rbp, "[rbp-%zu]", rsp);
            break;
        }

        next_left = expr[order[i + 1] - 1];
        while (!next_left->active)
            next_left = expr[order[i + 1] + --j];

        if (next_left->type == AST_CALL) // Will corrupt the reg so must be saved
            goto save_math_result;

        // Used in the next expression, no need to save to the stack
        left->math_var.reg_name = is_float ? "xmm0" : "eax";
        left->math_var.stack_rbp = NULL;
    }

    rsp = beg_rsp;
    rsp_cap = beg_cap;

    float_math_result = is_float;
    return code;
}

char *emit_cond(AST **expr, size_t expr_cnt, char *true_label, char *false_label) {
    char *code = calloc(1, sizeof(char));

    AST *left;
    AST *right;
    TokType oper;

    char *compare;
    char *setup;
    char *label;
    bool is_float = false;
    bool opposite_jump = false;

    for (size_t i = 1; i < expr_cnt; i += 4) {
        left = expr[i - 1];
        right = expr[i + 1];
        oper = expr[i]->oper.kind;

        char left_value[64];
        char right_value[64];
        
        is_float = opposite_jump = false;

        switch (left->type) {
            case AST_INT:
                setup = calloc(64, sizeof(char));
                sprintf(setup, "    mov eax, %d\n", (int)left->data.digit);
                break;
            case AST_FLOAT:
                setup = calloc(64, sizeof(char));
                sprintf(setup, "    mov eax, %d\n", (int)left->data.digit);
                is_float = true;
                break;
            case AST_VAR: {
                AST *var = sym_find(AST_ASSIGN, left->scope_def, left->var.name);
                setup = calloc(strlen(var->assign.rbp) + 64, sizeof(char));
                
                if (strcmp(var->assign.type, "char") == 0)
                    sprintf(setup, "    movsx eax, byte %s\n", var->assign.rbp);
                else if (strcmp(var->assign.type, "int") == 0)
                    sprintf(setup, "    mov eax, dword %s\n", var->assign.rbp);
                else {
                    sprintf(setup, "    movss xmm0, dword %s\n", var->assign.rbp);
                    is_float = true;
                }
                break;
            }
            case AST_CALL: {
                setup = emit_ast(left);
                char *call_type = sym_find(AST_FUNC, "<global>", left->call.name)->func.type;

                if (strcmp(call_type, "float") == 0)
                    is_float = true;
                break;
            }
            case AST_MATH: {
                setup = emit_ast(left);
                is_float = float_math_result;
                break;
            }
            case AST_DEREF: {
                setup = emit_ast(left);
                AST *sym = sym_find(AST_ASSIGN, left->scope_def, left->deref.name);
                char *base_type = strdup(sym->assign.type);
                base_type[strlen(base_type) - 1] = '\0';

                char rbp[64];
                sprintf(rbp, "%s", sym->assign.rbp);
                rbp[strlen(rbp) - 1] = '\0';

                char *temp = calloc(strlen(setup) + strlen(rbp) + 64, sizeof(char));

                if (strcmp(base_type, "char") == 0)
                    sprintf(temp, "%s    movsx eax, byte %s+r10*1]\n", setup, rbp);
                else if (strcmp(base_type, "int") == 0)
                    sprintf(temp, "%s    mov eax, dword %s+r10*4]\n", setup, rbp);
                else {
                    sprintf(temp, "%s    movss xmm0, dword %s+r10*4]\n", setup, rbp);
                    is_float = true;
                }

                free(base_type);
                free(setup);
                setup = temp;
                break;
            }
            default: assert(false);
        }

        if (is_float)
            strcpy(left_value, "xmm0");
        else
            strcpy(left_value, "eax");

        code = realloc(code, (strlen(code) + strlen(setup) + 1) * sizeof(char));
        strcat(code, setup);
        free(setup);
        setup = NULL;

        switch (right->type) {
            case AST_INT:
                if (is_float) {
                    setup = calloc(128, sizeof(char));
                    sprintf(setup, "    mov eax, %d\n"
                                   "    cvtsi2ss xmm1, eax\n", (int)right->data.digit);
                    strcpy(right_value, "xmm1");
                } else
                    sprintf(right_value, "%d", (int)right->data.digit);
                break;
            case AST_FLOAT:
                if (!is_float) {
                    setup = calloc(64, sizeof(char));
                    strcpy(setup, "    cvtsi2ss xmm0, eax\n");
                    strcpy(left_value, "xmm0");
                    is_float = true;
                }

                sprintf(right_value, "dword [.f%zu]", float_init(32, right->data.digit));
                break;
            case AST_VAR: {
                AST *var = sym_find(AST_ASSIGN, right->scope_def, right->var.name);
                
                if (strcmp(var->assign.type, "char") == 0) {
                    if (is_float) {
                        setup = calloc(strlen(var->assign.rbp) + 128, sizeof(char));
                        sprintf(setup, "    movsx eax, byte %s\n"
                                       "    cvtsi2ss xmm1, eax\n", var->assign.rbp);
                        strcpy(right_value, "xmm1");
                    } else {
                        setup = calloc(strlen(var->assign.rbp) + 128, sizeof(char));
                        sprintf(setup, "    movsx ebx, byte %s\n", var->assign.rbp);
                        strcpy(right_value, "ebx");
                    }
                } else if (strcmp(var->assign.type, "int") == 0) {
                    if (is_float) {
                        setup = calloc(strlen(var->assign.rbp) + 128, sizeof(char));
                        sprintf(setup, "    cvtsi2ss xmm1, dword %s\n", var->assign.rbp);
                        strcpy(right_value, "xmm1");
                    } else
                        sprintf(right_value, "dword %s", var->assign.rbp);
                } else {
                    if (!is_float) {
                        setup = calloc(64, sizeof(char));
                        strcpy(setup, "    cvtsi2ss xmm0, eax\n");
                        strcpy(left_value, "xmm0");
                        is_float = true;
                    }

                    sprintf(right_value, "dword %s", var->assign.rbp);
                }
                break;
            }
            case AST_CALL:
            case AST_MATH: {
                char result_reg[16];

                if (right->type == AST_CALL) {
                    if (strcmp(sym_find(AST_FUNC, "<global>", right->call.name)->func.type, "float") == 0)
                        strcpy(result_reg, "xmm0");
                    else
                        strcpy(result_reg, "eax");
                } else {
                    if (float_math_result)
                        strcpy(result_reg, "xmm0");
                    else
                        strcpy(result_reg, "eax");
                }

                char *save;
                rsp += 8;

                if (is_float) {
                    if (rsp + 8 > rsp_cap) {
                        rsp_cap += 8;
                        setup = emit_ast(right);
                        rsp_cap -= 8;

                        save = calloc(strlen(setup) + 256, sizeof(char));

                        if (strcmp(result_reg, "xmm0") == 0)
                            sprintf(save, "    sub rsp, 8\n"
                                          "    movss dword [rbp-%zu], xmm0\n"
                                          "%s"
                                          "    movss xmm1, xmm0\n"
                                          "    movss xmm0, dword [rbp-%zu]\n"
                                          "    add rsp, 8\n", rsp, setup, rsp);
                        else
                            sprintf(save, "    sub rsp, 8\n"
                                          "    movss dword [rbp-%zu], xmm0\n"
                                          "%s"
                                          "    cvtsi2ss xmm1, eax\n"
                                          "    movss xmm0, dword [rbp-%zu]\n"
                                          "    add rsp, 8\n", rsp, setup, rsp);
                    } else {
                        setup = emit_ast(right);
                        save = calloc(strlen(setup) + 256, sizeof(char));

                        if (strcmp(result_reg, "xmm0") == 0)
                            sprintf(save, "    sub rsp, 8\n"
                                          "    movss dword [rbp-%zu], xmm0\n"
                                          "%s"
                                          "    movss xmm1, xmm0\n"
                                          "    movss xmm0, dword [rbp-%zu]\n"
                                          "    add rsp, 8\n", rsp, setup, rsp);
                        else
                            sprintf(save, "    movss dword [rbp-%zu], xmm0\n"
                                          "%s"
                                          "    cvtsi2ss xmm1, eax\n"
                                          "    movss xmm0, dword [rbp-%zu]\n", rsp, setup, rsp);
                    }

                    strcpy(right_value, "xmm1");
                } else {
                    rsp_cap += 8;
                    setup = emit_ast(right);
                    rsp_cap -= 8;

                    save = calloc(strlen(setup) + 256, sizeof(char));

                    if (strcmp(result_reg, "xmm0") == 0) {
                        sprintf(save, "    push rax\n"
                                      "%s"
                                      "    movss xmm1, xmm0\n"
                                      "    pop rax\n"
                                      "    cvtsi2ss xmm0, eax\n", setup);

                        strcpy(left_value, "xmm0");
                        strcpy(right_value, "xmm1");
                        is_float = true;
                    } else {
                        sprintf(save, "    push rax\n"
                                      "%s"
                                      "    mov ebx, eax\n"
                                      "    pop rax\n", setup);
                        strcpy(right_value, "ebx");
                    }
                }

                rsp -= 8;
                free(setup);
                setup = save;
                break;
            }
            case AST_DEREF: {
                AST *sym = sym_find(AST_ASSIGN, right->scope_def, right->deref.name);
                char *base_type = strdup(sym->assign.type);
                base_type[strlen(base_type) - 1] = '\0';

                char rbp[64];
                sprintf(rbp, "%s", sym->assign.rbp);
                rbp[strlen(rbp) - 1] = '\0';

                char *temp;
                rsp += 8;

                if (strcmp(base_type, "char") == 0) {
                    if (is_float) {
                        if (rsp + 8 > rsp_cap) {
                            rsp_cap += 8;
                            setup = emit_ast(right);
                            rsp_cap -= 8;
                            temp = calloc(strlen(setup) + strlen(rbp) + 256, sizeof(char));

                            sprintf(temp, "    sub rsp, 8\n"
                                        "    movss dword [rbp-%zu], xmm0\n"
                                        "%s"
                                        "    movsx eax, byte %s+r10*1]\n"
                                        "    cvtsi2ss xmm1, eax\n"
                                        "    movss xmm0, dword [rbp-%zu]\n"
                                        "    add rsp, 8\n", rsp + 8, setup, rbp, rsp + 8);

                        } else {
                            setup = emit_ast(right);
                            temp = calloc(strlen(setup) + strlen(rbp) + 128, sizeof(char));

                            sprintf(temp, "    movss dword [rbp-%zu], xmm0\n"
                                        "%s"
                                        "    movsx eax, byte %s+r10*1]\n"
                                        "    cvtsi2ss xmm1, eax\n"
                                        "    movss xmm0, dword [rbp-%zu]\n", rsp + 8, setup, rbp, rsp + 8);
                        }

                        strcpy(right_value, "xmm1");
                    } else {
                        setup = emit_ast(right);
                        temp = calloc(strlen(setup) + strlen(rbp) + 256, sizeof(char));

                        sprintf(temp, "    push rax\n"
                                    "%s"
                                    "    movsx ebx, byte %s+r10*1]\n"
                                    "    pop rax\n", setup, rbp);

                        strcpy(right_value, "ebx");
                    }
                } else if (strcmp(base_type, "int") == 0) {
                    if (is_float) {
                        if (rsp + 8 > rsp_cap) {
                            rsp_cap += 8;
                            setup = emit_ast(right);
                            rsp_cap -= 8;
                            temp = calloc(strlen(setup) + strlen(rbp) + 256, sizeof(char));

                            sprintf(temp, "    sub rsp, 8\n"
                                        "    movss dword [rbp-%zu], xmm0\n"
                                        "%s"
                                        "    cvtsi2ss xmm1, dword %s+r10*4]\n"
                                        "    movss xmm0, dword [rbp-%zu]\n"
                                        "    add rsp, 8\n", rsp + 8, setup, rbp, rsp + 8);

                        } else {
                            setup = emit_ast(right);
                            temp = calloc(strlen(setup) + strlen(rbp) + 128, sizeof(char));

                            sprintf(temp, "    movss dword [rbp-%zu], xmm0\n"
                                        "%s"
                                        "    cvtsi2ss xmm1, dword %s+r10*4]\n"
                                        "    movss xmm0, dword [rbp-%zu]\n", rsp + 8, setup, rbp, rsp + 8);
                        }

                        strcpy(right_value, "xmm1");
                    } else {
                        setup = emit_ast(right);
                        temp = calloc(strlen(setup) + strlen(rbp) + 256, sizeof(char));

                        sprintf(temp, "    push rax\n"
                                    "%s"
                                    "    pop rax\n", setup);

                        sprintf(right_value, "dword %s+r10*4]", rbp);
                    }
                } else {
                    if (is_float) {
                        if (rsp + 8 > rsp_cap) {
                            rsp_cap += 8;
                            setup = emit_ast(right);
                            rsp_cap -= 8;
                            temp = calloc(strlen(setup) + strlen(rbp) + 256, sizeof(char));

                            sprintf(temp, "    sub rsp, 8\n"
                                        "    movss dword [rbp-%zu], xmm0\n"
                                        "%s"
                                        "    movss xmm0, dword [rbp-%zu]\n"
                                        "    add rsp, 8\n", rsp + 8, setup, rsp + 8);

                        } else {
                            setup = emit_ast(right);
                            temp = calloc(strlen(setup) + strlen(rbp) + 128, sizeof(char));

                            sprintf(temp, "    movss dword [rbp-%zu], xmm0\n"
                                        "%s"
                                        "    movss xmm0, dword [rbp-%zu]\n", rsp + 8, setup, rsp + 8);
                        }

                        strcpy(right_value, "xmm1");
                    } else {
                        setup = emit_ast(right);
                        temp = calloc(strlen(setup) + strlen(rbp) + 256, sizeof(char));

                        sprintf(temp, "    push rax\n"
                                    "%s"
                                    "    pop rax\n"
                                    "    cvtsi2ss xmm0, eax\n", setup);
                        is_float = true;

                    }

                    sprintf(right_value, "dword %s+r10*4]", rbp);
                }

                free(base_type);
                rsp -= 8;
                free(setup);
                setup = temp;
                break;
            }
            default: assert(false);
        }

        if (setup != NULL) {
            code = realloc(code, (strlen(code) + strlen(setup) + 1) * sizeof(char));
            strcat(code, setup);
            free(setup);
        }

        char cmp_instr[7];
        if (is_float)
            strcpy(cmp_instr, "comiss");
        else
            strcpy(cmp_instr, "cmp");

        char jmp_instr[4];

        if (i + 2 < expr_cnt && expr[i + 2]->oper.kind == TOK_AND) {
            switch (oper) {
                case TOK_LT:
                    oper = TOK_GTE;
                    break;
                case TOK_LTE:
                    oper = TOK_GT;
                    break;
                case TOK_GT:
                    oper = TOK_LTE;
                    break;
                case TOK_GTE:
                    oper = TOK_LT;
                    break;
                case TOK_NOT_EQ:
                    oper = TOK_EQ_EQ;
                    break;
                default:
                    oper = TOK_NOT_EQ;
                    break;
            }

            label = false_label;
        } else
            label = true_label;

        switch (oper) {
            case TOK_LT:
                if (is_float)
                    strcpy(jmp_instr, "jb");
                else
                    strcpy(jmp_instr, "jl");
                break;
            case TOK_LTE:
                if (is_float)
                    strcpy(jmp_instr, "jbe");
                else
                    strcpy(jmp_instr, "jle");
                break;
            case TOK_GT:
                if (is_float)
                    strcpy(jmp_instr, "ja");
                else
                    strcpy(jmp_instr, "jg");
                break;
            case TOK_GTE:
                if (is_float)
                    strcpy(jmp_instr, "jae");
                else
                    strcpy(jmp_instr, "jge");
                break;
            case TOK_NOT_EQ:
                strcpy(jmp_instr, "jne");
                break;
            default:
                strcpy(jmp_instr, "je");
                break;
        }

        compare = calloc(strlen(left_value) + strlen(right_value) + strlen(cmp_instr) + strlen(jmp_instr) + 128, sizeof(char));
        sprintf(compare, "    %s %s, %s\n"
                         "    %s %s\n", cmp_instr, left_value, right_value, jmp_instr, label);

        code = realloc(code, (strlen(code) + strlen(compare) + 1) * sizeof(char));
        strcat(code, compare);
        free(compare);
    }

    return code;
}

char *emit_if_else(AST *ast) {
    char *if_label = label_init();
    char *else_label = label_init();
    char *cond = emit_cond(ast->if_else.exprs, ast->if_else.exprs_cnt, if_label, else_label);
    char *if_body = emit_arr(ast->if_else.body, ast->if_else.body_cnt, true);

    char *code = calloc(strlen(if_label) + strlen(else_label) + strlen(cond) + strlen(if_body) + 64, sizeof(char));
    sprintf(code, "%s"
                  "    jmp %s\n"
                  "%s:\n"
                  "%s", cond, else_label, if_label, if_body);

    if (ast->if_else.else_body != NULL) {
        char *final_label = label_init();
        char *else_body = emit_arr(ast->if_else.else_body, ast->if_else.else_body_cnt, true);

        char *extra = calloc(strlen(else_label) + (strlen(final_label) * 2) + strlen(else_body) + 64, sizeof(char));
        sprintf(extra, "    jmp %s\n"
                       "%s:\n"
                       "%s"
                       "%s:\n", final_label, else_label, else_body, final_label);

        code = realloc(code, (strlen(code) + strlen(extra) + 1) * sizeof(char));
        strcat(code, extra);
        free(extra);
        free(final_label);
        free(else_body);
    } else {
        char *temp = calloc(strlen(else_label) + 8, sizeof(char));
        sprintf(temp, "%s:\n", else_label);

        code = realloc(code, (strlen(code) + strlen(temp) + 1) * sizeof(char));
        strcat(code, temp);
        free(temp);
    }

    free(if_body);
    free(cond);
    free(if_label);
    free(else_label);
    return code;
}

char *emit_while(AST *ast) {
    char *body_label = label_init();
    char *end_label = label_init();
    char *cond = emit_cond(ast->while_.exprs, ast->while_.exprs_cnt, body_label, end_label);
    char *body = emit_arr(ast->while_.body, ast->while_.body_cnt, true);

    char *code;

    if (ast->while_.do_first) {
        code = calloc(strlen(body_label) + strlen(end_label)  + strlen(cond) + strlen(body) + 64, sizeof(char));
        sprintf(code, "%s:\n"
                      "%s"
                      "%s"
                      "%s:\n", body_label, body, cond, end_label);
    } else {
        char *cond_label = label_init();
        code = calloc((strlen(cond_label) * 2) + strlen(body_label) + (strlen(end_label) * 2) + strlen(cond) + strlen(body) + 64, sizeof(char));
        sprintf(code, "%s:\n"
                      "%s"
                      "    jmp %s\n"
                      "%s:\n"
                      "%s"
                      "    jmp %s\n"
                      "%s:\n", cond_label, cond, end_label, body_label, body, cond_label, end_label);
        free(cond_label);
    }

    free(body_label);
    free(end_label);
    free(cond);
    free(body);
    return code;
}

char *emit_for(AST *ast) {
    char *cond_label = label_init();
    char *body_label = label_init();
    char *end_label = label_init();

    char *init = emit_ast(ast->for_.init);
    char *cond = emit_cond(ast->for_.cond, ast->for_.cond_cnt, body_label, end_label);
    char *math = emit_ast(ast->for_.math);
    char *body = emit_arr(ast->for_.body, ast->for_.body_cnt, true);

    char *code = calloc(strlen(init) + strlen(cond) + strlen(math) + strlen(body) + ((strlen(cond_label) + strlen(end_label) + strlen(cond_label)) * 2) + 64, sizeof(char));
    sprintf(code, "%s"
                  "%s:\n"
                  "%s"
                  "    jmp %s\n"
                  "%s:\n"
                  "%s"
                  "%s"
                  "    jmp %s\n"
                  "%s:\n", init, cond_label, cond, end_label, body_label, body, math, cond_label, end_label);

    free(cond_label);
    free(body_label);
    free(end_label);
    free(init);
    free(cond);
    free(math);
    free(body);
    return code;
}

char *emit_subscr(AST *ast) {
    AST *index = ast->subscr.index;
    char *code;

    switch (index->type) {
        case AST_INT:
            // TODO: maybe just evaluate this into a constant?
            code = calloc(64, sizeof(char));
            sprintf(code, "    mov r10d, %d\n", (int)index->data.digit);
            break;
        case AST_VAR: {
            AST *var = sym_find(AST_ASSIGN, index->scope_def, index->var.name);
            code = calloc(strlen(var->assign.rbp) + 64, sizeof(char));

            if (strcmp(var->assign.type, "char") == 0)
                sprintf(code, "    movsx r10d, byte %s\n", var->assign.rbp);
            else if (strcmp(var->assign.type, "int") == 0)
                sprintf(code, "    mov r10d, dword %s\n", var->assign.rbp);
            else
                sprintf(code, "    cvttss2si r10d, dword %s\n", var->assign.rbp);
            break;
        }
        case AST_CALL:
        case AST_MATH:
            char *type;
            if (index->type == AST_CALL)
                type = sym_find(AST_FUNC, "<global>", index->call.name)->func.type;
            else {
                if (float_math_result)
                    type = "float";
                else
                    type = "int";
            }

            code = emit_ast(index);
            code = realloc(code, (strlen(code) + 64) * sizeof(char));

            if (strcmp(type, "char") == 0 || strcmp(type, "int") == 0)
                strcat(code, "    mov r10d, eax\n");
            else if (strcmp(type, "float") == 0)
                strcat(code, "    cvttss2si r10d, xmm0\n");
            else
                strcat(code, "    mov r10, rax\n");
            break;
        default: assert(false);
    }

    return code;
}

char *emit_deref_as_subscr(AST *ast) {
    AST *subscr = ast_init(AST_SUBSCR, ast->scope_def, ast->func_def, ast->ln, ast->col);
    subscr->subscr.name = ast->deref.name;
    subscr->subscr.value = ast->deref.value;
    subscr->subscr.index = ast_init(AST_INT, ast->scope_def, ast->func_def, ast->ln, ast->col);
    subscr->subscr.index->data.digit = 0;

    char *code = emit_ast(subscr);
    free(subscr->scope_def);
    free(subscr->func_def);
    free(subscr->subscr.index->scope_def);
    free(subscr->subscr.index->func_def);
    free(subscr->subscr.index);
    free(subscr);
    return code;
}

char *emit_ast(AST *ast) {
    switch (ast->type) {
        case AST_ROOT: return emit_root(ast);
        case AST_FUNC: return emit_func(ast);
        case AST_CALL: return emit_call(ast);
        case AST_ASSIGN: return emit_assign(ast);
        case AST_RET: return emit_ret(ast);
        case AST_MATH: return emit_math(ast);
        case AST_IF_ELSE: return emit_if_else(ast);
        case AST_WHILE: return emit_while(ast);
        case AST_FOR: return emit_for(ast);
        case AST_SUBSCR: return ast->subscr.value == NULL ? emit_subscr(ast) : emit_assign(ast);
        case AST_DEREF: return emit_deref_as_subscr(ast);
        default:
            fprintf(stderr, "steelc: error: missing backend for '%s'\n", ast_types[ast->type]);
            exit(EXIT_FAILURE);
    }
}
