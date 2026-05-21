#ifndef EXP2_SYNTAX_TREE_H
#define EXP2_SYNTAX_TREE_H

/* Node 表示实验二语法分析生成的一棵语法树结点。
 * label：当前结点显示的名字，例如 P、S、E、id(a)
 * children：孩子结点数组
 * child_count：孩子个数 */
typedef struct Node {
    char *label;
    struct Node **children;
    int child_count;
} Node;

#endif
