#ifndef EXP2_PARSER_API_H
#define EXP2_PARSER_API_H

#include "Exp2_SyntaxTree.h"

/* 调用实验二语法分析器分析指定文件。
 * 返回 0 表示语法分析成功，返回 1 表示失败。 */
int Exp2_ParseFile(const char *input_path);

/* 返回最近一次语法分析生成的语法树根节点。
 * 如果语法分析失败，则通常返回 NULL。 */
Node *Exp2_GetSyntaxRoot(void);

/* 按实验二原有格式打印语法树。 */
void Exp2_PrintSyntaxTree(void);

/* 按实验二原有格式打印归约序列。 */
void Exp2_PrintReductions(void);

/* 释放最近一次语法分析生成的语法树。 */
void Exp2_FreeSyntaxTree(void);

#endif
