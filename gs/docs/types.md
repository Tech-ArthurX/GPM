# Types

User-visible gs compiler types:

- `string`: declared by `STRV` or inferred by `SETV` from non-number/non-bool literals.
- `float`: declared by `FLOT` or inferred from all numeric literals. C backend maps to `double`; LCL backend maps to `float64`.
- `bool`: declared by `BOOL` or inferred from `true`/`false`.

Opaque internal values (not directly manipulated by users): DLL handles, function pointers, window/control handles.

Examples:

```gs
SETV X = 100
STRV NAME = world
BOOL FLAG = true
FLOT PI = 3.14159
CALC Z = X + 1
LOGS INFO, Z=%Z%
```