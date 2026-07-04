# Errors

Compile-time diagnostics:

```text
line 2: compiler does not support command "NOPE"
```

Runtime error command:

```gs
EROR boom from gs, gs demo, 7
```

C backend: shows Win32 error message box and returns code.
LCL backend: uses WinAPI message box through syscall and exits with code.