# UI XML

Raw Win32 backend uses `.ui` XML through `UIDF`:

```gs
UIDF WIN = main.ui
UILP WIN
```

Example XML:

```xml
<window title="GS UI" width="520" height="320">
  <label text="Name:" x="20" y="20" w="60" h="22" />
  <edit id="name" text="hello" x="90" y="18" w="200" h="24" />
  <button id="ok" text="OK" x="20" y="60" w="100" h="32" />
</window>
```

Supported controls: `label`, `edit`, `button`, `check`, `group`, `list`.