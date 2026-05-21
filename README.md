# 编译原理实验小组工程

这个仓库用于存放当前小组的编译原理实验统一工程，方便组员共同查看代码、同步修改和追踪历史版本。

## 工程目标

当前工程已经整理为一个统一可执行程序，运行后会按顺序完成：

1. 实验一：词法分析
2. 实验二：语法分析并构造语法树
3. 实验三：根据语法树生成三地址码

## 当前主线文件

- `Main.c`
  - 统一程序入口
  - 负责选择输入文件并依次调用三个实验

- `Exp1_Lexer.c`
- `Exp1_LexerAPI.h`
  - 实验一词法分析实现与接口

- `Exp2_Parser.y`
- `Exp2_ParserAPI.h`
- `Exp2_SyntaxTree.h`
  - 实验二语法分析实现、接口和语法树结构定义

- `Exp3_CodeGen.c`
- `Exp3_CodeGen.h`
  - 实验三三地址码生成实现与接口

- `BuildUnified.bat`
  - 一键构建脚本

- `统一可执行程序说明.md`
  - 更详细的工程结构说明文档

## 输入文件

程序启动后会自动扫描以下位置中的 `txt` 文件：

- 当前目录下的 `txt`
- `input_txt` 子目录下的 `txt`

也支持在程序运行时手动输入源程序。

## 构建方法

在当前目录下运行：

```powershell
.\BuildUnified.bat
```

构建成功后会生成：

```powershell
UnifiedCompiler.exe
```

## 运行方法

### 方式一：交互式运行

```powershell
.\UnifiedCompiler.exe
```

然后根据提示选择：

- 当前目录下某个 `txt` 文件
- `input_txt` 子目录下某个 `txt` 文件
- 手动输入源程序

### 方式二：直接指定输入文件

```powershell
.\UnifiedCompiler.exe your_input.txt
```

## 说明

- 本仓库默认不提交 `exe`、`obj`、Bison 自动生成文件等构建产物
- 如果需要重新生成 parser 相关文件，请重新运行 `BuildUnified.bat`
- 更详细的文件作用说明请查看 `统一可执行程序说明.md`

