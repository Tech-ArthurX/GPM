SETV X = 100
SETV Y = 3.14
SETV S = hello
SETV B = true
STRV NAME = world
BOOL FLAG = false
FLOT PI = 3.14159
CALC Z = X + 1
LOGS INFO, X = %X%
LOGS INFO, Y = %Y%
LOGS INFO, NAME = %NAME%
LOGS INFO, FLAG = %FLAG%
LOGS INFO, PI = %PI%
LOGS INFO, Z = %Z%
WAPI Beep, 800, 200
DLLO U = user32.dll
DLLG MB = U, MessageBoxA
DLLC R = MB, 0, gs+winapi works, gs compiler, 0
LOGS INFO, MessageBox returned %R%
DLLF U