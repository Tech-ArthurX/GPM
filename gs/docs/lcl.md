# LCL UI Backend

Current production path: gs C backend calls a small Pascal/LCL runtime DLL.

```text
gs -> C -> clang -> app.exe
                 -> app.exe loads gs_lcl_runtime.dll
```

Final distribution only needs:

```text
app.exe
gs_lcl_runtime.dll
```

No `fpc.exe`, no Lazarus source tree, no Go LCL runtime is required on the target machine.

## Runtime DLL

Source:

```text
runtime/lcl/gs_lcl_runtime.lpr
```

Built with:

```text
D:\lazarus\fpc\3.2.2\bin\x86_64-win64\fpc.exe
```

Exports:

- `gslcl_init`
- `gslcl_form_new`
- `gslcl_control`
- `gslcl_show`
- `gslcl_run`
- `gslcl_free`

## gs Usage

```gs
UIDF WIN = widget_test.ui
UILP WIN
```

The compiler emits C calls to `gs_lcl_load()`, `gs_lcl_form_new()`, `gs_lcl_control()`, and `gs_lcl_run()`.

## Generated Artifacts

Compiling `examples/widget_test.gs` creates:

```text
examples/widget_test.exe
examples/gs_lcl_runtime.dll
examples/widget_test.exe.c
examples/widget_test.exe.ll
```

## Why not link LCL .obj directly?

FPC/LCL has Pascal RTL initialization, unit initialization order, resources, and widgetset plumbing. Manually linking scattered `.o/.obj` files into a C/clang exe is fragile. The small runtime DLL keeps that initialization owned by FPC and gives gs a stable C ABI.