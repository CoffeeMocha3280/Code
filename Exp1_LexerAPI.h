#ifndef EXP1_LEXER_API_H
#define EXP1_LEXER_API_H

/* 重新初始化实验一词法分析器的内部状态。
 * 当同一个进程里要多次分析不同输入文件时，需要先调用它。 */
void Exp1_ResetLexerState(void);

/* 使用实验一词法分析器读取文件并输出 token 结果。
 * 返回 0 表示词法分析顺利结束，返回 1 表示文件打开失败或出现词法错误。 */
int Exp1_RunLexerFromFile(const char *input_path);

#endif
