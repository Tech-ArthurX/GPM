# GS 脚本语言 — 架构总览

> 更新日期：2026-06-12
> 模块：`github.com/SECTL/GPM/gs`

---

## 什么是 gs？

`gs`（Glim Script / Gpm Script）是一个 **轻量级 Windows 脚本语言**，专为 GPM 包管理器设计。语法受 PECMD 启发（4 字母命令名），但大幅扩展了现代能力。

### 设计目标

| 目标 | 说明 |
| --- | --- |
| **轻量** | 单文件脚本，无外部依赖 |
| **Windows 原生** | 直接调用 Win32 API、注册表、服务、VHD |
| **安全可控** | 每个危险操作都有 `HostCaps` 沙箱开关 |
| **可编译** | 可编译为独立 `.exe`（C 后端 ~110KB，LCL 后端 ~2.3MB） |
| **可嵌入** | Go 包，可在任何 Go 程序中调用 |

### 两种执行模式

```
┌─────────────────────────────────────────────┐
│          gs 脚本 (.gs)                       │
├─────────────────┬───────────────────────────┤
│  解释器模式      │  编译器模式                │
│  (Interpreter)   │  (Compiler → .exe)        │
├─────────────────┼───────────────────────────┤
│ gs.RunFile()    │ gs.ParseString() →        │
│ gs.RunPhases()  │ ir.Generator.Compile()    │
│                 │ → clang → .exe            │
├─────────────────┼───────────────────────────┤
│ GPM 内部使用     │ 开发者发布独立工具         │
│ 安装/卸载脚本    │ tiny Windows utility      │
└─────────────────┴───────────────────────────┘
```

---

## 项目结构

```
gs/
├── lexer.go          # 词法分析器
├── parser.go         # 语法分析器 (含 CGSB/PGCB/AGCB 块预处理)
├── interp.go         # 解释器核心 (控制流 + 命令路由 + 变量系统)
├── commands.go       # 解释器所有命令实现 (~1500 行)
├── run.go            # 执行入口 (RunFile / RunSource / RunFilePhases)
├── gs_test.go        # 集成测试
│
├── ir/               # 编译器 (Intermediate Representation)
│   ├── types.go      # 类型系统 (float/string/bool/handle/proc)
│   ├── ir.go         # C 代码生成器主逻辑 (含 MSVC 路径探测)
│   ├── uidef.go      # UI XML → C 代码生成 (UIDF/UILP)
│   ├── lcl_backend.go    # LCL (Pascal) 后端
│   ├── pas_lcl_backend.go # Pascal LCL 后端具体实现
│   └── pgc_agc.go    # Pascal/汇编块编译
│
├── cmd/gs/main.go    # 编译器 CLI 入口
│
├── docs/             # 本文档所在目录
├── examples/         # 示例文件
└── SPEC.md           # 语言规范
```

---

## 两种模式对比

| 维度 | 解释器 | 编译器 |
| --- | --- | --- |
| 执行方式 | Go 运行时执行 | 编译为独立 exe |
| 启动速度 | 毫秒级 | - |
| 依赖 | Go runtime (嵌入 GPM) | 无 (独立 exe) |
| 文件大小 | - | ~110KB-2.3MB |
| 支持哪些命令 | **所有** (含 FILE/REGI/SERV/TASK/ZIPX 等) | **子集** (SETV/CALC/LOGS/MBOX/EXEC/APIC/DLL 系列/UI) |
| UI | 无 (通过散脚本桥接) | UIDF + UILP (XML 定义 UI) |
| Win32 API | 无直接调用 | APIC/WAPI 直接调用 |
| 动态 DLL | 无 | DLLO/DLSY/DLCA/DLCL |
| C 内联 | 无 | CGSI/CGSH/CGSB/CGSC |
| 使用场景 | GPM 包安装/卸载脚本 | 独立 Windows 工具发布 |

---

## 安全沙箱 (HostCaps)

每个危险操作都受 `HostCaps` 控制，由调用方（GPM）决定启用哪些：

```go
type HostCaps struct {
    AllowExec      bool  // EXEC, RUNS, PECM, WNSH
    AllowRegistry  bool  // REGI SET/DEL
    AllowService   bool  // SERV
    AllowFirewall  bool  // FWAL
    AllowScheduled bool  // TASK
    AllowVHD       bool  // VHDM/VHDU/VHDC
    AllowLink      bool  // LINK
    AllowHTTP      bool  // HTTP/DOWN/UPLD
    AllowGPM       bool  // GPMI/GPMU
    TempResDir     string // 内置工具路径 (pecmd.exe 等)
    PecmdPath      string
    WinXShellPath  string
}
```

GPM 在 `gs_runner.go` 中为安装/卸载脚本启用所有能力：
```go
caps := gs.HostCaps{
    AllowExec: true, AllowRegistry: true, AllowService: true,
    AllowFirewall: true, AllowScheduled: true, AllowVHD: true,
    AllowLink: true, AllowHTTP: true, AllowGPM: true,
    TempResDir: tempResDir,
}
```

---

## 生命周期（GPM 集成）

```
gpm install package.gpm
│
├─ 解压 .gpm → Scripts/PackageName/*.gs
│
├─ 执行 gs 安装阶段:
│   gs.RunFilePhases(script, ["PREINST", "INSTALLING", "POSTINST"])
│
│   _PREI  → Pre-install (备份、停止服务等)
│   _INST  → Installing (散脚本桥接 RUNS)
│   _POST  → Post-install (注册、启动服务等)
│
├─ 散脚本回退 (无 .gs 时):
│   按顺序执行 .bat → .cmd → .ini → .exe → .reg → .lua
│
└─ gpm uninstall package
   │
   └─ 执行 gs 卸载阶段:
       gs.RunFilePhases(script, ["PREUNINST", "UNINSTALLING", "POSTUNINST"])
```

---

## 编译流程

```
.gs 文件
│
├─ preprocessBlocks()  → 处理 CGSB..CGSE / PGCB..PGCE / AGCB..AGCE 块
│
├─ Tokenize()          → 词法分析
│   ├─ 4字母大写命令 → TokCommand
│   ├─ 标识符        → TokIdent
│   ├─ 字符串 "..."   → TokString (含 \n \t \" \\)
│   ├─ 逗号          → TokComma
│   ├─ 等号          → TokEqual
│   └─ 换行          → TokNewline
│
├─ Parse()             → 语法分析
│   ├─ FUNC + 缩进块 → 子程序
│   ├─ _PREI/_INST 等 → 阶段块
│   └─ 其他           → Main 顶层
│
├─ ir.Generator
│   ├─ generate()     → C 代码生成
│   ├─ SetBackend()   → "c" (默认) / "lcl" (Pascal)
│   └─ Compile()      → clang.exe → .exe
│       ├─ .exe.c     → 生成的 C 源码 (可审查)
│       ├─ .exe.ll    → LLVM IR (可审查)
│       └─ .exe       → 最终编译产物
```
