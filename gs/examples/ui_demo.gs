SETV X = 100
STRV NAME = world
BOOL FLAG = false
FLOT PI = 3.14159
CALC Z = X + 1
LOGS INFO, X = %X%
LOGS INFO, NAME = %NAME%
LOGS INFO, FLAG = %FLAG%
LOGS INFO, PI = %PI%
LOGS INFO, Z = %Z%
WAPI Beep, 800, 100
UIDF WIN = main.ui
; UILP WIN  ; keep disabled so tests do not launch/block UI
