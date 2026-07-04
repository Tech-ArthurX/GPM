# GS 实现状态与待办

> 最后更新：2026-06-12

---

## 实现总览

```
解释器:  ████████████████░░░░   ~80%  （全部命令框架 + 大部分实现）
编译器:  ████████░░░░░░░░░░░░   ~45%  （核心基础 + 部分命令）
测试:    ████████░░░░░░░░░░░░   ~40%  （基础测试通过，覆盖不全）
文档:    ████████████████░░░░   ~80%  （所有功能已写文档）
```

---

## 解释器命令实现状态

### ✅ 已实现（共 49 个命令）

| 类别 | 命令数 | 明细 |
| --- | --- | --- |
| 控制流 | 6 | `EXIT`, `WAIT`, `CALL`, `FUNC`, 阶段块 |
| 变量 | 2 | `SETV`, `ENVI` |
| 字符串 | 7 | `STRL`, `LPOS`, `RPOS`, `LSTR`, `RSTR`, `MSTR`, `RGEX`, `RGSB` |
| 计算 | 1 | `CALC`（存根） |
| 文件 | 6 | `FILE COPY/MOVE/DEL/READ/WRITE/APPEND` |
| 目录 | 3 | `FDIR MAKE/DEL/LIST` |
| 路径 | 3 | `FEXT`, `FDRV`, `EXIST` |
| 链接 | 1 | `LINK SYM/HARD/JUNC` |
| 外部执行 | 4 | `EXEC`, `RUNS`, `PECM`, `WNSH` |
| VHD | 3 | `VHDM`, `VHDU`, `VHDC` |
| JSON | 3 | `JSON`, `JSNS`, `JSNL` |
| XML | 2 | `XMLR`, `XMLW` |
| HTTP | 3 | `HTTP`, `DOWN`, `UPLD` |
| 编码 | 4 | `HASH`, `BASE`, `HEXC`, `AESC` |
| 归档 | 3 | `ZIPX`, `ZIPC`, `TARX` |
| 注册表 | 1 | `REGI GET/SET/DEL` |
| 服务 | 1 | `SERV` |
| 计划任务 | 1 | `TASK` |
| 防火墙 | 1 | `FWAL` |
| GPM | 3 | `GPMI`, `GPMU`, `GPMV`（存根） |
| 日志 | 1 | `LOGS` |

### ❌ 解释器中未实现

| 功能 | 预期位置 | 说明 |
| --- | --- | --- |
| `CALC` 运算 | `cmdCalc` | 只存值，不做 `+ - * /` |
| `IFEX` | `runOne` switch | 已解析但无处理 |
| `WHEN` | `runOne` switch | 已解析但无处理 |
| `LOOP` | `runOne` switch | 已解析但无处理 |
| `FORX` | `runOne` switch | 已解析但无处理 |
| `GPMI`/`GPMU`/`GPMV` | `cmdGpmInstall` 等 | 全是空存根 |
| 变量展开中的 `%NAME%` 展开 | `Expand()` | 已实现但 ENVI 不会自动展开 |
| `WAIT` 不支持非整数参数 | `runOne` | 需更好的错误提示 |

---

## 编译器命令实现状态

### ✅ 编译器已实现

| 命令 | 说明 |
| --- | --- |
| `SETV`, `STRV`, `FLOT`, `BOOL` | 变量声明/赋值 |
| `CALC` | C 表达式计算 |
| `FUNC`, `CALL`, `IFEX`, `WHEN`, `LOOP`, `FORX`, `EXIT`, `WAIT` | 子程序、条件、循环、退出、等待 |
| `LOGS` | printf 输出 |
| `EXEC` | 默认等待、`WAIT`、`NOWAIT`/`ASYNC`、`HIDE`、`MIN`、`OPEN`、`RUNAS` |
| `FILE COPY/MOVE/DEL/READ/WRITE/APPEND` | 文件读写和复制移动删除 |
| `FDIR MAKE/DEL/LIST` | 目录创建、删除、枚举 |
| `LINK SYM/HARD/JUNC` | 符号链接、硬链接、目录联接 |
| `FEXT`, `FDRV`, `EXIST` | 路径和存在性检测 |
| `HASH`, `BASE`, `HEXC` | 哈希、Base64、十六进制 |
| `JSON`, `JSNL`, `JSNS` | JSON 读取、数组长度、写入 |
| `HTTP`, `DOWN`, `UPLD` | libcurl 静态 runtime 网络请求、下载、multipart 上传 |
| `REGI GET/SET/DEL` | 注册表读写删除 |
| `SERV START/STOP/RESTART/STATUS` | Windows 服务控制 |
| `TASK RUN/DEL/QUERY/STATUS/CREATE` | 计划任务控制 |
| `FWAL ADD/DEL` | 防火墙规则增删 |
| `APIC`/`WAPI` | Win32 API 调用 |
| `DLLO`/`DLOP`, `DLLG`/`DLSY`, `DLLC`/`DLCA`, `DLLF`/`DLCL` | 动态 DLL 加载 |
| `UIDF`, `UILP` | UI XML 窗口 |
| `MBOX`, `BEEP`, `EROR` | 消息框、蜂鸣、错误退出 |
| `CGSI`, `CGSL`, `CGSH`, `CGSB`/`CGSE`, `CGSC` | C 内联 |
| `PGCB`/`PGCE` | Pascal 代码块 |
| `AGCB`/`AGCE` | 汇编代码块 |

### ❌ 编译器缺失命令

| 命令 | 编译器状态 | 优先级 |
| --- | --- | --- |
| `XMLR`/`XMLW` | ❌ | 中 |
| `AESC` | ❌ | 中 |
| `ZIPX`/`ZIPC`/`TARX` | ❌ | 中 |
| `RUNS`/`PECM`/`WNSH` | ❌ | 中 |
| `VHDM`/`VHDU`/`VHDC` | ❌ | 低 |
| `GPMI`/`GPMU`/`GPMV` | ❌ | 需要先完成 GPM 集成设计 |

---

## 测试覆盖率

### 现有测试

- `gs_test.go`：
  - `TestRunFilePhasesAndRunScripts`：阶段执行 + 散脚本桥接
  - `TestCoreCommands`：JSON/HASH/BASE/HEXC/AESC/FILE 集成测试

- `ir/compiler_more_test.go`：
  - `TestGeneratorHighFrequencyCommands`：验证已支持编译器命令的 C 代码生成

- `examples/compiler_all.gs`：
  - 覆盖当前主线已编译命令
  - `gs.exe -llvm C:\msys64\ucrt64\bin examples\compiler_all.gs -o examples\compiler_all.exe` 已成功生成 exe
  - 生成产物包括 `compiler_all.exe.c`、`compiler_all.exe.ll`、`gs_net_runtime.ll`、`gs_net_runtime.obj`

- `examples/compiler_all_ui.gs`：
  - 在全量样例基础上加入 `UIDF WIN=modern.ui` / `UILP WIN`
  - `gs.exe -llvm C:\msys64\ucrt64\bin examples\compiler_all_ui.gs -o examples\compiler_all_ui.exe` 已成功生成 exe
  - 运行时需要同目录 `gs_lcl_runtime.dll`，编译器会自动复制

- `examples/modern_ui.gs`：
  - 使用 `examples/modern.ui` 验证新版 LCL UI 控件样式
  - `modern.ui` 包含 panel、label、button、edit、check、group、progress、memo
  - LCL runtime 默认深色窗口、Segoe UI 字体和更现代的控件配色

- `examples/llvm_basic.gs` / `examples/compiler_all.gs`：
  - 验证 `-backend llvm` 直接 LLVM IR 后端
  - `gs.exe -backend llvm -llvm C:\msys64\ucrt64\bin examples\compiler_all.gs -o examples\compiler_all_purell.exe` 已成功生成 exe
  - 主程序由 gs AST 直接生成 `.ll`，不经过 C 主程序
  - 核心 runtime 已改为手写 `runtime/llvm/gs_llvm_runtime.ll`；解释器命令全集已具备 LLVM 后端入口，复杂命令会先落到 `.ll` runtime stub，再逐个做实

- `version_test.go`（在 Gpm 根）：
  - `TestCompareVersions`：24 个版本比较测试用例
  - `TestParseVersion`：8 个 `parseVersion` 测试用例

### 测试空缺

| 类别 | 缺什么 |
| --- | --- |
| 词法分析 | 边界情况测试（空字符串、特殊字符、连续逗号等） |
| 语法分析 | 错误恢复、嵌套块、多行 |
| 控制流 | CALL/EXIT/WAIT |
| 文件操作 | COPY/MOVE/DEL 边界、路径穿越防护 |
| 系统命令 | REGI/SERV/TASK/FWAL（需要 mock 或 admin） |
| 网络 | HTTP/DOWN/UPLD（需要 test server） |
| 安全 | 每个 HostCaps 开关的权限测试 |
| 编译器 | 继续补真实 exe 运行断言和外部副作用隔离 |

---

## 待办优先级

### P0 — 当前优先级
1. [ ] 编译器 `ZIPX` / `ZIPC` / `TARX` 归档命令
2. [ ] 编译器 `XMLR` / `XMLW` XML 命令
3. [ ] 编译器 `AESC` AES 加密命令

### P1 — 重要
5. [ ] 编译器 `RUNS` / `PECM` / `WNSH` 散脚本桥接
6. [ ] 编译器 `VHDM` / `VHDU` / `VHDC` 虚拟磁盘命令
7. [ ] `GPMI` / `GPMU` / `GPMV` 集成设计与实现
8. [ ] 增加真实 `.gs -> .exe -> 运行` 端到端用例

### P2 — 文档和测试
9. [ ] 继续同步 `reference.md` / `tutorial.md` / `status.md` 中的语法示例
10. [ ] 为已实现编译器命令补更多边界测试
11. [ ] 为 HostCaps 增加权限开关测试

---

## 已知问题

1. **服务端验证模型**：开源版 registry server 只返回公开 `packages.json`，不做 UA、签名、公钥或密钥验证。
2. **OpenList 扫描脚本**：`scan_packages.py` 可选使用 `GPM_OPENLIST_TOKEN` 刷新/列目录；该 token 不属于 GPM 服务端验证链，也不会写入 `packages.json`。
3. **命名约定**：GS 已拆成独立 `gs/` module，GPM 通过 `replace ../gs` 进行本地联调。
