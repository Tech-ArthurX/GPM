# Compiler

Build compiler:

```powershell
go build -o gs.exe ./cmd/gs
```

C/Win32 backend:

```powershell
.\gs.exe examples\types.gs -o examples\types.exe
```

LCL runtime backend (still C backend, but UI via DLL):

```powershell
.\gs.exe examples\widget_test.gs -o examples\widget_test.exe
```

This produces:

- `widget_test.exe`
- `widget_test.exe.c`
- `widget_test.exe.ll`
- `gs_lcl_runtime.dll`
- `gs_lcl_runtime.h`

Generated artifacts are kept beside the exe for inspection.

## CGS

cgo-style interop commands:

```gs
CGSI <math.h>
CGSL user32.lib
CGSH static int foo(void) { return 1; }
CGSB
static double sq(double x) {
  return x * x;
}
CGSE
CGSC double y = sq(12);
```