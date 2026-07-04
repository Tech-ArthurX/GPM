# GS Go API 参考 — 嵌入 gs 解释器

本文档面向希望在 Go 程序中嵌入 gs 脚本引擎的开发者。

---

## 包路径

```go
import "github.com/SECTL/GPM/gs"
```

---

## 核心类型

### `Statement`

```go
type Statement struct {
    Cmd  string   // 4 字母命令名
    Args []string // 参数列表
    Line int      // 源码行号
}
```

### `Program`

```go
type Program struct {
    Main []Statement          // 顶层语句
    Subs map[string][]Statement  // 子程序名 → 语句列表
}
```

### `HostCaps`

```go
type HostCaps struct {
    AllowExec      bool
    AllowRegistry  bool
    AllowService   bool
    AllowFirewall  bool
    AllowScheduled bool
    AllowVHD       bool
    AllowLink      bool
    AllowHTTP      bool
    AllowGPM       bool
    TempResDir     string
    PecmdPath      string
    WinXShellPath  string
}
```

### `Logger`

```go
type Logger interface {
    Log(level, msg string)
}
```

### `Interp`

解释器实例。通过 `NewInterp()` 创建，`Run()`/`RunPhases()` 执行。

---

## 解析 API

### `gs.ParseString(src string) (*Program, error)`

解析 gs 源码为程序结构。

```go
prog, err := gs.ParseString(`
SETV X = 42
LOGS INFO, X=%@X%
`)
```

### `gs.Tokenize(src string) []Token`

纯词法分析（用于调试或自定义处理）。

```go
tokens := gs.Tokenize(`SETV X = 42`)
for _, t := range tokens {
    fmt.Printf("%s:%s\n", t.Type, t.Value)
}
```

---

## 执行 API

### `gs.RunSource(src, scriptDir string, caps HostCaps, logger Logger) error`

解析并执行 gs 源码。

```go
err := gs.RunSource(`
SETV NAME = World
LOGS INFO, Hello %@NAME%!
`, ".", myCaps, myLogger)
```

### `gs.RunFile(path string, caps HostCaps, logger Logger) error`

读取文件并执行。

```go
err := gs.RunFile("install.gs", caps, logger)
```

### `gs.RunFilePhases(path string, phases []string, caps HostCaps, logger Logger) error`

按阶段执行（GPM 集成入口点）。

```go
// GPM 安装流程
gs.RunFilePhases("install.gs", []string{
    "PREINST",
    "INSTALLING",
    "POSTINST",
}, caps, logger)

// GPM 卸载流程
gs.RunFilePhases("uninstall.gs", []string{
    "PREUNINST",
    "UNINSTALLING",
    "POSTUNINST",
}, caps, logger)
```

**阶段处理逻辑**：
1. 先执行顶层语句（init 段）
2. 对每个请求的阶段名，如果脚本中有对应的子程序则执行
3. 不存在的阶段被静默跳过（不报错）

---

## 解释器 API

### `NewInterp(prog *Program, scriptDir string, caps HostCaps, logger Logger) *Interp`

```go
interp := gs.NewInterp(prog, ".", caps, myLogger)
```

### `(*Interp) Run(ctx context.Context) error`

执行程序。如果存在 `MAIN` 子程序，先执行顶层再执行 `MAIN`。

```go
ctx, cancel := context.WithTimeout(context.Background(), 10*time.Minute)
defer cancel()
err := interp.Run(ctx)
```

### `(*Interp) RunPhases(ctx context.Context, phases []string) error`

按阶段执行。

### `(*Interp) SetVar(k, v string)`

在执行前/后设置局部变量。

```go
interp.SetVar("NAME", "World")
```

### `(*Interp) GetVar(k string) (string, bool)`

读取局部变量。

```go
val, ok := interp.GetVar("RESULT")
```

### `(*Interp) Expand(s string) string`

展开字符串中的 `%VAR%` 和 `%@VAR%`。

```go
expanded := interp.Expand("Hello %@NAME%, path=%PATH%")
```

---

## 编译器 API

### `ir.NewGenerator(prog *gs.Program, llvmPath string) *ir.Generator`

```go
gen := ir.NewGenerator(prog, `D:\clang+llvm-22.1.6-x86_64-pc-windows-msvc\bin`)
```

### `(*Generator) SetBackend(backend string)`

```go
gen.SetBackend("c")    // C 后端（默认，~110KB 输出）
gen.SetBackend("lcl")  // LCL (Pascal) 后端（~2.3MB + DLL）
```

### `(*Generator) SetSourceDir(dir string)`

设置 UI XML 文件的查找目录。

```go
gen.SetSourceDir("examples")
```

### `(*Generator) Compile(outputPath string) error`

编译为 exe。

```go
err := gen.Compile("output.exe")
```

---

## CLI 使用

```powershell
# 编译 gs 脚本
.\gs.exe input.gs -o output.exe

# 指定 LLVM/Clang 路径
.\gs.exe -llvm D:\my\clang\bin input.gs -o output.exe

# LCL 后端
.\gs.exe -backend lcl input.gs -o output.exe
```

---

## 完整示例：Go 中嵌入

```go
package main

import (
    "context"
    "fmt"
    "time"
    
    "github.com/SECTL/GPM/gs"
)

type myLogger struct{}

func (myLogger) Log(level, msg string) {
    fmt.Printf("[%s] %s\n", level, msg)
}

func main() {
    script := `
SETV X = 42
STRV NAME = gs embedded
LOGS INFO, Hello %@NAME%! X = %@X%
    `

    caps := gs.HostCaps{
        AllowExec: true,
    }

    err := gs.RunSource(script, ".", caps, myLogger{})
    if err != nil {
        fmt.Printf("Script failed: %v\n", err)
    }
}
```

---

## 日志适配器

GPM 中使用：

```go
type gsLogAdapter struct{}

func (gsLogAdapter) Log(level, msg string) {
    LogDebug("[gs:%s] %s", strings.ToUpper(level), msg)
}
```

---

## 安全注意事项

1. **始终设置 `HostCaps`**：不给不必要的权限
2. **设置 Context 超时**：`Run()` 和 `RunPhases()` 都接受 `context.Context`
3. **脚本来源控制**：gpm 只从本地包或公开索引下载后的 `.gpm` 包中提取脚本；开源版不执行包签名验证
4. **路径隔离**：相对路径始终在 `scriptDir` 内解析
5. **ZipSlip 防护**：`ZIPX` 命令自带路径穿越检查
