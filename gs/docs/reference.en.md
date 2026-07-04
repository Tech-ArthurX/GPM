# GS Language Reference

Chinese version: [reference.md](reference.md)

This is the short public reference for GS.

- `gs/SPEC.en.md` defines the language contract.
- `tutorial.en.md` shows how to write and run scripts.

Use this page when you want a quick overview of the common command shapes.

## Common forms

```gs
SETV NAME = World
LOGS INFO, Hello %@NAME%
```

```gs
if %@ARCH% == x64:
  LOGS INFO, x64
else:
  LOGS INFO, other
```

```gs
FUNC setup
  LOGS INFO, preparing

CALL setup
```

## Package scripts

Typical package phases:

- `PREI` or `PREINST`
- `INST` or `INSTALLING`
- `POST` or `POSTINST`

`INSTALLROOT` is the package-level directive for default install paths.
