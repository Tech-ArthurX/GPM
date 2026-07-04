# GS Language Specification

Chinese version: [SPEC.md](SPEC.md)

Revision: 2026-06-29

This is the normative contract for GS syntax, parsing, and runtime behavior.
Command examples live in `docs/reference.en.md`, and beginner guidance lives in
`docs/tutorial.en.md`.

## Scope

GS is a line-oriented automation language used by GPM package scripts and by
standalone compiled utilities.
It supports package phases, filesystem work, process execution, registry access,
network I/O, JSON and XML helpers, archive helpers, and compiler-only syntax.

## Source rules

- One physical line is one logical statement, except inside raw compiler blocks.
- UTF-8 is preferred.
- A UTF-8 BOM may be tolerated by the preprocessor.
- There is no generic line continuation syntax.

Preprocessing normalizes these forms before parsing:

- indentation-style `func` blocks into `FUNC ... _END`
- indentation-style `if / elif / else` into `WHEN ... _END`
- raw compiler blocks such as `CGSB/CGSE`, `PGCB/PGCE`, and `AGCB/AGCE`

## Lexical rules

Whitespace outside strings separates tokens.
Newline terminates a statement.

Comments begin with `;`, `#`, or `//` when they appear as the next token outside
of a string.
URLs such as `http://...` and `https://...` are safe as bare values.
A bare argument that must begin with `//` should be quoted.

Only double-quoted strings are supported.
Supported escapes are `\n`, `\t`, `\"`, and `\\`.

Bare words end before comma, equals, whitespace, newline, carriage return, or
quote.
Multiple bare words before the next comma are joined with one space into a
single argument.

## Statement form

```gs
COMMAND arg1, arg2, key=value
```

- Runtime commands should usually use 4 uppercase letters such as `SETV`,
  `FILE`, `HTTP`, and `LOGS`.
- Long uppercase names are reserved for host directives and explicit extensions,
  such as `INSTALLROOT`.
- `_END` terminates block commands.
- Comma separates arguments.
- `KEY=VALUE` is a command convention, not a separate language-level assignment.

Unknown runtime commands should warn and continue.
Unknown compiler commands should fail.

## Variables and expansion

GS expands variables before execution.

```gs
%ENV_NAME%
%@LOCAL_NAME%
```

- `%NAME%` reads process environment variables.
- `%@NAME%` reads GS local variables.
- Unresolved values expand to an empty string in normal argument expansion.
- `SETV`, `CALC`, `ENVI`, and many data commands write local variables.

## Expressions

Boolean expressions are used by `WHEN`, `IFEX`, and indentation-style
conditionals.

Operator precedence from low to high:

- `OR`
- `AND`
- `NOT`
- `==`, `!=`, `>`, `<`, `>=`, `<=`

Parentheses are supported.
Comparisons are numeric when both sides parse as numbers, otherwise string-based.
Falsy values are empty string, `0`, `false`, `FALSE`, `no`, and `NO`.

## Paths

- Empty path means the script directory.
- Absolute paths are cleaned and used directly.
- Relative paths are resolved against the running `.gs` file.
- `EXEC` runs with the script directory as its working directory.

Package hosts may add extra safety checks such as zip-slip blocking.

## Control flow

Recommended helper form:

```gs
FUNC setup
  LOGS INFO, preparing

CALL setup
```
