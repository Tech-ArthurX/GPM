# GS Tutorial

Chinese version: [tutorial.md](tutorial.md)

GS is a line-oriented script language used by GPM packages and standalone tools.

## First script

```gs
SETV NAME = World
LOGS INFO, Hello %@NAME%
```

## Common flow

```gs
FUNC hello
  LOGS INFO, inside function

CALL hello
```

## Package example

```gs
_PREI
  LOGS INFO, preparing
_END

_INST
  LOGS INFO, installing
_END

_POST
  LOGS INFO, done
_END
```

## Next step

Read `gs/SPEC.en.md` for the full language rules.
