%{
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Exp1_LexerAPI.h"
#include "Exp2_SyntaxTree.h"

/* 最终语法树根节点，以及成功后要打印的归约序列。 */
static Node *syntax_root = NULL;
static char reduction_logs[4096][128];
static int reduction_count = 0;
static int parse_failed = 0;
static int syntax_error_count = 0;

extern FILE *yyin;
extern int yylex(void);
extern int yylineno;
extern int yytoken_column;
extern int lexer_failed;

static char *xstrdup(const char *text) {
    size_t length = strlen(text);
    char *copy = (char *)malloc(length + 1);
    if (copy == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }
    memcpy(copy, text, length + 1);
    return copy;
}

/* 创建一个可带任意数量孩子节点的通用语法树节点。 */
static Node *make_node(const char *label, int child_count, ...) {
    int index;
    va_list args;
    Node *node = (Node *)malloc(sizeof(Node));
    if (node == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    node->label = xstrdup(label);
    node->child_count = child_count;
    node->children = child_count > 0 ? (Node **)malloc(sizeof(Node *) * child_count) : NULL;
    if (child_count > 0 && node->children == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    va_start(args, child_count);
    for (index = 0; index < child_count; index++) {
        node->children[index] = va_arg(args, Node *);
    }
    va_end(args);
    return node;
}

static Node *make_leaf(const char *label) {
    return make_node(label, 0);
}

/* 把单词包装成 id(x)、int10(15) 这种更便于阅读的叶子节点。 */
static Node *make_token_node(const char *kind, const char *lexeme) {
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s(%s)", kind, lexeme);
    return make_leaf(buffer);
}

/* 记录归约顺序，便于语法分析完成后统一打印。 */
static void record_reduction(const char *text) {
    if (reduction_count < 4096) {
        snprintf(reduction_logs[reduction_count], sizeof(reduction_logs[reduction_count]), "%s", text);
        reduction_count++;
    }
}

static void print_indent(int depth) {
    int index;
    for (index = 0; index < depth; index++) {
        printf("  ");
    }
}

static void print_tree(const Node *node, const char *prefix, int is_last, int show_branch) {
    int index;
    char next_prefix[512];

    if (node == NULL) {
        return;
    }

    if (show_branch) {
        printf("%s", prefix);
        printf("%s", is_last ? "`- " : "|- ");
    }
    printf("%s\n", node->label);

    snprintf(next_prefix, sizeof(next_prefix), "%s%s", prefix, show_branch ? (is_last ? "   " : "|  ") : "");
    for (index = 0; index < node->child_count; index++) {
        print_tree(node->children[index], next_prefix, index == node->child_count - 1, 1);
    }
}

static void free_tree(Node *node) {
    int index;
    if (node == NULL) {
        return;
    }
    for (index = 0; index < node->child_count; index++) {
        free_tree(node->children[index]);
    }
    free(node->children);
    free(node->label);
    free(node);
}

extern int yy_prev_token_code;
extern int yy_current_token_code;
extern char yy_prev_token_text[256];
extern char yy_current_token_text[256];
extern int yy_recent_guard_keyword;
extern int yy_open_paren_balance;

static const char *token_label(int token_code, const char *token_text);
static int token_starts_statement(int token_code);
static int token_can_end_expression(int token_code);
static void build_syntax_hint(char *buffer, size_t size);
static void reset_parse_state(void);
void yyerror(const char *message);
%}

%union {
    char *text;
    void *node;
}

%token <text> ID INT8 INT10 INT16 FLOAT
%token IF THEN ELSE WHILE DO BEGIN END GE LE NEQ AND OR NOT

%type <node> input program line stmt cond bool_term bool_factor rel expr term factor

/* 让每个 else 都优先匹配最近的、尚未匹配的 if。 */
%nonassoc LOWER_THAN_ELSE
%nonassoc ELSE
%left OR
%left AND
%right NOT
%right UMINUS

%%

input
    : program
      {
          syntax_root = $1;
      }
    ;

program
    : line
      {
          $$ = make_node("P", 1, $1);
          record_reduction("P -> L");
      }
    | line program
      {
          $$ = make_node("P", 2, $1, $2);
          record_reduction("P -> L P");
      }
    ;

line
    : stmt ';'
      {
          $$ = make_node("L", 2, $1, make_leaf(";"));
          record_reduction("L -> S ;");
      }
    | stmt error
      {
          $$ = make_node("L", 2, $1, make_leaf("<missing-;>"));
          record_reduction("L -> S error");
          yyerrok;
      }
    | error ';'
      {
          $$ = make_node("L", 2, make_leaf("<error-stmt>"), make_leaf(";"));
          record_reduction("L -> error ;");
          yyerrok;
      }
    ;

stmt
    : ID '=' expr
      {
          Node *id_node = make_token_node("id", $1);
          free($1);
          $$ = make_node("S", 3, id_node, make_leaf("="), $3);
          record_reduction("S -> id = E");
      }
    | IF cond THEN stmt %prec LOWER_THAN_ELSE
      {
          $$ = make_node("S", 4, make_leaf("if"), $2, make_leaf("then"), $4);
          record_reduction("S -> if C then S");
      }
    | IF cond THEN stmt ELSE stmt
      {
          $$ = make_node("S", 6, make_leaf("if"), $2, make_leaf("then"), $4, make_leaf("else"), $6);
          record_reduction("S -> if C then S else S");
      }
    | IF cond error stmt %prec LOWER_THAN_ELSE
      {
          $$ = make_node("S", 4, make_leaf("if"), $2, make_leaf("<missing-then>"), $4);
          record_reduction("S -> if C error S");
          yyerrok;
      }
    | IF cond error stmt ELSE stmt
      {
          $$ = make_node("S", 6, make_leaf("if"), $2, make_leaf("<missing-then>"), $4, make_leaf("else"), $6);
          record_reduction("S -> if C error S else S");
          yyerrok;
      }
    | WHILE cond DO stmt
      {
          $$ = make_node("S", 4, make_leaf("while"), $2, make_leaf("do"), $4);
          record_reduction("S -> while C do S");
      }
    | WHILE cond error stmt
      {
          $$ = make_node("S", 4, make_leaf("while"), $2, make_leaf("<missing-do>"), $4);
          record_reduction("S -> while C error S");
          yyerrok;
      }
    | BEGIN END
      {
          $$ = make_node("S", 2, make_leaf("begin"), make_leaf("end"));
          record_reduction("S -> begin end");
      }
    | BEGIN program END
      {
          $$ = make_node("S", 3, make_leaf("begin"), $2, make_leaf("end"));
          record_reduction("S -> begin P end");
      }
    | BEGIN program error
      {
          $$ = make_node("S", 3, make_leaf("begin"), $2, make_leaf("<missing-end>"));
          record_reduction("S -> begin P error");
          yyerrok;
      }
    | BEGIN error END
      {
          $$ = make_node("S", 3, make_leaf("begin"), make_leaf("<error-block>"), make_leaf("end"));
          record_reduction("S -> begin error end");
          yyerrok;
      }
    ;

cond
    : cond OR bool_term
      {
          $$ = make_node("C", 3, $1, make_leaf("or"), $3);
          record_reduction("C -> C or B");
      }
    | bool_term
      {
          $$ = make_node("C", 1, $1);
          record_reduction("C -> B");
      }
    ;

bool_term
    : bool_term AND bool_factor
      {
          $$ = make_node("B", 3, $1, make_leaf("and"), $3);
          record_reduction("B -> B and N");
      }
    | bool_factor
      {
          $$ = make_node("B", 1, $1);
          record_reduction("B -> N");
      }
    ;

bool_factor
    : NOT bool_factor
      {
          $$ = make_node("N", 2, make_leaf("not"), $2);
          record_reduction("N -> not N");
      }
    | '(' cond ')'
      {
          $$ = make_node("N", 3, make_leaf("("), $2, make_leaf(")"));
          record_reduction("N -> ( C )");
      }
    | rel
      {
          $$ = make_node("N", 1, $1);
          record_reduction("N -> R");
      }
    ;

rel
    : expr '>' expr
      {
          $$ = make_node("R", 3, $1, make_leaf(">"), $3);
          record_reduction("R -> E > E");
      }
    | expr '<' expr
      {
          $$ = make_node("R", 3, $1, make_leaf("<"), $3);
          record_reduction("R -> E < E");
      }
    | expr '=' expr
      {
          $$ = make_node("R", 3, $1, make_leaf("="), $3);
          record_reduction("R -> E = E");
      }
    | expr GE expr
      {
          $$ = make_node("R", 3, $1, make_leaf(">="), $3);
          record_reduction("R -> E >= E");
      }
    | expr LE expr
      {
          $$ = make_node("R", 3, $1, make_leaf("<="), $3);
          record_reduction("R -> E <= E");
      }
    | expr NEQ expr
      {
          $$ = make_node("R", 3, $1, make_leaf("<>"), $3);
          record_reduction("R -> E <> E");
      }
    ;

expr
    : expr '+' term
      {
          $$ = make_node("E", 3, $1, make_leaf("+"), $3);
          record_reduction("E -> E + T");
      }
    | expr '-' term
      {
          $$ = make_node("E", 3, $1, make_leaf("-"), $3);
          record_reduction("E -> E - T");
      }
    | term
      {
          $$ = make_node("E", 1, $1);
          record_reduction("E -> T");
      }
    ;

term
    : term '*' factor
      {
          $$ = make_node("T", 3, $1, make_leaf("*"), $3);
          record_reduction("T -> T * F");
      }
    | term '/' factor
      {
          $$ = make_node("T", 3, $1, make_leaf("/"), $3);
          record_reduction("T -> T / F");
      }
    | factor
      {
          $$ = make_node("T", 1, $1);
          record_reduction("T -> F");
      }
    ;

factor
    : '(' expr ')'
      {
          $$ = make_node("F", 3, make_leaf("("), $2, make_leaf(")"));
          record_reduction("F -> ( E )");
      }
    | '-' factor %prec UMINUS
      {
          $$ = make_node("F", 2, make_leaf("-"), $2);
          record_reduction("F -> - F");
      }
    | ID
      {
          $$ = make_node("F", 1, make_token_node("id", $1));
          free($1);
          record_reduction("F -> id");
      }
    | INT8
      {
          $$ = make_node("F", 1, make_token_node("int8", $1));
          free($1);
          record_reduction("F -> int8");
      }
    | INT10
      {
          $$ = make_node("F", 1, make_token_node("int10", $1));
          free($1);
          record_reduction("F -> int10");
      }
    | INT16
      {
          $$ = make_node("F", 1, make_token_node("int16", $1));
          free($1);
          record_reduction("F -> int16");
      }
    | FLOAT
      {
          $$ = make_node("F", 1, make_token_node("float", $1));
          free($1);
          record_reduction("F -> float");
      }
    ;

%%

static const char *token_label(int token_code, const char *token_text) {
    if (token_text != NULL && token_text[0] != '\0') {
        return token_text;
    }

    switch (token_code) {
        case 0: return "end of file";
        case ID: return "identifier";
        case INT8: return "int8";
        case INT10: return "int10";
        case INT16: return "int16";
        case FLOAT: return "float";
        case IF: return "if";
        case THEN: return "then";
        case ELSE: return "else";
        case WHILE: return "while";
        case DO: return "do";
        case BEGIN: return "begin";
        case END: return "end";
        case GE: return ">=";
        case LE: return "<=";
        case NEQ: return "<>";
        case AND: return "and";
        case OR: return "or";
        case NOT: return "not";
        case '+': return "+";
        case '-': return "-";
        case '*': return "*";
        case '/': return "/";
        case '(': return "(";
        case ')': return ")";
        case ';': return ";";
        case '=': return "=";
        case '>': return ">";
        case '<': return "<";
        default: return "unknown token";
    }
}

static int token_starts_statement(int token_code) {
    return token_code == ID || token_code == IF || token_code == WHILE || token_code == BEGIN;
}

static int token_can_end_expression(int token_code) {
    return token_code == ID || token_code == INT8 || token_code == INT10 ||
           token_code == INT16 || token_code == FLOAT ||
           token_code == ')' || token_code == END;
}

static void build_syntax_hint(char *buffer, size_t size) {
    const char *current = token_label(yy_current_token_code, yy_current_token_text);
    buffer[0] = '\0';

    if (yy_recent_guard_keyword == IF &&
        token_starts_statement(yy_current_token_code) &&
        token_can_end_expression(yy_prev_token_code)) {
        snprintf(buffer, size, "possible missing 'then' before '%s'", current);
        return;
    }

    if (yy_recent_guard_keyword == WHILE &&
        token_starts_statement(yy_current_token_code) &&
        token_can_end_expression(yy_prev_token_code)) {
        snprintf(buffer, size, "possible missing 'do' before '%s'", current);
        return;
    }

    if (yy_open_paren_balance > 0 &&
        token_can_end_expression(yy_prev_token_code) &&
        (token_starts_statement(yy_current_token_code) ||
         yy_current_token_code == THEN ||
         yy_current_token_code == DO ||
         yy_current_token_code == AND ||
         yy_current_token_code == OR ||
         yy_current_token_code == END ||
         yy_current_token_code == ';')) {
        snprintf(buffer, size, "possible missing ')' before '%s'", current);
        return;
    }

    if ((token_starts_statement(yy_current_token_code) ||
         yy_current_token_code == END ||
         yy_current_token_code == 0) &&
        token_can_end_expression(yy_prev_token_code)) {
        snprintf(buffer, size, "possible missing ';' before '%s'", current);
    }
}

void yyerror(const char *message) {
    char hint[256];
    const char *current;

    if (lexer_failed) {
        return;
    }

    parse_failed = 1;
    syntax_error_count++;
    build_syntax_hint(hint, sizeof(hint));
    current = token_label(yy_current_token_code, yy_current_token_text);

    if (hint[0] != '\0') {
        fprintf(stderr, "Syntax error at %d:%d near '%s': %s (%s)\n",
                yylineno, yytoken_column, current, message, hint);
    } else {
        fprintf(stderr, "Syntax error at %d:%d near '%s': %s\n",
                yylineno, yytoken_column, current, message);
    }
}

/* 重新初始化实验二保存的语法树、归约序列和错误状态。 */
static void reset_parse_state(void) {
    if (syntax_root != NULL) {
        free_tree(syntax_root);
        syntax_root = NULL;
    }
    reduction_count = 0;
    parse_failed = 0;
    syntax_error_count = 0;
    Exp1_ResetLexerState();
}

/* 供统一主程序调用：读取文件并执行实验二语法分析。 */
int Exp2_ParseFile(const char *input_path) {
    reset_parse_state();

    yyin = fopen(input_path, "r");
    if (yyin == NULL) {
        fprintf(stderr, "Input file not found: %s\n", input_path);
        return 1;
    }

    if (yyparse() == 0 && !lexer_failed && !parse_failed) {
        fclose(yyin);
        yyin = NULL;
        return 0;
    }

    fclose(yyin);
    yyin = NULL;
    if (parse_failed && syntax_error_count > 0) {
        fprintf(stderr, "Parsing finished with %d syntax error(s).\n", syntax_error_count);
    }
    if (syntax_root != NULL) {
        free_tree(syntax_root);
        syntax_root = NULL;
    }
    return 1;
}

/* 返回最近一次语法分析得到的语法树根节点。 */
Node *Exp2_GetSyntaxRoot(void) {
    return syntax_root;
}

/* 按实验二原有风格输出语法树。 */
void Exp2_PrintSyntaxTree(void) {
    printf("Syntax tree:\n");
    if (syntax_root == NULL) {
        printf("<empty>\n");
        return;
    }
    print_tree(syntax_root, "", 1, 0);
}

/* 按实验二原有风格输出归约序列。 */
void Exp2_PrintReductions(void) {
    int index;
    printf("Reductions:\n");
    for (index = 0; index < reduction_count; index++) {
        printf("%02d. %s\n", index + 1, reduction_logs[index]);
    }
}

/* 释放最近一次语法分析得到的语法树。 */
void Exp2_FreeSyntaxTree(void) {
    if (syntax_root != NULL) {
        free_tree(syntax_root);
        syntax_root = NULL;
    }
}

#ifndef EXP2_DISABLE_STANDALONE_MAIN
int main(int argc, char *argv[]) {
    const char *input_path = argc > 1 ? argv[1] : "Exp2_Input_Sample.txt";

    if (Exp2_ParseFile(input_path) == 0) {
        printf("Parse succeeded.\n\n");
        Exp2_PrintSyntaxTree();
        printf("\n");
        Exp2_PrintReductions();
        Exp2_FreeSyntaxTree();
        return 0;
    }

    return 1;
}
#endif
