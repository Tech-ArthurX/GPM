# WinAPI

High-level wrappers:

```gs
BEEP 800, 100
MBOX hello,title,info
MBOX failed,title,error
```

Static/implicit WinAPI:

```gs
APIC MessageBoxA, NULL, text, title, MB_OK
APIC R = user32.MessageBoxA, 0, text, title, 0
```

The compiler hides `uintptr` and pointer conversion.