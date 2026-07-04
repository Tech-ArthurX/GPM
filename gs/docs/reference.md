# GS 语言完整参考手册

> **注意**：本文覆盖解释器全部已实现命令。编译器只支持其中子集（见 [编译器支持](#编译器支持的命令)）。

---

## 基础语法

### 行结构

```
CMD  ARG1, ARG2, KEY=VALUE, ...
```

- **一行一条语句**：普通 GS 语句不支持通用续行。
- **命令名**：运行时命令推荐 4 个大写字母（如 `SETV` / `FILE` / `LOGS`），宿主指令或明确扩展可使用长命令名（如 `INSTALLROOT`）。
- **参数分隔符**：逗号 `,`。逗号两侧空白会被忽略。
- **多词参数**：逗号前的多个裸词会合并为一个参数。例如 `EXEC notepad foo.txt` 会作为一个命令行参数传给 `EXEC`。
- **KEY=VALUE**：这是命令约定，不是独立赋值语法。多数 `KEY=VALUE` 命令只读取第一个参数中的等号。
- **原始代码块**：`CGSB/CGSE`、`PGCB/PGCE`、`AGCB/AGCE` 是例外，块内多行会被预处理成一个参数。

### 注释

```gs
; 分号注释
# 井号注释
// 双斜杠注释
LOGS INFO, done // 行尾注释
```

注释规则：

- `;`、`#`、`//` 在字符串外作为下一个 token 出现时开始注释。
- `http://example.com` 这种 URL 可以直接裸写，因为 `//` 不在 token 开头。
- 如果参数本身需要以 `//` 开头，必须写成字符串：`"//server/share"`。

### 字符串

```gs
"双引号字符串"      ; 支持转义
bareword            ; 裸词
```

转义序列：

| 序列 | 含义 |
| --- | --- |
| `\n` | 换行 |
| `\t` | 制表符 |
| `\"` | 双引号 |
| `\\` | 反斜杠 |

裸词规则：

- 裸词遇到空白、逗号、等号、换行、回车或双引号结束。
- 如果参数里需要空格、逗号或以 `//` 开头，建议使用双引号。
- 不认识的转义会原样保留为反斜杠加字符。

### 变量引用

```gs
%ENV_VAR%     ; 进程环境变量
%@GS_VAR%     ; gs 局部变量（解释器）/ gs 编译器变量
```

- 解释器执行语句前会展开参数中的变量。
- `%NAME%` 读取当前进程环境变量。
- `%@NAME%` 读取 GS 局部变量。
- `ENVI` 会修改当前进程环境，并影响后续命令和子进程。
- 未解析变量在普通参数展开中会变成空字符串。

### 路径解析

解释器命令里涉及文件路径时通常遵循：

- 空路径表示当前脚本所在目录。
- 绝对路径直接清理后使用。
- 相对路径基于 `.gs` 脚本所在目录解析。

GPM 安装器、包解压器等宿主层会额外做路径安全检查，例如阻止 ZipSlip 路径穿越。

### 空白行 & 空命令

空白行 / 只有注释的行被跳过。空命令体不报错。

---

## 控制流

### 简洁条件语法（推荐）

缩进式 `if / elif / else`，无需手写 `_END`：

```gs
if %@X% == 1
  FILE WRITE,out.txt,one
elif %@X% == 2
  FILE WRITE,out.txt,two
else
  FILE WRITE,out.txt,other
```

预处理器会把它转换成内部控制流块；脚本侧只需要写缩进语法。

### `FUNC name` — 子程序

```gs
FUNC greet
  LOGS INFO, Hello %@NAME%

CALL greet
CALL greet          ; 可多次调用
```

- `FUNC` 使用缩进块，不需要结束标记
- 子程序可出现在脚本任意位置
- 暂不支持参数传递（但可通过局部变量共享）

### 阶段块

别名（大小写不敏感）：

| 规范名 | 可用别名 |
| --- | --- |
| `_PREINST` | `_PREI`, `_PRE`, `_PREINSTALL`, `_PRE-INSTALL` |
| `_INSTALLING` | `_INST`, `_DURING`, `_DOING` |
| `_POSTINST` | `_POST`, `_POSTINSTALL`, `_POST-INSTALL` |
| `_PREUNINST` | `_PREU`, `_PREUNINSTALL`, `_PRE-UNINSTALL` |
| `_UNINSTALLING` | `_UNINST`, `_UNIN` |
| `_POSTUNINST` | `_POSTU`, `_POSTUNINSTALL`, `_POST-UNINSTALL` |

```gs
_PREI
  LOGS INFO, pre-install
_END

_INST
  RUNS Scripts/install
_END
```

通过 `RunFilePhases(path, phases, caps, logger)` 驱动。

### `EXIT [code]` — 退出

```gs
EXIT         ; 退出码 0
EXIT 7       ; 退出码 7
```

### `WAIT ms` — 等待

```gs
WAIT 1000    ; 等 1 秒
WAIT 500     ; 等 500ms
```

### `CALL name` — 调用子程序

```gs
FUNC myFunc
  LOGS INFO, in myFunc

CALL myFunc
```

---

## 变量与字符串处理

### `SETV KEY=VALUE` — 设置局部变量

```gs
SETV X = 42
SETV Y = hello
SETV Z = "quoted string"
SETV A = %COMSPEC%   ; 可展开环境变量
```

### `ENVI KEY=VALUE` — 设置进程环境变量

```gs
ENVI MY_VAR = hello
ENVI PATH = %PATH%;C:\tools    ; 可拼接
```

### `CALC KEY=expr` — 计算

当前实现是存根（`cmdCalc` 只存储值，不做实际计算）。主要用作：

```gs
CALC X = 42
LOGS INFO, X = %@X%
```

**TODO**：需要实现 `+ - * /` 运算。编译器端 `genCalc()` 已支持 C 表达式传递。

### `STRL KEY=STRING` — 字符串长度

```gs
STRL LEN = hello     ; @LEN = "5"
STRL LEN = ""         ; @LEN = "0"
```

### `LPOS KEY=STR,SUB` / `RPOS KEY=STR,SUB` — 查找位置

```gs
LPOS POS = hello,el    ; POS = "1" (从 0 开始)
RPOS POS = hello,l     ; POS = "3" (最后一个 l)

; 未找到返回 -1
LPOS POS = hello,x     ; POS = "-1"
```

### `LSTR KEY=S,N` / `RSTR KEY=S,N` — 取左右子串

```gs
LSTR OUT = hello,2     ; OUT = "he"
RSTR OUT = hello,2     ; OUT = "lo"
; 如果 N ≥ 字符串长度，返回整个字符串
LSTR OUT = hi,100      ; OUT = "hi"
```

### `MSTR KEY=S,FROM,LEN` — 取中间子串

```gs
MSTR OUT = hello,1,3   ; OUT = "ell"
MSTR OUT = hello,0,100 ; OUT = "hello" (自动截断)
MSTR OUT = hello,99,3  ; OUT = "" (起始越界)
```

### `RGEX KEY=STR,PATTERN[,GROUP]` — 正则匹配

```gs
RGEX OUT = hello123,([a-z]+)(\d+),2  ; OUT = "123"
RGEX OUT = abc,,1                     ; OUT = "" (未匹配)
```

- 使用 Go `regexp` 包（RE2 语法）
- `GROUP` 默认 = 0（全文匹配）

### `RGSB KEY,STR,PATTERN,REPL` — 正则替换

```gs
RGSB OUT, hello world, (hello) (world), $2 $1  ; OUT = "world hello"
```

---

## 文件 / 目录 / 路径

### `FILE OP,PATH[,ARGS]` — 文件操作

#### `FILE COPY SRC,DST`
```gs
FILE COPY, C:\a.txt, C:\b.txt
FILE COPY, file.txt, backup\file.txt    ; 相对路径
```

#### `FILE MOVE SRC,DST`
```gs
FILE MOVE, C:\a.txt, C:\b.txt
```

#### `FILE DEL PATH`
```gs
FILE DEL, C:\temp\old.log
```

#### `FILE READ PATH[,VAR]`
```gs
FILE READ, config.json, CONTENT          ; 读到 @CONTENT
FILE READ, data.txt                      ; 直接写到 stdout

; 配合 JSON 使用
FILE READ, pkg.json, RAW
JSON VALUE = @%RAW%, $.version
```

#### `FILE WRITE PATH,CONTENT`
```gs
FILE WRITE, output.txt, Hello World
FILE WRITE, log.txt, %@X%                ; 写变量
```

#### `FILE APPEND PATH,CONTENT`
```gs
FILE APPEND, log.txt, new line
```

### `FDIR OP[,PATH]` — 目录操作

#### `FDIR MAKE PATH`
```gs
FDIR MAKE, C:\my\dir
FDIR MAKE, subdir                       ; 相对路径
```

#### `FDIR DEL PATH`
```gs
FDIR DEL, C:\temp\old
```

#### `FDIR LIST PATH,KEY`
```gs
FDIR LIST, C:\Windows, FILES            ; @FILES = 换行分隔的文件名列表
```

### `LINK TYPE,SRC,DST` — 链接（需 AllowLink）

```gs
LINK SYM, C:\real, C:\link              ; 符号链接
LINK HARD, C:\real.txt, C:\hard.txt     ; 硬链接
LINK JUNC, C:\real, C:\junc             ; 目录交接点
```

编译器同样支持 `SYM` / `HARD` / `JUNC`：`HARD` 使用 `CreateHardLinkA`，`SYM` 使用 `CreateSymbolicLinkA`，`JUNC` 走 `mklink /J`。

### `FEXT KEY=PATH` — 取扩展名

```gs
FEXT EXT = C:\test.txt                  ; @EXT = ".txt"
FEXT EXT = C:\test                      ; @EXT = ""
```

### `FDRV KEY=PATH` — 取驱动器号

```gs
FDRV DRV = C:\Windows\System32          ; @DRV = "C:"
```

### `EXIST KEY=PATH` — 检测存在

```gs
EXIST OK = C:\Windows\explorer.exe      ; @OK = "1"
EXIST OK = C:\nonexistent               ; @OK = "0"
```

---

## 外部执行

### `EXEC command` — 运行命令

```gs
EXEC notepad.exe
EXEC reg add "HKLM\SOFTWARE\MyApp" /v Version /t REG_SZ /d 1.0 /f
EXECHK msiexec /i installer.msi /quiet
```

- 通过 `cmd.exe /c` 执行
- 工作目录 = 脚本所在目录
- `%@EXITCODE%` 会（待实现）记录返回码
- 需要 `AllowExec` 权限

### `RUNS [DIR]` — 散脚本桥接

按顺序运行指定目录下的所有脚本：

```gs
RUNS Scripts\install          ; 运行目录下所有散脚本
RUNS                          ; 默认当前目录
```

执行顺序：`.bat` → `.cmd` → `.ini` → `.exe` → `.reg` → `.lua`

- `.bat/.cmd` → `cmd.exe /c chcp 65001`
- `.exe` → 优先 pecmd.exe EXEC，否则直接运行
- `.ini` → pecmd.exe LOAD
- `.reg` → regedit.exe /s
- `.lua` → winxshell.exe -script
- 需要 `AllowExec` 权限

### `PECM LOAD|EXEC,PATH` — 用 pecmd 运行

```gs
PECM EXEC, myinstaller.exe /silent
PECM LOAD, config.ini
```

### `WNSH LUA_SCRIPT` — 用 WinXShell 运行

```gs
WNSH script.lua
```

### `DRVI OP,...` — 用 Drvinstall 安装/管理驱动

`DRVI` 是 `Drvinstall.exe` 的 GS 桥接命令，适合 PE 环境或离线系统维护脚本。相对路径会按当前 `.gs` 脚本目录解析，命令需要宿主开启 `AllowExec`。

```gs
DRVI B, D:\Drivers, 密码, wificonfig.ini
DRVI T, D:\Drivers                 ; 安装但忽略显卡驱动
DRVI Y                             ; 自动扫描离线系统驱动并安装
DRVI H                             ; 自动扫描并忽略显卡驱动
DRVI IMPORT, D:\driver, E:\Windows
DRVI REMOVE, oem1.inf, E:\Windows
DRVI MIGRATE, E:\Windows, net;display
DRVI BACKUP, D:\driver, E:\Windows, net
DRVI RAW, -b, D:\Drivers, -p:密码, -config:wificonfig.ini
```

映射关系：

| GS 写法 | Drvinstall 参数 |
| --- | --- |
| `DRVI B,源[,密码[,配置]]` | `-b 源 [-p:密码] [-config:配置] -Progress` |
| `DRVI T,源[,密码[,配置]]` | `-t 源 [-p:密码] [-config:配置] -Progress` |
| `DRVI Y` | `-y -Progress` |
| `DRVI H` | `-h -Progress` |
| `DRVI IMPORT,源[,系统]` | `-import 源 [系统]` |
| `DRVI REMOVE,oem.inf[,系统]` | `-remove oem.inf [系统]` |
| `DRVI MIGRATE,系统[,filter]` | `-migrate 系统 [/filter:...]` |
| `DRVI BACKUP,目录[,系统[,filter]]` | `-backup 目录 [系统] [/filter:...]` |
| `DRVI RAW,args...` | 原样兼容传参，仍会解析路径型参数 |

`filter` 只允许 `net;display;audio;bluetooth;system;disk` 这些类别。

---

## VHD 虚拟磁盘

全部需要 `AllowVHD` 权限。底层通过 `diskpart.exe /s` 驱动。

### `VHDM PATH[,RO]` — 挂载

```gs
VHDM C:\my.vhdx              ; 读写挂载
VHDM C:\my.vhd, RO           ; 只读挂载
```

### `VHDU PATH` — 卸载

```gs
VHDU C:\my.vhdx
```

### `VHDC PATH,MB,FIXED|DYNAMIC` — 创建

```gs
VHDC C:\new.vhdx, 2048, DYNAMIC
VHDC C:\new.vhd, 10240, FIXED
```

---

## JSON 操作

### `JSON KEY=SOURCE,PATH` — 读取 JSON 值

```gs
; 从文件读取
JSON VER = package.json, $.version
JSON NAME = data.json, $.author.name

; 从内联文本（以 @ 开头）
JSON VAL = @{"x":{"y":42}}, $.x.y      ; @VAL = "42"
```

路径语法类似 JSONPath 子集：
- `$.key` — 对象键
- `$.key.subkey` — 嵌套
- `$.arr[0]` — 数组索引（当前仅 `JSON` 和 `JSNL` 支持）

### `JSNS FILE,PATH=VALUE` — 设置 JSON 值

```gs
JSNS config.json, $.debug = true
JSNS config.json, $.name = MyApp
JSNS config.json, $.version = 1.0
JSNS config.json, $.features.enableLogging = true    ; 自动创建中间对象
```

解释器会直接读写 JSON 文件。编译器也支持 `JSNS`，生成的 exe 会在运行时通过内置 PowerShell JSON helper 修改目标文件，支持字符串、数字、`true`/`false`/`null` 和自动创建中间对象。

### `JSNL KEY=SOURCE,PATH` — 取 JSON 数组长度

```gs
JSNL N = data.json, $.items          ; @N = 数组元素个数
JSNL N = @{"a":[1,2,3]}, $.a         ; @N = "3"
```

---

## XML 操作

### `XMLR KEY=FILE,XPATH` — 读取 XML

```gs
XMLR VAL = config.xml, /root/name
XMLR VAL = config.xml, root.setting.version    ; 简写路径
```

路径：`/` 或 `.` 分隔的标签名，不使用完整 XPath 语法。

### `XMLW FILE,XPATH=VALUE` — 写入 XML

```gs
XMLW config.xml, /root/name = NewValue
```

仅替换**已有**元素的文本内容（不支持新增节点），使用正则回退。

---

## HTTP 网络

全部需要 `AllowHTTP` 权限。

### `HTTP METHOD,URL[,BODY]` — 通用 HTTP 请求

```gs
HTTP GET, http://example.com/api
LOGS INFO, code=%@HTTP_CODE%
LOGS INFO, body=%@HTTP_BODY%

HTTP POST, http://example.com/api, {"key":"value"}
HTTP PUT, http://example.com/api/1, {"name":"test"}
HTTP DELETE, http://example.com/api/1
```

结果保存在变量中：
- `%@HTTP_CODE%` — HTTP 状态码
- `%@HTTP_BODY%` — 响应体

### `DOWN URL,FILE` — 下载文件

```gs
DOWN https://example.com/file.zip, C:\downloads\file.zip
DOWN http://osbox.top/d/CloudService/plugins/test.gpm, test.gpm
```

### `UPLD FILE,URL` — 上传文件（multipart）

```gs
UPLD report.log, http://example.com/upload
```

编译器网络后端使用 libcurl C runtime：`runtime/net/gs_net_runtime.c` 负责编译成对象文件，链接时连同 MSYS2 UCRT64/MINGW64 的静态 libcurl 依赖嵌入最终 exe。生成的 exe 不需要额外携带网络 DLL。

---

## 编码与加密

### `HASH KEY=ALGO,FILE_OR_TEXT` — 哈希

```gs
; 文件 hash
HASH H = SHA256, C:\Windows\explorer.exe

; 文本 hash（源以 @ 开头）
HASH H = MD5, @hello world
HASH H = SHA1, @data to hash

; 算法名（大小写不敏感）
; MD5, SHA1, SHA256, SHA512
```

### `BASE KEY=ENC|DEC,TEXT` — Base64

```gs
BASE OUT = ENC, hello world            ; @OUT = "aGVsbG8gd29ybGQ="
BASE OUT = DEC, aGVsbG8=               ; @OUT = "hello"
```

### `HEXC KEY=ENC|DEC,TEXT` — 十六进制

```gs
HEXC OUT = ENC, hello                  ; @OUT = "68656c6c6f"
HEXC OUT = DEC, 68656c6c6f             ; @OUT = "hello"
```

### `AESC KEY=ENC|DEC,KEY,IV,TEXT` — AES-256-CBC 加密

```gs
; 加密
AESC OUT = ENC, hex:001122..., hex:010203..., hello  ; @OUT = base64 密文

; 解密
AESC OUT = DEC, hex:001122..., hex:010203..., %@OUT%

; KEY/IV 格式:
;   hex:...  → 十六进制
;   b64:...  → Base64
;   裸字符串 → 直接作为字节（或检测为 hex）
```

---

## 压缩归档

### `ZIPX ARCHIVE,DEST_DIR` — 解压 ZIP

```gs
ZIPX package.zip, C:\output
```

自动防范 ZipSlip 路径穿越。

### `ZIPC ARCHIVE,SRC_DIR` — 创建 ZIP

```gs
ZIPC output.zip, C:\myfolder
```

递归添加目录中所有文件（不包含空目录）。

### `TARX ARCHIVE,DEST_DIR` — 解压 TAR/GZ

```gs
TARX archive.tar.gz, C:\output
TARX archive.tgz, C:\output
```

自动检测 `.gz`/`.tgz` 后缀解压缩。

---

## Windows 系统管理

### `REGI OP,PATH[,NAME,VALUE,TYPE]` — 注册表

需要 `AllowRegistry`。

```gs
; 读取
REGI GET, HKLM\SOFTWARE\MyApp, Version, VER
LOGS INFO, Version = %@VER%

; 写入
REGI SET, HKCU\Software\MyApp, Name, MyApp, SZ
REGI SET, HKLM\SOFTWARE\MyApp, Count, 100, DWORD
REGI SET, HKLM\SOFTWARE\MyApp, Big, 999999999999, QWORD

; 删除键值
REGI DEL, HKCU\Software\MyApp, Name

; 删除整个键
REGI DEL, HKCU\Software\MyApp
```

支持的注册表根键：`HKLM` / `HKCU` / `HKCR` / `HKU` / `HKCC`

支持的类型：`SZ` (string) / `DWORD` (uint32) / `QWORD` (uint64)

编译器支持 `GET` / `SET` / `DEL`，生成的 exe 直接调用 Win32 Registry API。

### `SERV OP,NAME` — Windows 服务

需要 `AllowService`。

```gs
SERV START, MyService
SERV STOP, MyService
SERV RESTART, MyService
SERV STATUS, MyService          ; 输出到 stdout
```

底层使用 `sc.exe`。编译器支持 `START` / `STOP` / `RESTART` / `STATUS`。

### `TASK OP,NAME[,TRIGGER,COMMAND]` — 计划任务

需要 `AllowScheduled`。

```gs
TASK RUN, MyTask
TASK DEL, MyTask
TASK QUERY, MyTask
TASK CREATE, MyTask, DAILY, C:\script.bat
```

底层使用 `schtasks.exe`。编译器支持 `RUN` / `DEL` / `QUERY` / `STATUS` / `CREATE`。

### `FWAL OP,RULE_ARGS` — 防火墙规则

需要 `AllowFirewall`。

```gs
FWAL ADD, name=MyApp dir=in action=allow program=C:\app.exe
FWAL DEL, name=MyApp
```

底层使用 `netsh advfirewall firewall`。编译器支持 `ADD` / `DEL`。

---

## GPM 集成

### `INSTALLROOT DIR` — 声明包默认安装根

```gs
INSTALLROOT C:\GPMApps
INSTALLROOT "%ProgramFiles%\GPMApps"
```

`INSTALLROOT` 是 GPM 安装器提前读取的包级指令，用来指定 core 文件的默认安装根目录。实际安装目录为：

```text
<INSTALLROOT>\<PackageName>
```

要求：

- 必须写在包内顶层 `Scripts/*.gs` 或 `Scripts/gs` 脚本中，安装器会在解压前预扫描这些文件
- 路径必须是绝对路径，可使用 `%ENV%` 环境变量展开
- 本机用户配置优先级更高：`GPM_INSTALL_ROOT` / `HPM_INSTALL_ROOT` / `GPM_HOME` / `gpm.ini [paths] install_root` 会覆盖包内 `INSTALLROOT`
- 脚本运行时 GPM 会注入 `%GPM_INSTALL_ROOT%`、`%GPM_INSTALL_DIR%`、`%GPM_CORE_DIR%`、`%GPM_SCRIPT_DIR%`

### `GPMI NAME[,VERSION]` — 安装 GPM 包

```gs
GPMI Dism++
GPMI MyApp, 2.0
```

**当前实现为存根**（需要 `AllowGPM`）。

### `GPMU NAME` — 卸载 GPM 包

```gs
GPMU Dism++
```

**当前实现为存根**（需要 `AllowGPM`）。

### `GPMV KEY=NAME` — 查询版本

```gs
GPMV VER = MyApp
```

**当前实现为存根**（返回空字符串）。

---

## 日志

### `LOGS LEVEL,MSG` — 输出日志

```gs
LOGS INFO, Hello World
LOGS WARN, Something is odd
LOGS ERROR, Something failed
LOGS DEBUG, X = %@X%

; 不带 level（默认为 INFO）
LOGS Hello World
```

日志通过 `Logger` 接口传递给 GPM 日志系统。在 GPM 中会同时写入日志文件和 stdout。

---

## 编译器支持的命令

编译器（`ir.Generator`）支持的命令子集：

| 分类 | 命令 | 状态 |
| --- | --- | --- |
| 变量 | `SETV`, `STRV`, `FLOT`, `BOOL` | ✅ |
| 计算 | `CALC` | ✅ (C 表达式传递) |
| 日志 | `LOGS` | ✅ |
| 执行 | `EXEC` | ✅（默认等待、`WAIT`、`NOWAIT`/`ASYNC`、`HIDE`、`MIN`、`OPEN`、`RUNAS`） |
| Win32 API | `APIC`, `WAPI` | ✅ |
| DLL 加载 | `DLLO`/`DLOP`, `DLLG`/`DLSY`, `DLLC`/`DLCA`, `DLLF`/`DLCL` | ✅ |
| C 内联 | `CGSI`, `CGSL`, `CGSH`, `CGSB`/`CGSE`, `CGSC` | ✅ |
| Pascal 块 | `PGCB`/`PGCE` | ✅ |
| Asm 块 | `AGCB`/`AGCE` | ✅ |
| UI 定义 | `UIDF`, `UILP` | ✅ |
| 对话框 | `MBOX` | ✅ |
| 蜂鸣 | `BEEP` | ✅ |
| 错误 | `EROR` | ✅ |
| 高频命令 | `EXIT`, `WAIT`, `ENVI`, `STRL`, `LPOS`, `RPOS`, `FEXT`, `FDRV`, `EXIST` | ✅ |
| 文件/目录 | `FILE COPY/MOVE/DEL/READ/WRITE/APPEND`, `FDIR MAKE/DEL/LIST`, `LINK SYM/HARD/JUNC` | ✅ |
| 编码/哈希 | `HASH`, `BASE`, `HEXC` | ✅ |
| JSON | `JSON`, `JSNL`, `JSNS` | ✅ |
| 网络 | `HTTP`, `DOWN`, `UPLD` | ✅（libcurl 静态 runtime 链入 exe） |
| 系统管理 | `REGI GET/SET/DEL`, `SERV START/STOP/RESTART/STATUS`, `TASK RUN/DEL/QUERY/STATUS/CREATE`, `FWAL ADD/DEL` | ✅ |
| 控制流 | `FUNC`, `CALL`, `IFEX`, `WHEN`, `LOOP`, `FORX`, `if/elif/else` | ✅ |

### 编译器特有命令

#### `CGSI <header>` / `CGSI "header"` — C 包含

```gs
CGSI <math.h>
CGSI "my_header.h"
```

#### `CGSL lib` — 链接库

```gs
CGSL user32.lib
CGSL winhttp.lib
```

#### `CGSH code` — C 文件级代码

```gs
CGSH static int add(int a, int b) { return a + b; }
```

#### `CGSB` / `CGSE` — C 多行块

```gs
CGSB
static int mul(int a, int b) {
  return a * b;
}
CGSE
```

#### `CGSC code` — C main 内代码

```gs
CGSC int result = mul(6, 7);
```

#### `APIC [KEY=]FUNC,ARGS...` — 调用 Win32 API

```gs
; 无返回值
APIC MessageBoxA, NULL, text, title, MB_OK

; 有返回值
APIC R = user32.MessageBoxA, 0, text, title, MB_OK

; 可省略 DLL 前缀（自动查找）
APIC R = MessageBoxA, 0, text, title, 0
```

编译器自动处理 `uintptr` 类型转换。

#### `DLLO KEY=DLL` / `DLOP KEY=DLL` — 加载 DLL

```gs
DLLO U = user32.dll
```

#### `DLLG KEY=HANDLE,FUNC_NAME` / `DLSY KEY=HANDLE,FUNC_NAME` — 获取函数地址

```gs
DLLG MB = U, MessageBoxA
```

#### `DLLC KEY=PROC,ARGS...` / `DLCA KEY=PROC,ARGS...` — 调用 DLL 函数

```gs
DLLC R = MB, 0, "hello", "title", 0
```

#### `DLLF HANDLE` / `DLCL HANDLE` — 释放 DLL

```gs
DLLF U
```

#### `MBOX TEXT,TITLE[,KIND]` — 消息框

```gs
MBOX Hello World, MyApp, info
MBOX Fatal Error!, MyApp, error
```

#### `BEEP FREQ,MS` — 蜂鸣

```gs
BEEP 800, 200
```

#### `EROR MESSAGE[,TITLE[,CODE]]` — 错误退出

```gs
EROR Something went wrong, MyApp, 1
```

显示错误消息框并以指定退出码终止。

#### `UIDF NAME=UI_FILE` — 定义 UI 窗口

```gs
UIDF WIN = main.ui
```

#### `UILP NAME` — 启动 UI 消息循环

```gs
UILP WIN
```

#### `PGCB`/`PGCE` — Pascal 代码块

```gs
PGCB
procedure MyFunc; stdcall;
begin
  // pascal code
end;
PGCE
```

#### `AGCB`/`AGCE` — 汇编代码块

```gs
AGCB
section .text
global myAsmFunc
myAsmFunc:
  ret
AGCE
```
