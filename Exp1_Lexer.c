#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "Exp1_LexerAPI.h"

#ifdef PARSER_MODE
#include "Exp2_Parser.tab.h"

enum {
    YTOK_ID = ID,
    YTOK_INT8 = INT8,
    YTOK_INT10 = INT10,
    YTOK_INT16 = INT16,
    YTOK_FLOAT = FLOAT,
    YTOK_IF = IF,
    YTOK_THEN = THEN,
    YTOK_ELSE = ELSE,
    YTOK_WHILE = WHILE,
    YTOK_DO = DO,
    YTOK_BEGIN = BEGIN,
    YTOK_END = END,
    YTOK_GE = GE,
    YTOK_LE = LE,
    YTOK_NEQ = NEQ,
    YTOK_AND = AND,
    YTOK_OR = OR,
    YTOK_NOT = NOT
};

FILE *yyin = NULL;
int yylineno = 1;
int yytoken_column = 1;
int lexer_failed = 0;
int yy_prev_token_code = 0;
int yy_current_token_code = 0;
char yy_prev_token_text[256] = "";
char yy_current_token_text[256] = "";
int yy_recent_guard_keyword = 0;
int yy_open_paren_balance = 0;

static char *dup_text(const char *text) {
    size_t length = strlen(text);
    char *copy = (char *)malloc(length + 1);
    if (copy == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }
    memcpy(copy, text, length + 1);
    return copy;
}

static void copy_token_text(char *dest, size_t size, const char *src) {
    if (src == NULL) {
        dest[0] = '\0';
        return;
    }
    snprintf(dest, size, "%s", src);
}

static int emit_parser_token(int token_code, const char *text) {
    yy_prev_token_code = yy_current_token_code;
    copy_token_text(yy_prev_token_text, sizeof(yy_prev_token_text), yy_current_token_text);
    yy_current_token_code = token_code;
    copy_token_text(yy_current_token_text, sizeof(yy_current_token_text), text);

    if (token_code == IF) {
        yy_recent_guard_keyword = IF;
    }
    else if (token_code == WHILE) {
        yy_recent_guard_keyword = WHILE;
    }
    else if (token_code == THEN || token_code == DO || token_code == ';') {
        yy_recent_guard_keyword = 0;
    }

    if (token_code == '(') {
        yy_open_paren_balance++;
    }
    else if (token_code == ')' && yy_open_paren_balance > 0) {
        yy_open_paren_balance--;
    }

    return token_code;
}
#endif

// Token code definitions used by the original Experiment 1 lexer.
#define IDN     1
#define DEC     2
#define OCT     3
#define HEX     4
#define FLOAT_NUM 5
#define ADD     6   // +
#define SUB     7   // -
#define MUL     8   // *
#define DIV     9   // /
#define GT      10  // >
#define LT      11  // <
#define EQ      12  // =
#define GE_OP   13  // >=
#define LE_OP   14  // <=
#define NEQ_OP  15  // <>
#define SLP     16  // (
#define SRP     17  // )
#define SEMI    18  // ;
#define IF_KW    19
#define THEN_KW  20
#define ELSE_KW  21
#define WHILE_KW 22
#define DO_KW    23
#define BEGIN_KW 24
#define END_KW   25
#define AND_KW   26
#define OR_KW    27
#define NOT_KW   28
#define ILOCT   29
#define ILHEX   30
#define ENDFILE 0
#define ERROR   99

// Keyword table.
char *keywords[] = {"if", "then", "else", "while", "do", "begin", "end", "and", "or", "not"};
int   keycodes[] = {IF_KW, THEN_KW, ELSE_KW, WHILE_KW, DO_KW, BEGIN_KW, END_KW, AND_KW, OR_KW, NOT_KW};

// Double-buffer scanner state.
#define BUF_SIZE 128
char buf1[BUF_SIZE], buf2[BUF_SIZE];
int flag1 = 0, flag2 = 0;
int cur = 1;
int p = 0;
int len1 = 0, len2 = 0;
int scan_line = 1, scan_column = 1;
int prev_line = 1, prev_column = 1;

static FILE *current_input(void) {
#ifdef PARSER_MODE
    return yyin != NULL ? yyin : stdin;
#else
    return stdin;
#endif
}

int getch() {
    int ch;

    if (cur == 1) {
        if (!flag1) {
            len1 = (int)fread(buf1, 1, BUF_SIZE, current_input());
            flag1 = 1;
            if (len1 == 0) return EOF;
        }
        if (p >= len1) { cur = 2; p = 0; return getch(); }
        ch = (unsigned char)buf1[p++];
    } else {
        if (!flag2) {
            len2 = (int)fread(buf2, 1, BUF_SIZE, current_input());
            flag2 = 1;
            if (len2 == 0) return EOF;
        }
        if (p >= len2) { cur = 1; p = 0; flag1 = flag2 = 0; return getch(); }
        ch = (unsigned char)buf2[p++];
    }

    prev_line = scan_line;
    prev_column = scan_column;
    if (ch == '\n') {
        scan_line++;
        scan_column = 1;
    } else {
        scan_column++;
    }
    return ch;
}

void back() {
    if (p > 0) {
        p--;
        scan_line = prev_line;
        scan_column = prev_column;
    }
}

void skip_blank() {
    int c;
    while (1) {
        c = getch();
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            continue;
        }
        back();
        break;
    }
}

int search(char *s) {
    for (int i = 0; i < 10; i++)
        if (strcmp(s, keywords[i]) == 0)
            return keycodes[i];
    return IDN;
}

// Lexical analysis function.
int  code;
char token[256];

/* 重新初始化双缓冲扫描器和词法分析上下文。 */
void Exp1_ResetLexerState(void) {
    flag1 = 0;
    flag2 = 0;
    cur = 1;
    p = 0;
    len1 = 0;
    len2 = 0;
    scan_line = 1;
    scan_column = 1;
    prev_line = 1;
    prev_column = 1;
    code = ENDFILE;
    token[0] = '\0';

#ifdef PARSER_MODE
    yylineno = 1;
    yytoken_column = 1;
    lexer_failed = 0;
    yy_prev_token_code = 0;
    yy_current_token_code = 0;
    yy_prev_token_text[0] = '\0';
    yy_current_token_text[0] = '\0';
    yy_recent_guard_keyword = 0;
    yy_open_paren_balance = 0;
#endif
}

void scanner() {
    int ch;
    int idx = 0;
    code = ENDFILE;
    token[0] = '\0';

    skip_blank();
#ifdef PARSER_MODE
    yylineno = scan_line;
    yytoken_column = scan_column;
#endif
    ch = getch();
    if (ch == EOF) { code = ENDFILE; return; }

    // Identifiers and keywords.
    if (isalpha((unsigned char)ch)) {
        while (isalpha((unsigned char)ch) || isdigit((unsigned char)ch)) {
            token[idx++] = (char)ch;
            ch = getch();
        }
        back();
        token[idx] = '\0';
        code = search(token);
        return;
    }

    // Numbers.
    if (isdigit((unsigned char)ch)) {
        if (ch == '0') {
            token[idx++] = (char)ch;
            ch = getch();

            // Hexadecimal 0x / 0X.
            if (ch == 'x' || ch == 'X') {
                token[idx++] = (char)ch;
                ch = getch();
                int ill = 0;
                int has_hex_digit = 0;
                while (isalnum((unsigned char)ch)) {
                    has_hex_digit = 1;
                    token[idx++] = (char)ch;
                    if ((ch >= 'g' && ch <= 'z') || (ch >= 'G' && ch <= 'Z')) ill = 1;
                    ch = getch();
                }
                back();
                token[idx] = '\0';
                code = (!has_hex_digit || ill) ? ILHEX : HEX;
                return;
            }

            // Octal 0...
            else if (isdigit((unsigned char)ch)) {
                int ill = 0;
                while (isdigit((unsigned char)ch)) {
                    token[idx++] = (char)ch;
                    if (ch == '8' || ch == '9') ill = 1;
                    ch = getch();
                }
                back();
                token[idx] = '\0';
                code = ill ? ILOCT : OCT;
                return;
            }

            /* 浮点数 0.xxx 或 0.
             * 这里沿用你组员的新规则：只要看到 0 后面紧跟小数点，
             * 就把它当成浮点数继续读。 */
            else if (ch == '.') {
                token[idx++] = (char)ch;
                ch = getch();
                while (isdigit((unsigned char)ch)) {
                    token[idx++] = (char)ch;
                    ch = getch();
                }
                back();
                token[idx] = '\0';
                code = FLOAT_NUM;
                return;
            }

            // Single 0.
            else {
                back();
                token[idx] = '\0';
                code = DEC;
                return;
            }
        }
        /* 十进制整数或浮点数。
         * 先读整数部分，如果后面还有小数点，就继续读取小数部分。 */
        else {
            while (isdigit((unsigned char)ch)) {
                token[idx++] = (char)ch;
                ch = getch();
            }
            if (ch == '.') {
                token[idx++] = (char)ch;
                ch = getch();
                while (isdigit((unsigned char)ch)) {
                    token[idx++] = (char)ch;
                    ch = getch();
                }
                back();
                token[idx] = '\0';
                code = FLOAT_NUM;
                return;
            }
            back();
            token[idx] = '\0';
            code = DEC;
            return;
        }
    }

    // Operators and separators.
    token[idx++] = (char)ch;
    token[idx] = '\0';
    switch (ch) {
        case '+': code = ADD; break;
        case '-': code = SUB; break;
        case '*': code = MUL; break;
        case '/': code = DIV; break;
        case '(': code = SLP; break;
        case ')': code = SRP; break;
        case ';': code = SEMI; break;
        case '=': code = EQ; break;
        case '>':
            ch = getch();
            if (ch == '=') { code = GE_OP; strcpy(token, ">="); }
            else { code = GT; back(); }
            break;
        case '<':
            ch = getch();
            if (ch == '=') { code = LE_OP; strcpy(token, "<="); }
            else if (ch == '>') { code = NEQ_OP; strcpy(token, "<>"); }
            else { code = LT; back(); }
            break;
        default:
            code = ERROR;
            token[0] = (char)ch;
            token[1] = '\0';
            break;
    }
}

// Output helpers for standalone Experiment 1 mode.
void print(int c) {
    switch (c) {
        case IDN:   printf("IDN  "); break;
        case DEC:   printf("DEC  "); break;
        case OCT:   printf("OCT  "); break;
        case HEX:   printf("HEX  "); break;
        case FLOAT_NUM: printf("FLOAT"); break;
        case ADD:   printf("ADD  "); break;
        case SUB:   printf("SUB  "); break;
        case MUL:   printf("MUL  "); break;
        case DIV:   printf("DIV  "); break;
        case GT:    printf("GT   "); break;
        case LT:    printf("LT   "); break;
        case EQ:    printf("EQ   "); break;
        case GE_OP: printf("GE   "); break;
        case LE_OP: printf("LE   "); break;
        case NEQ_OP: printf("NEQ  "); break;
        case SLP:   printf("SLP  "); break;
        case SRP:   printf("SRP  "); break;
        case SEMI:  printf("SEMI "); break;
        case IF_KW:    printf("IF   "); break;
        case THEN_KW:  printf("THEN "); break;
        case ELSE_KW:  printf("ELSE "); break;
        case WHILE_KW: printf("WHILE"); break;
        case DO_KW:    printf("DO   "); break;
        case BEGIN_KW: printf("BEGIN"); break;
        case END_KW:   printf("END  "); break;
        case AND_KW:   printf("AND  "); break;
        case OR_KW:    printf("OR   "); break;
        case NOT_KW:   printf("NOT  "); break;
        case ILOCT: printf("ILOCT"); break;
        case ILHEX: printf("ILHEX"); break;
        case ERROR: printf("ERROR"); break;
        default:    printf("UNKNOWN"); break;
    }
}

/* 使用实验一原有输出格式，打印一次完整词法分析结果。 */
static int run_lexer_loop(void) {
    while (1) {
        scanner();
        if (code == ENDFILE) break;

#ifdef PARSER_MODE
        if (code == ILOCT || code == ILHEX || code == ERROR) {
            lexer_failed = 1;
        }
#endif

        print(code);

        if (code == ILOCT || code == ILHEX) {
            printf(" -");
        }
        else if (code == IDN || code == DEC || code == OCT || code == HEX || code == FLOAT_NUM) {
            unsigned long num;
            if (code == OCT) {
                num = strtoul(token, NULL, 8);
                printf(" %lu", num);
            }
            else if (code == HEX) {
                num = strtoul(token, NULL, 16);
                printf(" %lu", num);
            }
            else if (code == FLOAT_NUM) {
                printf(" %s", token);
            }
            else {
                printf(" %s", token);
            }
        }
        else if (code != ERROR) {
            printf(" -");
        }
        else {
            printf(" Invalid Char:%s", token);
        }

        printf("\n");
    }

#ifdef PARSER_MODE
    return lexer_failed ? 1 : 0;
#else
    return 0;
#endif
}

#ifdef PARSER_MODE
/* 供统一主程序调用：读取文件并执行实验一词法分析。 */
int Exp1_RunLexerFromFile(const char *input_path) {
    FILE *input = fopen(input_path, "r");
    int result;

    if (input == NULL) {
        fprintf(stderr, "Input file not found: %s\n", input_path);
        return 1;
    }

    Exp1_ResetLexerState();
    yyin = input;
    result = run_lexer_loop();
    fclose(yyin);
    yyin = NULL;
    printf("\nLexical analysis finished.\n");
    return result;
}
#endif

#ifdef PARSER_MODE
int yylex(void) {
    scanner();
    switch (code) {
        case ENDFILE:
            return emit_parser_token(0, "end of file");
        case IDN:
            yylval.text = dup_text(token);
            return emit_parser_token(YTOK_ID, token);
        case DEC:
            yylval.text = dup_text(token);
            return emit_parser_token(YTOK_INT10, token);
        case OCT:
            yylval.text = dup_text(token);
            return emit_parser_token(YTOK_INT8, token);
        case HEX:
            yylval.text = dup_text(token);
            return emit_parser_token(YTOK_INT16, token);
        case FLOAT_NUM:
            yylval.text = dup_text(token);
            return emit_parser_token(YTOK_FLOAT, token);
        case ADD:
            return emit_parser_token('+', "+");
        case SUB:
            return emit_parser_token('-', "-");
        case MUL:
            return emit_parser_token('*', "*");
        case DIV:
            return emit_parser_token('/', "/");
        case GT:
            return emit_parser_token('>', ">");
        case LT:
            return emit_parser_token('<', "<");
        case EQ:
            return emit_parser_token('=', "=");
        case GE_OP:
            return emit_parser_token(YTOK_GE, ">=");
        case LE_OP:
            return emit_parser_token(YTOK_LE, "<=");
        case NEQ_OP:
            return emit_parser_token(YTOK_NEQ, "<>");
        case SLP:
            return emit_parser_token('(', "(");
        case SRP:
            return emit_parser_token(')', ")");
        case SEMI:
            return emit_parser_token(';', ";");
        case IF_KW:
            return emit_parser_token(YTOK_IF, token);
        case THEN_KW:
            return emit_parser_token(YTOK_THEN, token);
        case ELSE_KW:
            return emit_parser_token(YTOK_ELSE, token);
        case WHILE_KW:
            return emit_parser_token(YTOK_WHILE, token);
        case DO_KW:
            return emit_parser_token(YTOK_DO, token);
        case BEGIN_KW:
            return emit_parser_token(YTOK_BEGIN, token);
        case END_KW:
            return emit_parser_token(YTOK_END, token);
        case AND_KW:
            return emit_parser_token(YTOK_AND, token);
        case OR_KW:
            return emit_parser_token(YTOK_OR, token);
        case NOT_KW:
            return emit_parser_token(YTOK_NOT, token);
        case ILOCT:
            fprintf(stderr, "Lexical error at %d:%d: ILOCT for token '%s'.\n", yylineno, yytoken_column, token);
            lexer_failed = 1;
            return 0;
        case ILHEX:
            fprintf(stderr, "Lexical error at %d:%d: ILHEX for token '%s'.\n", yylineno, yytoken_column, token);
            lexer_failed = 1;
            return 0;
        default:
            fprintf(stderr, "Lexical error at %d:%d: ERROR for token '%s'.\n", yylineno, yytoken_column, token);
            lexer_failed = 1;
            return 0;
    }
}
#else
// Standalone Experiment 1 main function.
int main() {
    printf("=========================================================\n");
    printf("            Lexical Analyzer (Double Buffer)\n");
    printf("        Press Ctrl+Z (Windows) to end input\n");
    printf("=========================================================\n\n");

    Exp1_ResetLexerState();
    run_lexer_loop();

    printf("\nLexical analysis finished.\n");
    return 0;
}
#endif
