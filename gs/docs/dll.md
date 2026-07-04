# DLL Loading

Dynamic DLL path:

```gs
DLOP U = user32.dll
DLSY MB = U, MessageBoxA
DLCA R = MB, 0, Dynamic DLL works, dynamic dll, 0
LOGS INFO, result=%R%
DLCL U
```

Aliases:

- `DLLO` / `DLOP`: LoadLibrary
- `DLLG` / `DLSY`: GetProcAddress
- `DLLC` / `DLCA`: call function pointer
- `DLLF` / `DLCL`: free library

Users never handle raw pointers directly.