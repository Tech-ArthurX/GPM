FUNC helper
  FILE WRITE,tmp/func.txt,ok

FDIR MAKE,tmp
FILE WRITE,tmp/a.txt,7
CALL helper

SETV X=3
CALC Y=X+4
IFEX %Y% == 7,FILE,WRITE,tmp/ifex.txt,ok

WHEN %Y% >= 7
  FILE APPEND,tmp/when.txt,%Y%
  LINK HARD,tmp/a.txt,tmp/a-hard.txt
_END

LOOP 2
  FILE APPEND,tmp/loop.txt,%INDEX%
_END

if %Y% == 7
  FILE WRITE,tmp/if.txt,yes
else
  FILE WRITE,tmp/if.txt,no

FDIR LIST,tmp,FILES
FORX *.txt,tmp
  FILE APPEND,tmp/list.txt,%FILE%|
_END

STRL LEN=hello
LPOS POS=hello,el
RPOS RPOS=hello,l
FEXT EXT=tmp/a.txt
FDRV DRV=C:\Windows\notepad.exe
EXIST HAS=tmp/a.txt

HASH H=SHA256,@abc
BASE B=ENC,hello
HEXC HX=ENC,hi

JSON J=data.json,$.a.b
JSNL N=data.json,$.items
JSNS data.json,$.a.c=ok

REGI SET,HKCU\Software\GPMGSTest,Name,Value,SZ
REGI GET,HKCU\Software\GPMGSTest,Name,REGVAL
REGI DEL,HKCU\Software\GPMGSTest,Name
SERV STATUS,Spooler
TASK QUERY,\Microsoft\Windows\Defrag\ScheduledDefrag
FWAL ADD,name=GPMGSTest dir=in action=allow program=C:\Windows\System32\notepad.exe
FWAL DEL,name=GPMGSTest

HTTP GET,https://example.com
HTTP POST,https://example.com/api,{"x":1}
DOWN https://example.com/, tmp/down.html
UPLD tmp/a.txt, https://example.com/upload

EXEC WAIT,cmd /c echo wait>>tmp/exec.txt
EXEC HIDE,cmd.exe /c exit
EXEC MIN,cmd.exe /c exit
EXEC OPEN,tmp/a.txt

LOGS INFO, done %@Y% %@J% %@N% %@HTTP_CODE%
