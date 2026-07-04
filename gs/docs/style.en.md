# GS Writing Guide

Chinese version: [style.md](style.md)

This guide is the practical companion to `gs/SPEC.md`.
It explains how to write GS scripts that are easy to read, stable inside GPM
packages, and consistent across install and automation flows.

## Preferred style

- Use one command per line.
- Prefer 4-letter uppercase command names for runtime commands.
- Use long uppercase names only for host directives such as `INSTALLROOT`.
- Use commas to separate arguments.
- Quote values when they contain spaces, commas, or comment-like prefixes.
- Keep package scripts relative to the script directory whenever possible.

## Core forms

```gs
SETV NAME = World
LOGS INFO, Hello %@NAME%
```

```gs
if %@ARCH% == x64:
  LOGS INFO, x64 build
elif %@ARCH% == arm64:
  LOGS INFO, arm64 build
else:
  LOGS INFO, other build
```

```gs
FUNC setup
  LOGS INFO, preparing

CALL setup
```

## Package scripts

A package script usually follows this order:

- `PREI` / `PREINST` for preparation.
- `INST` / `INSTALLING` for the main install work.
- `POST` / `POSTINST` for cleanup and final registration.

Use phase blocks for package lifecycle actions and regular helper functions for
shared logic.

## Naming and layout

- Prefer descriptive variable names for values that survive across several steps.
- Keep small helper logic in `FUNC` blocks instead of repeating statements.
- Use `LOGS` for short status messages that help packaging or debugging.
- Avoid extra indentation levels unless they make control flow clearer.

## Relationship to the spec

When style and behavior conflict, `gs/SPEC.md` wins.
This guide is intentionally practical, not normative.
