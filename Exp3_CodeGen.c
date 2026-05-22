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

/* 判断一行是否是标签定义，例如 L3: */
static int parse_label_line(const char *line, char *label, size_t size);

/* 判断一行是否是无条件跳转，例如 goto L3 */
static int parse_plain_goto_line(const char *line, char *label, size_t size);

/* 解析一行中的跳转目标，支持 goto 和 if ... goto 两种形式。 */
static int parse_any_goto_target(const char *line, char *label, size_t size);

/* 在标签映射表中查找某个旧标签最终应该对应到哪个新标签。 */
static const char *resolve_label_alias(const char *label,
                                       char old_labels[][32],
                                       char new_labels[][32],
                                       int alias_count);

/* 在一对一重命名表里直接查找新标签，不做链式追踪。 */
static const char *lookup_direct_label(const char *label,
                                       char old_labels[][32],
                                       char new_labels[][32],
                                       int label_count);

/* 把一行三地址码中的跳转目标替换成优化后的标签名。 */
static char *rewrite_line_target(const char *line,
                                 char old_labels[][32],
                                 char new_labels[][32],
                                 int alias_count);

/* 把一行三地址码中的跳转目标按最终重编号结果直接替换。 */
static char *rewrite_line_target_direct(const char *line,
                                        char old_labels[][32],
                                        char new_labels[][32],
                                        int label_count);

/* 对已经生成的三地址码做简单标签优化。 */
static void optimize_labels(CodeBuffer *buffer);

/* 判断某个标签位置前面是否可能有顺序落入它的执行流。 */
static int label_has_fallthrough_entry(CodeBuffer *buffer, int label_index);

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

static int parse_label_line(const char *line, char *label, size_t size) {
    size_t length;

    if (line == NULL) {
        return 0;
    }

    length = strlen(line);
    if (length < 2 || line[length - 1] != ':') {
        return 0;
    }

    if (length >= size) {
        return 0;
    }

    memcpy(label, line, length - 1);
    label[length - 1] = '\0';
    return 1;
}

static int parse_plain_goto_line(const char *line, char *label, size_t size) {
    if (line == NULL || strncmp(line, "goto ", 5) != 0) {
        return 0;
    }

    snprintf(label, size, "%s", line + 5);
    return 1;
}

static int parse_any_goto_target(const char *line, char *label, size_t size) {
    const char *goto_pos;

    if (parse_plain_goto_line(line, label, size)) {
        return 1;
    }

    goto_pos = strstr(line, " goto ");
    if (goto_pos == NULL) {
        return 0;
    }

    snprintf(label, size, "%s", goto_pos + 6);
    return 1;
}

static const char *resolve_label_alias(const char *label,
                                       char old_labels[][32],
                                       char new_labels[][32],
                                       int alias_count) {
    const char *current;
    int step;

    current = label;
    for (step = 0; step < alias_count; step++) {
        int index;
        int changed = 0;

        for (index = 0; index < alias_count; index++) {
            if (strcmp(old_labels[index], current) == 0) {
                current = new_labels[index];
                changed = 1;
                break;
            }
        }

        if (!changed) {
            break;
        }
    }

    return current;
}

static const char *lookup_direct_label(const char *label,
                                       char old_labels[][32],
                                       char new_labels[][32],
                                       int label_count) {
    int index;

    for (index = 0; index < label_count; index++) {
        if (strcmp(old_labels[index], label) == 0) {
            return new_labels[index];
        }
    }

    return label;
}

static char *rewrite_line_target(const char *line,
                                 char old_labels[][32],
                                 char new_labels[][32],
                                 int alias_count) {
    char target[32];
    const char *resolved;
    const char *goto_pos;
    char rebuilt[512];

    if (!parse_any_goto_target(line, target, sizeof(target))) {
        return duplicate_text(line);
    }

    resolved = resolve_label_alias(target, old_labels, new_labels, alias_count);

    if (strncmp(line, "goto ", 5) == 0) {
        snprintf(rebuilt, sizeof(rebuilt), "goto %s", resolved);
        return duplicate_text(rebuilt);
    }

    goto_pos = strstr(line, " goto ");
    if (goto_pos == NULL) {
        return duplicate_text(line);
    }

    snprintf(rebuilt, sizeof(rebuilt), "%.*s goto %s",
             (int)(goto_pos - line), line, resolved);
    return duplicate_text(rebuilt);
}

static char *rewrite_line_target_direct(const char *line,
                                        char old_labels[][32],
                                        char new_labels[][32],
                                        int label_count) {
    char target[32];
    const char *resolved;
    const char *goto_pos;
    char rebuilt[512];

    if (!parse_any_goto_target(line, target, sizeof(target))) {
        return duplicate_text(line);
    }

    resolved = lookup_direct_label(target, old_labels, new_labels, label_count);

    if (strncmp(line, "goto ", 5) == 0) {
        snprintf(rebuilt, sizeof(rebuilt), "goto %s", resolved);
        return duplicate_text(rebuilt);
    }

    goto_pos = strstr(line, " goto ");
    if (goto_pos == NULL) {
        return duplicate_text(line);
    }

    snprintf(rebuilt, sizeof(rebuilt), "%.*s goto %s",
             (int)(goto_pos - line), line, resolved);
    return duplicate_text(rebuilt);
}

static int label_has_fallthrough_entry(CodeBuffer *buffer, int label_index) {
    int index;
    char ignored_label[32];
    char ignored_target[32];

    index = label_index - 1;
    while (index >= 0) {
        if (!parse_label_line(buffer->lines[index], ignored_label, sizeof(ignored_label))) {
            break;
        }
        index--;
    }

    /* 程序开头直接就是标签时，不存在从上一条语句自然落入的问题。 */
    if (index < 0) {
        return 0;
    }

    /* 如果前一条真正可执行语句本身就是无条件跳转，也不会自然落入当前标签。 */
    if (parse_plain_goto_line(buffer->lines[index], ignored_target, sizeof(ignored_target))) {
        return 0;
    }

    return 1;
}

static void optimize_labels(CodeBuffer *buffer) {
    char (*alias_old)[32];
    char (*alias_new)[32];
    char (*rename_old)[32];
    char (*rename_new)[32];
    char (*used_labels)[32];
    char **new_lines;
    int alias_count;
    int new_count;
    int rename_count;
    int used_count;
    int index;

    if (buffer->count == 0) {
        return;
    }

    alias_old = (char (*)[32])malloc(sizeof(char[32]) * buffer->count);
    alias_new = (char (*)[32])malloc(sizeof(char[32]) * buffer->count);
    rename_old = (char (*)[32])malloc(sizeof(char[32]) * buffer->count);
    rename_new = (char (*)[32])malloc(sizeof(char[32]) * buffer->count);
    used_labels = (char (*)[32])malloc(sizeof(char[32]) * buffer->count);
    new_lines = (char **)malloc(sizeof(char *) * buffer->count);
    if (alias_old == NULL || alias_new == NULL || rename_old == NULL ||
        rename_new == NULL || used_labels == NULL || new_lines == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    /* 第一步：把连续出现的空标签合并到后一个真实落点标签上。 */
    alias_count = 0;
    for (index = 0; index < buffer->count - 1; index++) {
        char current_label[32];
        char next_label[32];

        if (parse_label_line(buffer->lines[index], current_label, sizeof(current_label)) &&
            parse_label_line(buffer->lines[index + 1], next_label, sizeof(next_label))) {
            snprintf(alias_old[alias_count], sizeof(alias_old[alias_count]), "%s", current_label);
            snprintf(alias_new[alias_count], sizeof(alias_new[alias_count]), "%s", next_label);
            alias_count++;
        }
    }

    /* 第二步：删除被合并掉的标签定义，并同步改写所有跳转目标。 */
    new_count = 0;
    for (index = 0; index < buffer->count; index++) {
        char label_name[32];

        if (parse_label_line(buffer->lines[index], label_name, sizeof(label_name))) {
            const char *resolved = resolve_label_alias(label_name, alias_old, alias_new, alias_count);
            if (strcmp(label_name, resolved) != 0) {
                continue;
            }
            new_lines[new_count++] = duplicate_text(buffer->lines[index]);
        }
        else {
            new_lines[new_count++] = rewrite_line_target(buffer->lines[index],
                                                         alias_old,
                                                         alias_new,
                                                         alias_count);
        }
    }

    for (index = 0; index < buffer->count; index++) {
        free(buffer->lines[index]);
    }
    free(buffer->lines);
    buffer->lines = new_lines;
    buffer->count = new_count;
    buffer->capacity = new_count;

    /* 第三步：删除“goto 后面立刻就是目标标签”的无意义跳转。 */
    new_lines = (char **)malloc(sizeof(char *) * buffer->count);
    if (new_lines == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    new_count = 0;
    for (index = 0; index < buffer->count; index++) {
        char goto_target[32];
        char next_label[32];

        if (index + 1 < buffer->count &&
            parse_plain_goto_line(buffer->lines[index], goto_target, sizeof(goto_target)) &&
            parse_label_line(buffer->lines[index + 1], next_label, sizeof(next_label)) &&
            strcmp(goto_target, next_label) == 0) {
            free(buffer->lines[index]);
            continue;
        }

        new_lines[new_count++] = buffer->lines[index];
    }

    free(buffer->lines);
    buffer->lines = new_lines;
    buffer->count = new_count;
    buffer->capacity = new_count;

    /* 第四步：消除“标签后面只有一条 goto”的跳板标签。
     * 只有当这个标签不会被上一条语句顺序落入时，才可以安全删除。 */
    alias_count = 0;
    for (index = 0; index < buffer->count - 1; index++) {
        char label_name[32];
        char goto_target[32];

        if (parse_label_line(buffer->lines[index], label_name, sizeof(label_name)) &&
            parse_plain_goto_line(buffer->lines[index + 1], goto_target, sizeof(goto_target)) &&
            !label_has_fallthrough_entry(buffer, index)) {
            snprintf(alias_old[alias_count], sizeof(alias_old[alias_count]), "%s", label_name);
            snprintf(alias_new[alias_count], sizeof(alias_new[alias_count]), "%s", goto_target);
            alias_count++;
        }
    }

    new_lines = (char **)malloc(sizeof(char *) * buffer->count);
    if (new_lines == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    new_count = 0;
    for (index = 0; index < buffer->count; index++) {
        char label_name[32];
        char goto_target[32];

        if (parse_label_line(buffer->lines[index], label_name, sizeof(label_name))) {
            const char *resolved = resolve_label_alias(label_name, alias_old, alias_new, alias_count);
            if (strcmp(label_name, resolved) != 0) {
                free(buffer->lines[index]);
                continue;
            }
        }

        if (index > 0 &&
            parse_plain_goto_line(buffer->lines[index], goto_target, sizeof(goto_target)) &&
            parse_label_line(buffer->lines[index - 1], label_name, sizeof(label_name))) {
            const char *resolved = resolve_label_alias(label_name, alias_old, alias_new, alias_count);
            if (strcmp(label_name, resolved) != 0) {
                free(buffer->lines[index]);
                continue;
            }
        }

        new_lines[new_count++] = rewrite_line_target(buffer->lines[index],
                                                     alias_old,
                                                     alias_new,
                                                     alias_count);
        free(buffer->lines[index]);
    }

    free(buffer->lines);
    buffer->lines = new_lines;
    buffer->count = new_count;
    buffer->capacity = new_count;

    /* 第五步：删除没有任何跳转会用到的标签。 */
    used_count = 0;
    for (index = 0; index < buffer->count; index++) {
        char target[32];
        int exists;
        int used_index;

        if (!parse_any_goto_target(buffer->lines[index], target, sizeof(target))) {
            continue;
        }

        exists = 0;
        for (used_index = 0; used_index < used_count; used_index++) {
            if (strcmp(used_labels[used_index], target) == 0) {
                exists = 1;
                break;
            }
        }

        if (!exists) {
            snprintf(used_labels[used_count], sizeof(used_labels[used_count]), "%s", target);
            used_count++;
        }
    }

    new_lines = (char **)malloc(sizeof(char *) * buffer->count);
    if (new_lines == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    new_count = 0;
    for (index = 0; index < buffer->count; index++) {
        char label_name[32];
        int exists;
        int used_index;

        if (!parse_label_line(buffer->lines[index], label_name, sizeof(label_name))) {
            new_lines[new_count++] = buffer->lines[index];
            continue;
        }

        exists = 0;
        for (used_index = 0; used_index < used_count; used_index++) {
            if (strcmp(used_labels[used_index], label_name) == 0) {
                exists = 1;
                break;
            }
        }

        if (exists) {
            new_lines[new_count++] = buffer->lines[index];
        }
        else {
            free(buffer->lines[index]);
        }
    }

    free(buffer->lines);
    buffer->lines = new_lines;
    buffer->count = new_count;
    buffer->capacity = new_count;

    /* 第六步：重新连续编号标签，避免删除冗余标签后出现编号断裂。 */
    rename_count = 0;
    for (index = 0; index < buffer->count; index++) {
        char label_name[32];
        char new_label_name[32];

        if (parse_label_line(buffer->lines[index], label_name, sizeof(label_name))) {
            snprintf(rename_old[rename_count], sizeof(rename_old[rename_count]), "%s", label_name);
            snprintf(new_label_name, sizeof(new_label_name), "L%d", rename_count);
            snprintf(rename_new[rename_count], sizeof(rename_new[rename_count]), "%s", new_label_name);
            rename_count++;
        }
    }

    for (index = 0; index < buffer->count; index++) {
        char label_name[32];
        char rewritten[64];
        char *updated_line;

        if (parse_label_line(buffer->lines[index], label_name, sizeof(label_name))) {
            const char *resolved = lookup_direct_label(label_name, rename_old, rename_new, rename_count);
            snprintf(rewritten, sizeof(rewritten), "%s:", resolved);
            free(buffer->lines[index]);
            buffer->lines[index] = duplicate_text(rewritten);
        }
        else {
            updated_line = rewrite_line_target_direct(buffer->lines[index],
                                                      rename_old,
                                                      rename_new,
                                                      rename_count);
            free(buffer->lines[index]);
            buffer->lines[index] = updated_line;
        }
    }

    free(alias_old);
    free(alias_new);
    free(rename_old);
    free(rename_new);
    free(used_labels);
}

void Exp3_PrintThreeAddressCode(const Node *root) {
    int index;
    CodeBuffer buffer;

    code_buffer_init(&buffer);
    generate_program_code(&buffer, root);
    optimize_labels(&buffer);

    printf("Three-address code:\n");
    for (index = 0; index < buffer.count; index++) {
        printf("%s\n", buffer.lines[index]);
    }

    code_buffer_free(&buffer);
}
