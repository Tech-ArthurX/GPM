package ir

// ValType is the gs type. User-visible: float / string / bool.
// Handle / Proc are compiler-internal types produced by DLL ops.
type ValType int

const (
	TypeFloat  ValType = iota // float (covers all numbers, C double)
	TypeString                // string  (const char *)
	TypeBool                  // bool   (C int, 0/1)
	TypeHandle                // HMODULE (DLL handle, internal)
	TypeProc                  // FARPROC (function pointer, internal)
)

// cType returns the C type for declaration.
func (t ValType) cType() string {
	switch t {
	case TypeString:
		return "const char *"
	case TypeBool:
		return "int"
	case TypeHandle:
		return "void *"
	case TypeProc:
		return "FARPROC"
	default:
		return "double"
	}
}

// printfFmt returns the printf format specifier.
func (t ValType) printfFmt() string {
	switch t {
	case TypeString:
		return "%s"
	case TypeBool:
		return "%s" // we feed "true"/"false" string
	case TypeHandle, TypeProc:
		return "%p"
	default:
		return "%g"
	}
}

// printfExpr converts a variable name into the C expression suitable for the format above.
func (t ValType) printfExpr(name string) string {
	if t == TypeBool {
		return "(" + name + " ? \"true\" : \"false\")"
	}
	return name
}

// zeroInit returns the C zero initializer for the type.
func (t ValType) zeroInit() string {
	switch t {
	case TypeString:
		return "\"\""
	case TypeBool:
		return "0"
	case TypeHandle, TypeProc:
		return "NULL"
	default:
		return "0.0"
	}
}
