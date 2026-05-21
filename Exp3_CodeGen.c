#include "Exp3_CodeGen.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* CodeBuffer 用来暂存生成好的三地址码文本行。 */
typedef struct CodeBuffer {
    char **lines;
    int count;
    int capacity;
    int temp_counter;
    int label_counter;
} CodeBuffer;

/* 初始化代码缓冲区和临时变量/标签计数器。 */
static void code_buffer_init(CodeBuffer *buffer);

/* 释放代码缓冲区里动态申请的文本行。 */
static void code_buffer_free(CodeBuffer *buffer);

/* 向缓冲区追加一行三地址码。 */
static void emit_line(CodeBuffer *buffer, const char *format, ...);

/* 追加一个标签行，例如 L0: */
static void emit_label(CodeBuffer *buffer, const char *label);

/* 生成新的临时变量名，例如 t1、t2。 */
static char *new_temp(CodeBuffer *buffer);

/* 生成新的标签名，例如 L0、L1。 */
static char *new_label(CodeBuffer *buffer);

/* 判断一个语法树结点的标签是否等于指定字符串。 */
static int is_label(const Node *node, const char *label);

/* 判断字符串是否具有指定前缀。 */
static int starts_with(const char *text, const char *prefix);

/* 复制一份字符串，供调用者长期保存。 */
static char *duplicate_text(const char *text);

/* 从 id(a)、int10(15) 这种结点标签中取出括号里的值。 */
static char *extract_wrapped_value(const char *label, const char *prefix);

/* 把实验二语法树叶子结点转换成实验三使用的操作数文本。 */
static char *leaf_value_to_operand(const Node *node);

/* 根据表达式子树生成三地址码，并返回表达式结果所在的位置。 */
static char *generate_expr_code(CodeBuffer *buffer, const Node *node);

/* 根据条件子树生成跳转代码。 */
static void generate_cond_code(CodeBuffer *buffer, const Node *node,
                               const char *true_label, const char *false_label);

/* 根据语句子树生成三地址码。 */
static void generate_stmt_code(CodeBuffer *buffer, const Node *node,
                               const char *entry_label);

/* 根据一行语句结点生成三地址码。 */
static void generate_line_code(CodeBuffer *buffer, const Node *node);

/* 根据程序结点递归生成全部三地址码。 */
static void generate_program_code(CodeBuffer *buffer, const Node *node);

static void code_buffer_init(CodeBuffer *buffer) {
    buffer->lines = NULL;
    buffer->count = 0;
    buffer->capacity = 0;
    buffer->temp_counter = 1;
    buffer->label_counter = 0;
}

static void code_buffer_free(CodeBuffer *buffer) {
    int index;
    for (index = 0; index < buffer->count; index++) {
        free(buffer->lines[index]);
    }
    free(buffer->lines);
    buffer->lines = NULL;
    buffer->count = 0;
    buffer->capacity = 0;
}

static void emit_line(CodeBuffer *buffer, const char *format, ...) {
    va_list args;
    char temp[512];
    char *line;

    va_start(args, format);
    vsnprintf(temp, sizeof(temp), format, args);
    va_end(args);

    if (buffer->count == buffer->capacity) {
        int new_capacity = buffer->capacity == 0 ? 32 : buffer->capacity * 2;
        char **new_lines = (char **)realloc(buffer->lines, sizeof(char *) * new_capacity);
        if (new_lines == NULL) {
            fprintf(stderr, "Out of memory.\n");
            exit(1);
        }
        buffer->lines = new_lines;
        buffer->capacity = new_capacity;
    }

    line = duplicate_text(temp);
    buffer->lines[buffer->count++] = line;
}

static void emit_label(CodeBuffer *buffer, const char *label) {
    emit_line(buffer, "%s:", label);
}

static char *new_temp(CodeBuffer *buffer) {
    char temp[32];
    snprintf(temp, sizeof(temp), "t%d", buffer->temp_counter++);
    return duplicate_text(temp);
}

static char *new_label(CodeBuffer *buffer) {
    char temp[32];
    snprintf(temp, sizeof(temp), "L%d", buffer->label_counter++);
    return duplicate_text(temp);
}

static int is_label(const Node *node, const char *label) {
    return node != NULL && strcmp(node->label, label) == 0;
}

static int starts_with(const char *text, const char *prefix) {
    size_t prefix_length = strlen(prefix);
    return strncmp(text, prefix, prefix_length) == 0;
}

static char *duplicate_text(const char *text) {
    size_t length = strlen(text);
    char *copy = (char *)malloc(length + 1);
    if (copy == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }
    memcpy(copy, text, length + 1);
    return copy;
}

static char *extract_wrapped_value(const char *label, const char *prefix) {
    size_t prefix_length = strlen(prefix);
    size_t label_length = strlen(label);
    size_t value_length;
    char *value;

    if (!starts_with(label, prefix) || label_length <= prefix_length || label[label_length - 1] != ')') {
        return duplicate_text(label);
    }

    value_length = label_length - prefix_length - 1;
    value = (char *)malloc(value_length + 1);
    if (value == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }
    memcpy(value, label + prefix_length, value_length);
    value[value_length] = '\0';
    return value;
}

static char *leaf_value_to_operand(const Node *node) {
    char *raw_text;
    char number_text[64];
    long value;

    if (node == NULL) {
        return duplicate_text("");
    }

    if (starts_with(node->label, "id(")) {
        return extract_wrapped_value(node->label, "id(");
    }
    if (starts_with(node->label, "int10(")) {
        return extract_wrapped_value(node->label, "int10(");
    }
    if (starts_with(node->label, "int8(")) {
        raw_text = extract_wrapped_value(node->label, "int8(");
        value = strtol(raw_text, NULL, 8);
        free(raw_text);
        snprintf(number_text, sizeof(number_text), "%ld", value);
        return duplicate_text(number_text);
    }
    if (starts_with(node->label, "int16(")) {
        raw_text = extract_wrapped_value(node->label, "int16(");
        value = strtol(raw_text, NULL, 16);
        free(raw_text);
        snprintf(number_text, sizeof(number_text), "%ld", value);
        return duplicate_text(number_text);
    }
    if (starts_with(node->label, "float(")) {
        return extract_wrapped_value(node->label, "float(");
    }

    return duplicate_text(node->label);
}

static char *generate_expr_code(CodeBuffer *buffer, const Node *node) {
    char *left;
    char *right;
    char *result;

    if (node == NULL) {
        return duplicate_text("");
    }

    if (is_label(node, "E") || is_label(node, "T")) {
        if (node->child_count == 1) {
            return generate_expr_code(buffer, node->children[0]);
        }
        if (node->child_count == 3) {
            left = generate_expr_code(buffer, node->children[0]);
            right = generate_expr_code(buffer, node->children[2]);
            result = new_temp(buffer);
            emit_line(buffer, "%s := %s %s %s", result, left, node->children[1]->label, right);
            free(left);
            free(right);
            return result;
        }
    }

    if (is_label(node, "F")) {
        if (node->child_count == 3 && is_label(node->children[0], "(")) {
            return generate_expr_code(buffer, node->children[1]);
        }
        if (node->child_count == 2 && is_label(node->children[0], "-")) {
            left = generate_expr_code(buffer, node->children[1]);
            result = new_temp(buffer);
            emit_line(buffer, "%s := - %s", result, left);
            free(left);
            return result;
        }
        if (node->child_count == 1) {
            return leaf_value_to_operand(node->children[0]);
        }
    }

    return leaf_value_to_operand(node);
}

static void generate_cond_code(CodeBuffer *buffer, const Node *node,
                               const char *true_label, const char *false_label) {
    char *left;
    char *right;
    char *middle_label;

    if (node == NULL) {
        return;
    }

    if (is_label(node, "C")) {
        if (node->child_count == 3 && is_label(node->children[1], "or")) {
            middle_label = new_label(buffer);
            generate_cond_code(buffer, node->children[0], true_label, middle_label);
            emit_label(buffer, middle_label);
            generate_cond_code(buffer, node->children[2], true_label, false_label);
            free(middle_label);
            return;
        }
        if (node->child_count == 1) {
            generate_cond_code(buffer, node->children[0], true_label, false_label);
            return;
        }
    }

    if (is_label(node, "B")) {
        if (node->child_count == 3 && is_label(node->children[1], "and")) {
            middle_label = new_label(buffer);
            generate_cond_code(buffer, node->children[0], middle_label, false_label);
            emit_label(buffer, middle_label);
            generate_cond_code(buffer, node->children[2], true_label, false_label);
            free(middle_label);
            return;
        }
        if (node->child_count == 1) {
            generate_cond_code(buffer, node->children[0], true_label, false_label);
            return;
        }
    }

    if (is_label(node, "N")) {
        if (node->child_count == 2 && is_label(node->children[0], "not")) {
            generate_cond_code(buffer, node->children[1], false_label, true_label);
            return;
        }
        if (node->child_count == 3 && is_label(node->children[0], "(")) {
            generate_cond_code(buffer, node->children[1], true_label, false_label);
            return;
        }
        if (node->child_count == 1) {
            generate_cond_code(buffer, node->children[0], true_label, false_label);
            return;
        }
    }

    if (is_label(node, "R") && node->child_count == 3) {
        left = generate_expr_code(buffer, node->children[0]);
        right = generate_expr_code(buffer, node->children[2]);
        emit_line(buffer, "if %s %s %s goto %s", left, node->children[1]->label, right, true_label);
        emit_line(buffer, "goto %s", false_label);
        free(left);
        free(right);
    }
}

static void generate_stmt_code(CodeBuffer *buffer, const Node *node,
                               const char *entry_label) {
    char *left_name;
    char *right_name;
    char *true_label;
    char *false_label;
    char *next_label;
    char *begin_label;

    if (node == NULL || !is_label(node, "S")) {
        return;
    }

    if (entry_label != NULL) {
        emit_label(buffer, entry_label);
    }

    if (node->child_count == 3 && starts_with(node->children[0]->label, "id(")) {
        left_name = leaf_value_to_operand(node->children[0]);
        right_name = generate_expr_code(buffer, node->children[2]);
        emit_line(buffer, "%s := %s", left_name, right_name);
        free(left_name);
        free(right_name);
        return;
    }

    if (node->child_count == 4 && is_label(node->children[0], "if")) {
        true_label = new_label(buffer);
        false_label = new_label(buffer);
        generate_cond_code(buffer, node->children[1], true_label, false_label);
        generate_stmt_code(buffer, node->children[3], true_label);
        emit_label(buffer, false_label);
        free(true_label);
        free(false_label);
        return;
    }

    if (node->child_count == 6 && is_label(node->children[0], "if")) {
        true_label = new_label(buffer);
        false_label = new_label(buffer);
        next_label = new_label(buffer);
        generate_cond_code(buffer, node->children[1], true_label, false_label);
        generate_stmt_code(buffer, node->children[3], true_label);
        emit_line(buffer, "goto %s", next_label);
        generate_stmt_code(buffer, node->children[5], false_label);
        emit_label(buffer, next_label);
        free(true_label);
        free(false_label);
        free(next_label);
        return;
    }

    if (node->child_count == 4 && is_label(node->children[0], "while")) {
        begin_label = entry_label != NULL ? duplicate_text(entry_label) : new_label(buffer);
        true_label = new_label(buffer);
        false_label = new_label(buffer);

        if (entry_label == NULL) {
            emit_label(buffer, begin_label);
        }
        generate_cond_code(buffer, node->children[1], true_label, false_label);
        generate_stmt_code(buffer, node->children[3], true_label);
        emit_line(buffer, "goto %s", begin_label);
        emit_label(buffer, false_label);
        free(begin_label);
        free(true_label);
        free(false_label);
        return;
    }

    if (node->child_count == 3 && is_label(node->children[0], "begin")) {
        generate_program_code(buffer, node->children[1]);
        return;
    }
}

static void generate_line_code(CodeBuffer *buffer, const Node *node) {
    if (node == NULL || !is_label(node, "L")) {
        return;
    }

    if (node->child_count >= 1 && is_label(node->children[0], "S")) {
        generate_stmt_code(buffer, node->children[0], NULL);
    }
}

static void generate_program_code(CodeBuffer *buffer, const Node *node) {
    if (node == NULL || !is_label(node, "P")) {
        return;
    }

    if (node->child_count == 1) {
        generate_line_code(buffer, node->children[0]);
        return;
    }

    if (node->child_count == 2) {
        generate_line_code(buffer, node->children[0]);
        generate_program_code(buffer, node->children[1]);
    }
}

void Exp3_PrintThreeAddressCode(const Node *root) {
    int index;
    CodeBuffer buffer;

    code_buffer_init(&buffer);
    generate_program_code(&buffer, root);

    printf("Three-address code:\n");
    for (index = 0; index < buffer.count; index++) {
        printf("%s\n", buffer.lines[index]);
    }

    code_buffer_free(&buffer);
}
