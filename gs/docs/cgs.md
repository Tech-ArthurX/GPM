# CGS: C interop for gs

CGS is the gs equivalent of a small cgo-style escape hatch. It lets a gs script add C includes, link libraries, file-scope C helpers, and main-body C statements.

## Commands

```gs
CGSI <math.h>                              ; add #include <math.h>
CGSL user32.lib                            ; add #pragma comment(lib, "user32.lib")
CGSH static double sq(double x){return x*x;} ; add C helper before main
CGSC double y = sq(12);                    ; add C statement inside main
```

## Example

```gs
CGSI <math.h>
CGSH static double gs_square(double x) { return x * x; }
SETV X = 12
CGSC double Y = gs_square(X);
CGSC if (Y < 0) return 9;
LOGS INFO, cgs square computed
```

## Notes

- CGS is intentionally sharp; use it for system integration and runtime helpers.
- Normal gs users should prefer typed gs commands.
- Quoted strings in `CGSC` are limited by current gs parser tokenization; prefer C helpers in `CGSH` for complex code.
## CGSB / CGSE

Use CGSB/CGSE for multiline C code blocks, similar in spirit to cgo preambles.
