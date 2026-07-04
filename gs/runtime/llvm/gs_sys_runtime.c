#include <windows.h>
#include <stdio.h>
__declspec(dllexport) double gs_reg_get(const char* p, const char* n, char* o, int sz) {
  HKEY r = HKEY_LOCAL_MACHINE; const char* s = p;
  if (_strnicmp(p,"HKLM",4)==0){r=HKEY_LOCAL_MACHINE;s=p+4;}
  else if(_strnicmp(p,"HKCU",4)==0){r=HKEY_CURRENT_USER;s=p+4;}
  else if(_strnicmp(p,"HKCR",4)==0){r=HKEY_CLASSES_ROOT;s=p+4;}
  else if(_strnicmp(p,"HKU",3)==0){r=HKEY_USERS;s=p+3;}
  else if(_strnicmp(p,"HKCC",4)==0){r=HKEY_CURRENT_CONFIG;s=p+4;}else return 0;
  if(*s=='/'||*s=='\\')s++; HKEY k; DWORD t=0; if(o&&sz>0)o[0]=0;
  if(RegOpenKeyExA(r,s,0,KEY_QUERY_VALUE,&k)!=0)return 0;
  BYTE b[512]; DWORD bs=sizeof(b); LONG l=RegQueryValueExA(k,n&&*n?n:NULL,NULL,&t,b,&bs);
  if(l==0){if(t==REG_DWORD&&bs>=4)snprintf(o,sz,"%lu",*(DWORD*)b);
  else if(t==REG_QWORD&&bs>=8)snprintf(o,sz,"%llu",*(unsigned long long*)b);
  else{int m=bs<sz-1?bs:sz-1;memcpy(o,b,m);o[m]=0;}}
  RegCloseKey(k); return l==0?1.0:0.0;
}
__declspec(dllexport) double gs_reg_set(const char* p,const char* n,const char* v,const char* t){
  HKEY r=HKEY_LOCAL_MACHINE;const char*s=p;
  if(_strnicmp(p,"HKLM",4)==0){r=HKEY_LOCAL_MACHINE;s=p+4;}
  else if(_strnicmp(p,"HKCU",4)==0){r=HKEY_CURRENT_USER;s=p+4;}
  else if(_strnicmp(p,"HKCR",4)==0){r=HKEY_CLASSES_ROOT;s=p+4;}
  else if(_strnicmp(p,"HKU",3)==0){r=HKEY_USERS;s=p+3;}
  else if(_strnicmp(p,"HKCC",4)==0){r=HKEY_CURRENT_CONFIG;s=p+4;}else return 0;
  if(*s=='/'||*s=='\\')s++;HKEY k;
  if(RegCreateKeyExA(r,s,0,NULL,0,KEY_SET_VALUE,NULL,&k,NULL)!=0)return 0;
  char tp[32];int i=0;for(;t&&t[i]&&i<31;i++)tp[i]=(char)toupper(t[i]);tp[i]=0;LONG l;
  if(!strcmp(tp,"DWORD")||!strcmp(tp,"REG_DWORD")){DWORD x=(DWORD)strtoul(v,NULL,0);l=RegSetValueExA(k,n,0,REG_DWORD,(BYTE*)&x,sizeof(x));}
  else if(!strcmp(tp,"QWORD")||!strcmp(tp,"REG_QWORD")){unsigned long long x=_strtoui64(v,NULL,0);l=RegSetValueExA(k,n,0,REG_QWORD,(BYTE*)&x,sizeof(x));}
  else{l=RegSetValueExA(k,n,0,REG_SZ,(const BYTE*)v,(DWORD)strlen(v)+1);}
  RegCloseKey(k);return l==0?1.0:0.0;
}
__declspec(dllexport) double gs_reg_del(const char*p,const char*n){
  HKEY r=HKEY_LOCAL_MACHINE;const char*s=p;
  if(_strnicmp(p,"HKLM",4)==0){r=HKEY_LOCAL_MACHINE;s=p+4;}
  else if(_strnicmp(p,"HKCU",4)==0){r=HKEY_CURRENT_USER;s=p+4;}
  else if(_strnicmp(p,"HKCR",4)==0){r=HKEY_CLASSES_ROOT;s=p+4;}
  else if(_strnicmp(p,"HKU",3)==0){r=HKEY_USERS;s=p+3;}
  else if(_strnicmp(p,"HKCC",4)==0){r=HKEY_CURRENT_CONFIG;s=p+4;}else return 0;
  if(*s=='/'||*s=='\\')s++;
  if(n&&*n){HKEY k;if(RegOpenKeyExA(r,s,0,KEY_SET_VALUE,&k)!=0)return 0;LONG l=RegDeleteValueA(k,n);RegCloseKey(k);return l==0?1.0:0.0;}
  return RegDeleteKeyA(r,s)==0?1.0:0.0;
}
__declspec(dllexport) double gs_service(const char*o,const char*n){char c[1024],u[32];int i=0;for(;o&&o[i]&&i<31;i++)u[i]=(char)toupper(o[i]);u[i]=0;
  if(!strcmp(u,"START"))snprintf(c,1024,"sc start \"%s\" >nul 2>nul",n);
  else if(!strcmp(u,"STOP"))snprintf(c,1024,"sc stop \"%s\" >nul 2>nul",n);
  else if(!strcmp(u,"RESTART")){snprintf(c,1024,"sc stop \"%s\" >nul 2>nul",n);system(c);snprintf(c,1024,"sc start \"%s\" >nul 2>nul",n);}
  else if(!strcmp(u,"STATUS"))snprintf(c,1024,"sc query \"%s\"",n);else return 0;
  return system(c)==0?1.0:0.0;
}
__declspec(dllexport) double gs_task(const char*o,const char*n,const char*t,const char*c2){char c[4096],u[32];int i=0;for(;o&&o[i]&&i<31;i++)u[i]=(char)toupper(o[i]);u[i]=0;
  if(!strcmp(u,"RUN"))snprintf(c,4096,"schtasks /Run /TN \"%s\" >nul 2>nul",n);
  else if(!strcmp(u,"DEL")||!strcmp(u,"DELETE"))snprintf(c,4096,"schtasks /Delete /F /TN \"%s\" >nul 2>nul",n);
  else if(!strcmp(u,"QUERY")||!strcmp(u,"STATUS"))snprintf(c,4096,"schtasks /Query /TN \"%s\"",n);
  else if(!strcmp(u,"CREATE"))snprintf(c,4096,"schtasks /Create /F /TN \"%s\" /SC \"%s\" /TR \"%s\" >nul 2>nul",n,t,c2);
  else return 0;return system(c)==0?1.0:0.0;
}
__declspec(dllexport) double gs_firewall(const char*o,const char*r){char c[4096],u[32];int i=0;for(;o&&o[i]&&i<31;i++)u[i]=(char)toupper(o[i]);u[i]=0;
  if(!strcmp(u,"ADD"))snprintf(c,4096,"netsh advfirewall firewall add rule %s",r);
  else if(!strcmp(u,"DEL")||!strcmp(u,"DELETE"))snprintf(c,4096,"netsh advfirewall firewall delete rule %s",r);
  else return 0;return system(c)==0?1.0:0.0;
}
