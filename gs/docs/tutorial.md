# GS 入门教程

---

## 一、什么是 gs？

`gs` 是一个 Windows 脚本语言，可以：

1. **在 GPM 包中作为安装/卸载脚本** — 自动分阶段执行
2. **被编译为独立 exe** — 用作 Windows 小工具

---

## 二、编写第一个脚本

### 2.1 解释器模式

创建一个 `hello.gs`：

```gs
SETV NAME = World
LOGS INFO, Hello %@NAME%!
```

运行（通过 GPM 嵌入或 Go 测试）：

```go
// 在 Go 中运行
gs.RunSource(src, scriptDir, caps, logger)
```

### 2.2 编译器模式

创建一个 `hello2.gs`：

```gs
SETV X = 42
STRV MSG = Hello from compiled gs!
LOGS INFO, X = %X%
LOGS INFO, MSG = %MSG%
MBOX %MSG%, gs demo, info
```

编译：

```powershell
# 先编译编译器
go build -o gs.exe ./cmd/gs

# 编译脚本
.\gs.exe hello2.gs -o hello2.exe
```

输出：
```
hello2.exe        ← 独立可执行文件 (~110KB)
hello2.exe.c      ← 生成的 C 源码（可审查）
hello2.exe.ll     ← LLVM IR（可审查）
gs_lcl_runtime.dll ← LCL UI 运行时（如果用到 UI 才需要）
```

运行 `hello2.exe` → 显示控制台输出 + 消息框。

---

## 三、完整示例：从简单到复杂

### 3.1 变量和类型

```gs
; 文件: examples/types.gs

SETV X = 100          ; 整数 → 推断为 float
STRV NAME = world     ; 显式字符串
BOOL FLAG = true      ; 显式布尔值
FLOT PI = 3.14159     ; 显式浮点数
CALC Z = X + 1        ; 计算

LOGS INFO, X=%X%
LOGS INFO, NAME=%NAME%
LOGS INFO, FLAG=%FLAG%
LOGS INFO, PI=%PI%
LOGS INFO, Z=%Z%
```

编译运行：
```powershell
.\gs.exe examples\types.gs -o types.exe
types.exe
```

输出：
```
X=100
NAME=world
FLAG=true
PI=3.14159
Z=101
```

### 3.2 调用 Win32 API

```gs
; 文件: examples/static_api.gs

SETV X = 123
STRV TITLE = static api demo
LOGS INFO, X=%X%

BEEP 700, 80                                  ; 蜂鸣
APIC MessageBoxA, NULL, Static API works!, %TITLE%, MB_OK
```

### 3.3 加载动态 DLL

```gs
; 文件: examples/dynamic_dll.gs

DLLO U = user32.dll                           ; 加载 DLL
DLLG MB = U, MessageBoxA                      ; 获取函数地址
DLLC R = MB, 0, "Hello from dynamic DLL!", "gs demo", 0   ; 调用
LOGS INFO, Return code = %R%
DLLF U                                        ; 释放
```

### 3.4 UI 窗口

创建一个 `main.ui`：

```xml
<window title="GS UI Demo" width="520" height="320">
  <label text="Name:" x="20" y="20" w="60" h="22" />
  <edit id="name" text="hello" x="90" y="18" w="200" h="24" />
  <button id="ok" text="OK" x="20" y="60" w="100" h="32" />
  <check id="opt" text="Enable feature" x="20" y="100" w="150" h="24" />
</window>
```

`ui_demo.gs`：

```gs
SETV X = 100
STRV NAME = world
BOOL FLAG = false
FLOT PI = 3.14159
CALC Z = X + 1
LOGS INFO, Variables initialized

BEEP 800, 100
UIDF WIN = main.ui
; UILP WIN    ; 取消注释来显示窗口（会阻塞直到窗口关闭）
```

编译：
```powershell
.\gs.exe ui_demo.gs -o ui_demo.exe
```

### 3.5 LCL 后端 UI

```gs
; 文件: examples/widget_test.gs

LOGS INFO, Starting widget test...
UIDF WIN = widget_test.ui
UILP WIN
LOGS INFO, Widget test done.
```

编译（LCL 后端，需 `gs_lcl_runtime.dll`）：

```powershell
.\gs.exe -backend lcl examples\widget_test.gs -o examples\lcl_widget.exe
```

### 3.6 错误处理

```gs
; 文件: examples/error_demo.gs

LOGS INFO, Script starting...
EROR Something went wrong!, gs demo, 7
LOGS INFO, This line is unreachable
```

编译后运行 → 显示错误消息框 → 退出码 7。

### 3.7 C 内联

```gs
; 文件: examples/cgs_demo.gs

CGSI <math.h>
CGSB
static double gs_square(double x) {
  return x * x;
}
CGSE

SETV X = 12
CGSC double Y = gs_square(X);
LOGS INFO, 12 squared is...
```

---

## 四、GPM 包中使用 gs

### 4.1 安装脚本

在 `.gpm` 包的 `Scripts/` 目录下放 `install.gs`：

```gs
_PREI
  LOGS INFO, Pre-install: backing up config
  FILE COPY, %ProgramFiles%\MyApp\config.json, %ProgramFiles%\MyApp\config.json.bak

_INST
  LOGS INFO, Installing: running setup scripts
  RUNS Scripts\install

_POST
  LOGS INFO, Post-install: registering service
  REGI SET, HKLM\SOFTWARE\MyApp, Version, 1.0, SZ
  SERV START, MyService
```

### 4.2 卸载脚本

`uninstall.gs`：

```gs
_PREU
  LOGS INFO, Pre-uninstall: stopping service
  SERV STOP, MyService

_UNIN
  LOGS INFO, Uninstalling...

_POSTU
  LOGS INFO, Post-uninstall: cleaning registry
  REGI DEL, HKLM\SOFTWARE\MyApp
```

### 4.3 混用散脚本

如果不想用 gs，GPM 也支持传统方式：在 `Scripts/` 下放 `.bat` / `.cmd` / `.ini` / `.exe` / `.reg` / `.lua` 文件。

gs 脚本中用 `RUNS` 桥接这些脚本：

```gs
_INST
  ; 先跑散脚本，然后执行 gs 逻辑
  RUNS Scripts\install
  FILE WRITE, %ProgramFiles%\MyApp\.installed, ok
```

---

## 五、常见模式

### 5.1 版本判断

```gs
FILE READ, version.txt, VER
RGEX MAJOR = %@VER%, (\d+)
; %@MAJOR% 就是主版本号
```

### 5.2 配置文件读写

```gs
; 读取 JSON 配置
JSON DEBUG = config.json, $.debug
JSON TIMEOUT = config.json, $.timeout

; 如果 DEBUG 为 true…
```

### 5.3 下载安装

```gs
DOWN https://example.com/tool.exe, C:\tools\tool.exe
EXEC C:\tools\tool.exe /install
REGI SET, HKLM\SOFTWARE\MyApp, Installed, 1, DWORD
```

### 5.4 检查文件是否存在

```gs
EXIST HAS_EXE = C:\Windows\System32\myapp.exe
; %@HAS_EXE% = "1" 或 "0"
if %@HAS_EXE% == 1
  CALL alreadyInstalled
```

`IFEX` / `WHEN` / `LOOP` / `FORX` / `FUNC` / `CALL` 在解释器和 C 编译器中都已接入。

---

## 六、调试技巧

1. **使用 `LOGS DEBUG`**：在 GPM 中运行时，`-d` 标志显示所有日志
2. **编译后检查 `.c` 文件**：编译器会保留生成的 C 源码以便审查
3. **检查 `.ll` 文件**：LLVM IR 文件可以帮助理解编译优化
4. **测试前用 `EROR`**：可以在关键位置加入早期退出方便调试

```gs
LOGS DEBUG, X = %@X%
LOGS INFO, Step 1 complete
EROR Stop here for testing, debug, 0
```
