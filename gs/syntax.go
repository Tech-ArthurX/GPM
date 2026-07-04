package gs

import "strings"

const InterpreterSyntax = `
GS Interpreter Syntax

Line form:
  COMMAND arg1,arg2,key=value
  "quoted strings" may contain spaces and commas
  %ENV% expands process environment variables
  %@NAME% expands GS local variables

Package phases:
  _PREI/_PREINST          before core install
  _INST/_INSTALLING       during install
  _POST/_POSTINST         after install
  _PREU/_PREUNINST        before uninstall
  _UNINST/_UNINSTALLING   during uninstall
  _POSTU/_POSTUNINST      after uninstall

Control:
  EXIT [code]
  WAIT ms
  CALL name
  IFEX condition,CMD,args...
  WHEN condition ... _END
  LOOP count ... _END
  FORX pattern,dir ... _END
  if/elif/else indentation syntax

Variables and strings:
  SETV KEY=VALUE
  ENVI KEY=VALUE
  CALC KEY=expr
  STRL KEY=TEXT
  LPOS/RPOS KEY=TEXT,SUB
  LSTR/RSTR KEY=TEXT,N
  MSTR KEY=TEXT,FROM,LEN
  RGEX KEY=TEXT,PATTERN[,GROUP]
  RGSB KEY,TEXT,PATTERN,REPLACEMENT

Files and execution:
  EXEC command
  RUNS [dir]                    runs .bat/.cmd/.ini/.exe/.reg/.lua in order
  PECM LOAD|EXEC,path           runs through pecmd.exe
  WNSH lua_script               runs through winxshell.exe
  DRVI op,args...               runs through Drvinstall.exe
  FILE COPY|MOVE|DEL|READ|WRITE|APPEND,path[,args]
  FDIR MAKE|DEL|LIST[,path]
  LINK SYM|HARD|JUNC,src,dst
  FEXT/FDRV/EXIST KEY=path

DRVI examples:
  DRVI B,D:\Drivers[,password[,wificonfig.ini]]
  DRVI T,D:\Drivers[,password[,wificonfig.ini]]
  DRVI Y
  DRVI H
  DRVI IMPORT,D:\driver[,E:\Windows]
  DRVI REMOVE,oem1.inf[,E:\Windows]
  DRVI MIGRATE,E:\Windows[,net;display;audio;bluetooth;system;disk]
  DRVI BACKUP,D:\driver[,E:\Windows[,net]]
  DRVI RAW,-b,D:\Drivers,-p:password,-config:wificonfig.ini

Data and network:
  JSON KEY=source,$.path
  JSNS file,$.path=value
  JSNL KEY=source,$.array
  XMLR KEY=file,path
  XMLW file,path=value
  HTTP METHOD,url[,body]
  DOWN url,file
  UPLD file,url

Crypto and archives:
  HASH KEY=MD5|SHA1|SHA256|SHA512,file_or_@text
  BASE KEY=ENC|DEC,text
  HEXC KEY=ENC|DEC,text
  AESC KEY=ENC|DEC,key,iv,text
  ZIPX archive,dest
  ZIPC archive,srcdir
  TARX archive,dest

Windows system:
  REGI GET|SET|DEL,root\path[,name,value,type]
  SERV START|STOP|RESTART|STATUS,name
  TASK RUN|DEL|QUERY|CREATE,name[,trigger,command]
  FWAL ADD|DEL,netsh rule args
  VHDM path[,RO]
  VHDU path
  VHDC path,mb,FIXED|DYNAMIC

GPM host:
  INSTALLROOT absolute_path
  GPMI name[,version]
  GPMU name
  GPMV KEY=name
  LOGS [level,]message
`

func SyntaxText() string {
	return strings.TrimSpace(InterpreterSyntax) + "\n"
}
