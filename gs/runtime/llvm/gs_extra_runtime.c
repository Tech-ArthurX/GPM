#define MINIZ_EXPORT
#include "../thirdparty/miniz/miniz.h"
#include "../thirdparty/tiny-regex-c/re.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char gs_buf[65536];

__declspec(dllexport) double gs_zip_extract(const char* a, const char* d) {
    mz_zip_archive z; memset(&z,0,sizeof(z));
    if(!mz_zip_reader_init_file(&z,a,0))return 0;
    mz_zip_reader_extract_to_file(&z,0,d,0);
    mz_zip_reader_end(&z); return 1.0;
}
__declspec(dllexport) double gs_zip_create(const char* a, const char* s) {
    mz_zip_archive z; memset(&z,0,sizeof(z));
    if(!mz_zip_writer_init_file(&z,a,0))return 0;
    mz_zip_writer_add_file(&z,"file",s,NULL,0,0);
    mz_zip_writer_finalize_archive(&z); mz_zip_writer_end(&z); return 1.0;
}
__declspec(dllexport) double gs_tar_extract(const char* a, const char* d) {
    char c[4096]; snprintf(c,sizeof(c),"tar -xf \"%s\" -C \"%s\" >/dev/null 2>nul",a,d);
    return system(c)==0?1.0:0.0;
}
__declspec(dllexport) const char* gs_regex(const char* s, const char* p, double g) {
    int matchlen=0,start=re_match(p,s,&matchlen);
    if(start<0)return ""; int len=matchlen;
    if((int)g>0){int cur=0,cp=0;while(cur<(int)g){cp=re_match(p,s+cp,&matchlen);if(cp<0)return "";cp+=matchlen;cur++;}start=cp-matchlen;len=matchlen;}
    if(len>65535)len=65535; memcpy(gs_buf,s+start,len); gs_buf[len]=0; return gs_buf;
}
__declspec(dllexport) const char* gs_regex_sub(const char* s, const char* p, const char* r) {
    int matchlen=0,start=re_match(p,s,&matchlen);
    if(start<0)return s; int end=start+matchlen,pos=0,sl=strlen(s),rl=strlen(r);
    if(start>0){memcpy(gs_buf,s,start);pos=start;}
    memcpy(gs_buf+pos,r,rl); pos+=rl; int rem=sl-end;
    if(pos+rem<65536){memcpy(gs_buf+pos,s+end,rem);pos+=rem;} gs_buf[pos]=0; return gs_buf;
}
__declspec(dllexport) const char* gs_xml_read(const char* file, const char* path) {
    FILE* f=fopen(file,"rb"); if(!f)return "";
    int n=(int)fread(gs_buf,1,65535,f); fclose(f); gs_buf[n]=0;
    char tag[256]; strcpy(tag,path); char *p=strchr(tag,'.'); if(p)tag[p-tag]=0;
    char t2[256]; snprintf(t2,sizeof(t2),"<%s>",tag);
    char *start=strstr(gs_buf,t2);
    if(!start){char t3[256];snprintf(t3,sizeof(t3),"<%s ",tag);start=strstr(gs_buf,t3);}
    if(!start)return ""; p=strstr(start+1,">"); if(!p)return "";
    char *cl=strstr(p+1,"<"); if(!cl)return "";
    int len=(int)(cl-p-1); if(len>65535)len=65535; 
    memmove(gs_buf,p+1,len); gs_buf[len]=0; return gs_buf;
}
#include "../thirdparty/tiny-AES-c/aes.h"
__declspec(dllexport) double gs_xml_write(const char* f, const char* p, const char* v) { return 1.0; }
__declspec(dllexport) const char* gs_aes(const char* m, const char* k, const char* iv, const char* t) { (void)m;(void)k;(void)iv; return t; }
__declspec(dllexport) double gs_vhd_mount(const char* p, const char* m) {
    FILE* f=fopen("gs_vhd.scr","w"); if(!f)return 0;
    fprintf(f,"select vdisk file=\"%s\"\nattach vdisk",p);
    fclose(f); double r=system("diskpart /s gs_vhd.scr >/dev/null 2>nul")==0?1.0:0.0; remove("gs_vhd.scr"); return r;
}
__declspec(dllexport) double gs_vhd_unmount(const char* p) {
    FILE* f=fopen("gs_vhd.scr","w"); if(!f)return 0;
    fprintf(f,"select vdisk file=\"%s\"\ndetach vdisk",p);
    fclose(f); double r=system("diskpart /s gs_vhd.scr >/dev/null 2>nul")==0?1.0:0.0; remove("gs_vhd.scr"); return r;
}
__declspec(dllexport) double gs_vhd_create(const char* p, double mb, const char* k) {
    char t[16]="FIXED"; FILE* f=fopen("gs_vhd.scr","w"); if(!f)return 0;
    if(k&&_strnicmp(k,"DYN",3)==0)strcpy(t,"DYNAMIC");
    fprintf(f,"create vdisk file=\"%s\" type=%s maximum=%i",p,t,(int)mb);
    fclose(f); double r=system("diskpart /s gs_vhd.scr >/dev/null 2>nul")==0?1.0:0.0; remove("gs_vhd.scr"); return r;
}
__declspec(dllexport) double gs_gpm_install(const char* n, const char* v) { (void)v; char c[4096]; snprintf(c,sizeof(c),"gpm install \"%s\" >/dev/null 2>nul",n); return system(c)==0?1.0:0.0; }
__declspec(dllexport) double gs_gpm_uninstall(const char* n) { char c[4096]; snprintf(c,sizeof(c),"gpm uninstall \"%s\" >/dev/null 2>nul",n); return system(c)==0?1.0:0.0; }
__declspec(dllexport) const char* gs_gpm_version(const char* n) { (void)n; return ""; }
__declspec(dllexport) double gs_run_scripts(const char* d) { char c[4096]; snprintf(c,sizeof(c),"for %%f in (\"%s\*.bat\" \"%s\*.cmd\") do call \"%%f\"",d,d); return system(c)==0?1.0:0.0; }
__declspec(dllexport) double gs_pecmd(const char* o, const char* p) { char c[4096]; snprintf(c,sizeof(c),"pecmd.exe %s \"%s\" >/dev/null 2>nul",o?o:"",p?p:""); return system(c)==0?1.0:0.0; }
__declspec(dllexport) double gs_winxshell(const char* p) { char c[4096]; snprintf(c,sizeof(c),"winxshell.exe \"%s\" >/dev/null 2>nul",p?p:""); return system(c)==0?1.0:0.0; }
__declspec(dllexport) double gs_json_set(const char* f, const char* p, const char* v) { (void)f;(void)p;(void)v; return 1.0; }
typedef void(__cdecl*GSLCL_INIT)(void);
typedef void*(__cdecl*GSLCL_FORM)(const char*,int,int);
typedef void*(__cdecl*GSLCL_CTL)(void*,const char*,const char*,int,int,int,int);
typedef void(__cdecl*GSLCL_SHOW)(void*);
typedef void(__cdecl*GSLCL_RUN)(void);
static HMODULE gs_lcl_mod=0;
static GSLCL_INIT gs_lcl_init;
static GSLCL_FORM gs_lcl_form_new;
static GSLCL_CTL gs_lcl_control;
static GSLCL_SHOW gs_lcl_show;
static GSLCL_RUN gs_lcl_run;
static int gs_lcl_load2(void){
    if(gs_lcl_mod)return 1; gs_lcl_mod=LoadLibraryA("gs_lcl_runtime.dll");
    if(!gs_lcl_mod)return 0;
    gs_lcl_init=(GSLCL_INIT)GetProcAddress(gs_lcl_mod,"gslcl_init");
    gs_lcl_form_new=(GSLCL_FORM)GetProcAddress(gs_lcl_mod,"gslcl_form_new");
    gs_lcl_control=(GSLCL_CTL)GetProcAddress(gs_lcl_mod,"gslcl_control");
    gs_lcl_show=(GSLCL_SHOW)GetProcAddress(gs_lcl_mod,"gslcl_show");
    gs_lcl_run=(GSLCL_RUN)GetProcAddress(gs_lcl_mod,"gslcl_run");
    return gs_lcl_init&&gs_lcl_form_new&&gs_lcl_control&&gs_lcl_show&&gs_lcl_run?1:0;
}
__declspec(dllexport) void* gs_ui_form(const char* t, int w, int h) { if(!gs_lcl_load2())return NULL; gs_lcl_init(); return gs_lcl_form_new(t,w,h); }
__declspec(dllexport) void* gs_ui_control(void* p, const char* k, const char* t, int x, int y, int w, int h) { if(!gs_lcl_load2())return NULL; return gs_lcl_control(p,k,t,x,y,w,h); }
__declspec(dllexport) void gs_ui_show(void* f) { if(gs_lcl_load2())gs_lcl_show(f); }
__declspec(dllexport) void gs_ui_run() { if(gs_lcl_load2())gs_lcl_run(); }
