# gs 编译型语言化方案分析

> 日期：2026-06-10
> 维护者：Sisyphus

## 当前实现

gs 现在有两个后端：

1. `c` 后端：gs -> C -> Clang -> Windows exe，适合小体积工具。
2. `lcl` 后端：gs -> Go LCL 项目 -> Go exe，LCL 作为 UI 层，适合更好看的桌面界面。

## 使用

```powershell
cd gs
go build -o gs.exe ./cmd/gs

# C/Win32 后端
.\gs.exe examples\types.gs -o examples\types.exe

# LCL UI 后端
.\gs.exe -backend lcl examples\ui_xml_lcl.gs -o examples\lcl_ui_xml_lcl.exe
```

## 用户类型

- `string`
- `float`：所有数字
- `bool`

句柄、窗口、DLL、函数指针都是内部不透明值，用户不直接操作指针。

## 命令状态

| 命令 | C 后端 | LCL 后端 | 说明 |
|---|---|---|---|
| SETV/STRV/FLOT/BOOL | ✅ | ✅ | 类型变量 |
| CALC | ✅ | ✅ | 基础表达式 |
| LOGS | ✅ | ✅ | 输出日志 |
| EXEC | ✅ | ✅ | 外部命令 |
| EROR | ✅ | ✅ | 运行期报错并退出 |
| BEEP | ✅ | ✅ | Beep 封装 |
| MBOX | ✅ | ✅ | MessageBox 封装 |
| APIC/WAPI | ✅ | ✅ | 隐式 WinAPI 调用 |
| DLOP/DLSY/DLCA/DLCL | ✅ | ✅ | 显式动态 DLL 调用 |
| UIDF/UILP | ✅ | ✅ | XML UI / LCL UI |

## LCL 作为 gs UI 层

LCL 现在作为 gs 的 UI runtime DLL。普通 gs 逻辑仍由 C 后端编译，UI 相关 UIDF/UILP 调用 gs_lcl_runtime.dll。

产物：

```text
out.exe
out.exe.lcl/main.go
out.exe.lcl/go.mod
```

注意：LCL 运行时需要 `libenergy` DLL。

## 示例

- `examples/types.gs`
- `examples/static_api.gs`
- `examples/dynamic_dll.gs`
- `examples/error_demo.gs`
- `examples/ui_xml_win32.gs`
- `examples/ui_xml_lcl.gs`
- `examples/widget_test.gs` + `examples/widget_test.ui`

## 验证

批量编译结果：9 个示例全部通过；自动验证不运行 UI exe。

## 下一步

1. LCL 事件绑定：`.ui` 支持 `onclick="subName"`。
2. LCL 控件数据绑定：从 edit/list/check 读写变量。
3. FILE/FDIR/LINK 编译支持。
4. 继续研究纯 LLVM IR 后端缩小 exe。
## CGS C Interop

See docs/cgs.md. Commands: CGSI, CGSL, CGSH, CGSC. This is the cgo-like escape hatch for gs.
