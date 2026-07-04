# gs Compiler Completion Plan

## Goal

Make the gs compiler usable as a small Windows-focused compiled language with concise APIs, examples, and per-feature documentation.

## Scope

1. Keep user-visible types simple: `string`, `float`, `bool`.
2. Hide pointers and handles behind compiler-internal values.
3. Provide both implicit WinAPI calls and explicit dynamic DLL calls.
4. Provide declarative `.ui` XML loading for raw Win32 and LCL backend.
5. Add a clear runtime error command and compile-time diagnostics.
6. Keep generated artifacts inspectable (`.c`, `.ll`, `.exe`, LCL Go project).

## Deliverables

- `docs/types.md`
- `docs/compiler.md`
- `docs/winapi.md`
- `docs/dll.md`
- `docs/ui-xml.md`
- `docs/lcl.md`
- `docs/errors.md`
- `examples/types.gs`
- `examples/static_api.gs`
- `examples/dynamic_dll.gs`
- `examples/ui_demo.gs` + `examples/main.ui`
- `examples/widget_test.gs` + `examples/widget_test.ui`
- `examples/error_demo.gs`

## Verification

Run:

```powershell
go test -v .
go build -o gs.exe ./cmd/gs
.\gs.exe examples\types.gs -o examples\types.exe
.\gs.exe examples\static_api.gs -o examples\static_api.exe
.\gs.exe examples\dynamic_dll.gs -o examples\dynamic_dll.exe
.\gs.exe examples\error_demo.gs -o examples\error_demo.exe
.\gs.exe examples\ui_demo.gs -o examples\ui_demo.exe
.\gs.exe -backend lcl examples\widget_test.gs -o examples\lcl_widget.exe
```

Do not run UI executables during automated verification.