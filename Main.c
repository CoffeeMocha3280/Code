#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "Exp1_LexerAPI.h"
#include "Exp2_ParserAPI.h"
#include "Exp3_CodeGen.h"

#define DEFAULT_INPUT_FILE "input.txt"
#define INPUT_FOLDER "input_txt"
#define MANUAL_END_MARKER "@end"
#define MAX_TXT_FILES 128
#define MAX_PATH_LENGTH 260

/* 打印统一可执行程序的使用方法。
 * 统一程序只有这一个 main，它负责按固定顺序执行实验一、实验二、实验三。 */
static void print_usage(const char *program_name) {
    printf("用法：\n");
    printf("  %s [输入文件]\n", program_name);
    printf("\n");
    printf("如果不传入输入文件，程序会进入交互模式。\n");
    printf("交互模式下只需要选择输入来源，随后会自动依次执行实验一、实验二、实验三。\n");
}

/* 去掉字符串末尾的换行符，方便后续比较用户输入。 */
static void trim_newline(char *text) {
    size_t length = strlen(text);
    while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r')) {
        text[length - 1] = '\0';
        length--;
    }
}

/* 在交互模式结束前暂停一下，避免窗口一闪而过。
 * 这样双击 exe 或者用某些 IDE 直接运行时，也能看到程序输出。 */
static void wait_before_exit(void) {
    char line[8];
    printf("\n按回车键退出程序...");
    fgets(line, sizeof(line), stdin);
}

/* 扫描指定模式匹配到的全部 txt 文件，并把相对路径保存到数组里。
 * 例如可以扫描 "*.txt"，也可以扫描 "input_txt\\*.txt"。 */
static int collect_txt_files_with_pattern(const char *pattern,
                                          const char *prefix,
                                          char files[][MAX_PATH_LENGTH],
                                          int start_count,
                                          int max_files) {
    WIN32_FIND_DATAA find_data;
    HANDLE handle;
    int count = start_count;

    handle = FindFirstFileA(pattern, &find_data);
    if (handle == INVALID_HANDLE_VALUE) {
        return count;
    }

    do {
        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            if (count < max_files) {
                if (prefix != NULL && prefix[0] != '\0') {
                    snprintf(files[count], MAX_PATH_LENGTH, "%s/%s", prefix, find_data.cFileName);
                }
                else {
                    snprintf(files[count], MAX_PATH_LENGTH, "%s", find_data.cFileName);
                }
                count++;
            }
        }
    } while (FindNextFileA(handle, &find_data));

    FindClose(handle);
    return count;
}

/* 汇总两处输入文件：
 * 1. 当前目录下的 txt
 * 2. input_txt 子目录下的 txt */
static int collect_all_txt_files(char files[][MAX_PATH_LENGTH], int max_files) {
    int count = 0;

    count = collect_txt_files_with_pattern("*.txt", "", files, count, max_files);
    count = collect_txt_files_with_pattern(INPUT_FOLDER "\\*.txt",
                                           INPUT_FOLDER,
                                           files,
                                           count,
                                           max_files);
    return count;
}

/* 显示当前目录里的 txt 文件，并让用户选择其中一个文件，
 * 或者选择手动输入源程序。 */
static int choose_input_path(char *selected_path, size_t path_size) {
    char txt_files[MAX_TXT_FILES][MAX_PATH_LENGTH];
    char choice_text[32];
    int txt_count;
    int choice;

    txt_count = collect_all_txt_files(txt_files, MAX_TXT_FILES);

    printf("\n统一程序将按顺序执行：实验一 -> 实验二 -> 实验三\n");
    printf("检测到的 txt 文件如下：\n");
    printf("  当前目录: *.txt\n");
    printf("  输入子目录: " INPUT_FOLDER "/*.txt\n");

    if (txt_count == 0) {
        printf("  (未检测到 txt 文件)\n");
    }
    else {
        int index;
        for (index = 0; index < txt_count; index++) {
            printf("  %d. %s\n", index + 1, txt_files[index]);
        }
    }

    printf("  %d. 手动输入源程序\n", txt_count + 1);
    printf("请输入要使用的编号：");

    if (fgets(choice_text, sizeof(choice_text), stdin) == NULL) {
        return 0;
    }
    trim_newline(choice_text);

    choice = atoi(choice_text);
    if (choice >= 1 && choice <= txt_count) {
        snprintf(selected_path, path_size, "%s", txt_files[choice - 1]);
        return 1;
    }
    if (choice == txt_count + 1) {
        snprintf(selected_path, path_size, "%s", DEFAULT_INPUT_FILE);
        return 2;
    }
    return 0;
}

/* 把用户手动输入的源程序写入 input.txt。
 * 单独输入一行 @end 表示输入结束。 */
static int save_manual_input_to_file(const char *output_path) {
    char line[1024];
    FILE *output = fopen(output_path, "w");

    if (output == NULL) {
        fprintf(stderr, "无法打开输出文件：%s\n", output_path);
        return 1;
    }

    printf("\n请输入源程序内容。\n");
    printf("单独输入一行 %s 表示结束输入。\n\n", MANUAL_END_MARKER);

    while (1) {
        if (fgets(line, sizeof(line), stdin) == NULL) {
            break;
        }

        trim_newline(line);
        if (strcmp(line, MANUAL_END_MARKER) == 0) {
            break;
        }

        fprintf(output, "%s\n", line);
    }

    fclose(output);
    return 0;
}

/* 按固定顺序依次执行实验一、实验二、实验三。 */
static int run_all_experiments(const char *input_path) {
    Node *syntax_root;

    printf("\n将使用输入文件：%s\n\n", input_path);

    printf("===== 实验一：词法分析 =====\n");
    if (Exp1_RunLexerFromFile(input_path) != 0) {
        return 1;
    }

    printf("\n===== 实验二：语法分析 =====\n");
    if (Exp2_ParseFile(input_path) != 0) {
        return 1;
    }

    printf("语法分析成功。\n\n");
    Exp2_PrintSyntaxTree();
    printf("\n");
    Exp2_PrintReductions();
    printf("\n");

    printf("===== 实验三：三地址码生成 =====\n");
    syntax_root = Exp2_GetSyntaxRoot();
    Exp3_PrintThreeAddressCode(syntax_root);

    Exp2_FreeSyntaxTree();
    return 0;
}

int main(int argc, char *argv[]) {
    char selected_path[MAX_PATH_LENGTH];
    const char *input_path = selected_path;
    int input_source;
    int result;

    /* 如果显式传入输入文件，就直接执行三个实验。 */
    if (argc >= 2) {
        input_path = argv[1];
        return run_all_experiments(input_path);
    }

    printf("===== 编译原理实验统一程序 =====\n");

    /* 不传参数时，进入 txt 文件/手动输入选择。 */
    input_source = choose_input_path(selected_path, sizeof(selected_path));
    if (input_source == 0) {
        print_usage(argv[0]);
        wait_before_exit();
        return 1;
    }

    if (input_source == 2) {
        if (save_manual_input_to_file(DEFAULT_INPUT_FILE) != 0) {
            wait_before_exit();
            return 1;
        }
    }

    result = run_all_experiments(input_path);
    wait_before_exit();
    return result;
}
