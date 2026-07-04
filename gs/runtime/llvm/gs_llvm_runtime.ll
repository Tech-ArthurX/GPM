target triple = "x86_64-pc-windows-msvc"

@.mode.w = private unnamed_addr constant [1 x i8] c"3"
@.mode.a = private unnamed_addr constant [1 x i8] c"3"
@.mode.r = private unnamed_addr constant [1 x i8] c"3"
@.fmt.num = private unnamed_addr constant [1 x i8] c"6"
@.empty = private unnamed_addr constant [1 x i8] c"1"
@.fmt.rmdir = private unnamed_addr constant [2 x i8] c"28"
@.fmt.mkdir = private unnamed_addr constant [2 x i8] c"22"
@.fmt.mlink = private unnamed_addr constant [2 x i8] c"36"
@.fmt.reg_get = private unnamed_addr constant [2 x i8] c"55"
@.fmt.reg_set = private unnamed_addr constant [2 x i8] c"55"
@.fmt.reg_del = private unnamed_addr constant [2 x i8] c"40"
@.fmt.reg_del_key = private unnamed_addr constant [2 x i8] c"31"
@.fmt.tmp = private unnamed_addr constant [2 x i8] c"24"
@.forx_data = internal global [592 x i8] zeroinitializer
@.num_buf = internal global [64 x i8] zeroinitializer

declare ptr @fopen(ptr, ptr)
declare i32 @fputs(ptr, ptr)
declare ptr @fgets(ptr, i32, ptr)
declare i32 @fclose(ptr)
declare i32 @snprintf(ptr, i64, ptr, ...)
declare i32 @system(ptr)
declare ptr @strstr(ptr, ptr)
declare ptr @strrchr(ptr, i32)
declare i64 @strlen(ptr)
declare i32 @strcmp(ptr, ptr)
declare ptr @strcpy(ptr, ptr)
declare ptr @strncpy(ptr, ptr, i64)
declare i32 @toupper(i32)
declare i32 @LoadLibraryA(ptr)
declare ptr @GetProcAddress(ptr, ptr)
declare i32 @FreeLibrary(ptr)
declare i32 @MessageBoxA(ptr, ptr, ptr, i32)
declare i32 @Beep(i32, i32)
declare void @ExitProcess(i32)
declare ptr @FindFirstFileA(ptr, ptr)
declare i32 @FindNextFileA(ptr, ptr)
declare i32 @FindClose(ptr)
declare i32 @GetFileAttributesA(ptr)
declare i32 @DeleteFileA(ptr)
declare i32 @CopyFileA(ptr, ptr, i32)
declare i32 @MoveFileA(ptr, ptr)
declare i32 @CreateHardLinkA(ptr, ptr, ptr)
declare i32 @CreateSymbolicLinkA(ptr, ptr, i32)

define ptr @gs_num_to_string(double %v) {
entry:
  %buf = getelementptr inbounds [64 x i8], ptr @.num_buf, i32 0, i32 0
  %fmt = getelementptr inbounds [6 x i8], ptr @.fmt.num, i32 0, i32 0
  call i32 (ptr, i64, ptr, ...) @snprintf(ptr %buf, i64 63, ptr %fmt, double %v)
  ret ptr %buf
}

define double @gs_strlen2(ptr %s) {
  %l = call i64 @strlen(ptr %s)
  %d = uitofp i64 %l to double
  ret double %d
}

define double @gs_lpos2(ptr %s, ptr %sub) {
  %p = call ptr @strstr(ptr %s, ptr %sub)
  %n = icmp eq ptr %p, null
  br i1 %n, label %nf, label %fnd
fnd:
  %pd = ptrtoint ptr %p to i64
  %sb = ptrtoint ptr %s to i64
  %o = sub i64 %pd, %sb
  %r = sitofp i64 %o to double
  ret double %r
nf:
  ret double -1.0
}

define double @gs_rpos2(ptr %s, ptr %sub) {
  %l = call i64 @strlen(ptr %s)
  %sl = call i64 @strlen(ptr %sub)
  %ls = icmp eq i64 %sl, 0
  br i1 %ls, label %nf2, label %scan
scan:
  %i = alloca i64
  store i64 %l, ptr %i
  br label %loop
loop:
  %v = load i64, ptr %i
  %z = icmp slt i64 %v, 0
  br i1 %z, label %nf2, label %chk
chk:
  %pp = getelementptr i8, ptr %s, i64 %v
  %m = call ptr @strstr(ptr %pp, ptr %sub)
  %ok = icmp eq ptr %m, %pp
  br i1 %ok, label %fnd2, label %dec
dec:
  %d2 = sub i64 %v, 1
  store i64 %d2, ptr %i
  br label %loop
fnd2:
  %r2 = sitofp i64 %v to double
  ret double %r2
nf2:
  ret double -1.0
}

define ptr @gs_lstr(ptr %s, double %n) {
  %ni = fptosi double %n to i64
  %l = call i64 @strlen(ptr %s)
  %cmp = icmp sge i64 %ni, %l
  br i1 %cmp, label %full, label %part
full:
  ret ptr %s
part:
  %end = getelementptr i8, ptr %s, i64 %ni
  store i8 0, ptr %end
  ret ptr %s
}

define ptr @gs_rstr(ptr %s, double %n) {
  %ni = fptosi double %n to i64
  %l = call i64 @strlen(ptr %s)
  %cmp = icmp sge i64 %ni, %l
  br i1 %cmp, label %full, label %part
full:
  ret ptr %s
part:
  %start = sub i64 %l, %ni
  %p = getelementptr i8, ptr %s, i64 %start
  ret ptr %p
}

define ptr @gs_mstr(ptr %s, double %from, double %len) {
  %fi = fptosi double %from to i64
  %ni = fptosi double %len to i64
  %l = call i64 @strlen(ptr %s)
  %cmp = icmp sge i64 %fi, %l
  br i1 %cmp, label %empty, label %ok
empty:
  ret ptr @.empty
ok:
  %p = getelementptr i8, ptr %s, i64 %fi
  %rem = sub i64 %l, %fi
  %take = icmp slt i64 %ni, %rem
  br i1 %take, label %clip, label %full2
full2:
  ret ptr %p
clip:
  %end2 = getelementptr i8, ptr %p, i64 %ni
  store i8 0, ptr %end2
  ret ptr %p
}

define double @gs_file_exist(ptr %p) {
  %a = call i32 @GetFileAttributesA(ptr %p)
  %ne = icmp ne i32 %a, -1
  %r = select i1 %ne, double 1.0, double 0.0
  ret double %r
}

define void @gs_file_del(ptr %p) {
  call i32 @DeleteFileA(ptr %p)
  ret void
}

define double @gs_file_copy(ptr %s, ptr %d) {
  %r = call i32 @CopyFileA(ptr %s, ptr %d, i32 0)
  %ok = icmp ne i32 %r, 0
  %v = select i1 %ok, double 1.0, double 0.0
  ret double %v
}

define double @gs_file_move(ptr %s, ptr %d) {
  %r = call i32 @MoveFileA(ptr %s, ptr %d)
  %ok = icmp ne i32 %r, 0
  %v = select i1 %ok, double 1.0, double 0.0
  ret double %v
}

define double @gs_file_write(ptr %p, ptr %c) {
  %mode = getelementptr inbounds [3 x i8], ptr @.mode.w, i32 0, i32 0
  %f = call ptr @fopen(ptr %p, ptr %mode)
  %isnull = icmp eq ptr %f, null
  br i1 %isnull, label %fail, label %ok
ok:
  call i32 @fputs(ptr %c, ptr %f)
  call i32 @fclose(ptr %f)
  ret double 1.0
fail:
  ret double 0.0
}

define double @gs_file_append(ptr %p, ptr %c) {
  %mode = getelementptr inbounds [3 x i8], ptr @.mode.a, i32 0, i32 0
  %f = call ptr @fopen(ptr %p, ptr %mode)
  %isnull = icmp eq ptr %f, null
  br i1 %isnull, label %fail, label %ok
ok:
  call i32 @fputs(ptr %c, ptr %f)
  call i32 @fclose(ptr %f)
  ret double 1.0
fail:
  ret double 0.0
}

define double @gs_file_read(ptr %p, ptr %out, i32 %n) {
  %mode = getelementptr inbounds [3 x i8], ptr @.mode.r, i32 0, i32 0
  %f = call ptr @fopen(ptr %p, ptr %mode)
  %isnull = icmp eq ptr %f, null
  br i1 %isnull, label %fail, label %ok
ok:
  %max = sub i32 %n, 1
  %r = call ptr @fgets(ptr %out, i32 %max, ptr %f)
  call i32 @fclose(ptr %f)
  %nr = icmp eq ptr %r, null
  br i1 %nr, label %fail2, label %done
fail:
  store i8 0, ptr %out
  ret double 0.0
done:
  %bytes = call i64 @strlen(ptr %out)
  %d = uitofp i64 %bytes to double
  ret double %d
fail2:
  ret double 0.0
}

define void @gs_dir_make(ptr %p) {
  %cmd = alloca [2048 x i8]
  %fmt = getelementptr inbounds [22 x i8], ptr @.fmt.mkdir, i32 0, i32 0
  call i32 (ptr, i64, ptr, ...) @snprintf(ptr %cmd, i64 2048, ptr %fmt, ptr %p)
  call i32 @system(ptr %cmd)
  ret void
}

define void @gs_dir_del(ptr %p) {
  %cmd = alloca [2048 x i8]
  %fmt = getelementptr inbounds [28 x i8], ptr @.fmt.rmdir, i32 0, i32 0
  call i32 (ptr, i64, ptr, ...) @snprintf(ptr %cmd, i64 2048, ptr %fmt, ptr %p)
  call i32 @system(ptr %cmd)
  ret void
}

define double @gs_dir_list(ptr %pattern, ptr %out, i32 %n) {
  %data = getelementptr inbounds [592 x i8], ptr @.forx_data, i32 0, i32 0
  %h = call ptr @FindFirstFileA(ptr %pattern, ptr %data)
  %inv = icmp eq ptr %h, inttoptr (i64 -1 to ptr)
  br i1 %inv, label %err, label %first
first:
  store i8 0, ptr %out
  %name1 = getelementptr i8, ptr %data, i64 44
  %dot1 = load i8, ptr %name1
  %isskip = icmp eq i8 %dot1, 46
  br i1 %isskip, label %loop, label %add1
add1:
  call ptr @strcpy(ptr %out, ptr %name1)
  br label %loop
loop:
  %ok = call i32 @FindNextFileA(ptr %h, ptr %data)
  %nok = icmp eq i32 %ok, 0
  br i1 %nok, label %done, label %add
add:
  %name = getelementptr i8, ptr %data, i64 44
  %dot = load i8, ptr %name
  %skip = icmp eq i8 %dot, 46
  br i1 %skip, label %loop, label %cat
cat:
  %cur = call i64 @strlen(ptr %out)
  call ptr @strncpy(ptr %out, ptr %name, i64 %cur)
  br label %loop
done:
  call i32 @FindClose(ptr %h)
  %len = call i64 @strlen(ptr %out)
  %d = uitofp i64 %len to double
  ret double %d
err:
  store i8 0, ptr %out
  ret double 0.0
}

define double @gs_fext(ptr %p, ptr %out) {
  %d = call ptr @strrchr(ptr %p, i32 46)
  %n = icmp eq ptr %d, null
  br i1 %n, label %nf, label %fnd
fnd:
  call ptr @strcpy(ptr %out, ptr %d)
  ret double 1.0
nf:
  store i8 0, ptr %out
  ret double 0.0
}

define double @gs_fdrv(ptr %p, ptr %out) {
  %c = load i8, ptr %p
  %cm = icmp eq i8 %c, 0
  br i1 %cm, label %nf, label %ok
ok:
  store i8 %c, ptr %out
  %p2 = getelementptr i8, ptr %out, i64 1
  store i8 58, ptr %p2
  %p3 = getelementptr i8, ptr %out, i64 2
  store i8 0, ptr %p3
  ret double 1.0
nf:
  store i8 0, ptr %out
  ret double 0.0
}

define double @gs_link(ptr %typ, ptr %src, ptr %dst) {
  %r = alloca i32
  %cc = load i8, ptr %typ
  %ce = zext i8 %cc to i32
  %upc = call i32 @toupper(i32 %ce)
  %isH = icmp eq i32 %upc, 72
  br i1 %isH, label %hard, label %chkS
chkS:
  %isS = icmp eq i32 %upc, 83
  br i1 %isS, label %sym, label %junc
hard:
  %rv = call i32 @CreateHardLinkA(ptr %dst, ptr %src, ptr null)
  store i32 %rv, ptr %r
  br label %done
sym:
  %attr = call i32 @GetFileAttributesA(ptr %src)
  %isdir = and i32 %attr, 16
  %flag = icmp ne i32 %isdir, 0
  %f = zext i1 %flag to i32
  %rv2 = call i32 @CreateSymbolicLinkA(ptr %dst, ptr %src, i32 %f)
  store i32 %rv2, ptr %r
  br label %done
junc:
  %cmd = alloca [4096 x i8]
  %mlinkfmt = getelementptr inbounds [36 x i8], ptr @.fmt.mlink, i32 0, i32 0
  call i32 (ptr, i64, ptr, ...) @snprintf(ptr %cmd, i64 4096, ptr %mlinkfmt, ptr %dst, ptr %src)
  %rv3 = call i32 @system(ptr %cmd)
  store i32 %rv3, ptr %r
  br label %done
done:
  %res = load i32, ptr %r
  %ok = icmp eq i32 %res, 0
  %v = select i1 %ok, double 1.0, double 0.0
  ret double %v
}

define double @gs_exec(ptr %mode, ptr %cmdline, ptr %target, ptr %params) {
  %c0 = load i8, ptr %mode
  %ext = zext i8 %c0 to i32
  %upc0 = call i32 @toupper(i32 %ext)
  %isW = icmp eq i32 %upc0, 87
  br i1 %isW, label %wait, label %dummy
dummy:
  ret double 1.0
wait:
  %r = call i32 @system(ptr %cmdline)
  %ok = icmp eq i32 %r, 0
  %v = select i1 %ok, double 1.0, double 0.0
  ret double %v
}

define ptr @gs_dll_open(ptr %name) {
  %h = call ptr @LoadLibraryA(ptr %name)
  ret ptr %h
}

define ptr @gs_dll_sym(ptr %h, ptr %name) {
  %p = call ptr @GetProcAddress(ptr %h, ptr %name)
  ret ptr %p
}

define void @gs_dll_close(ptr %h) {
  call i32 @FreeLibrary(ptr %h)
  ret void
}

define double @gs_dll_call(ptr %p, i64 %a0, i64 %a1, i64 %a2, i64 %a3, i64 %a4, i64 %a5, i64 %a6, i64 %a7) {
  %fn = bitcast ptr %p to ptr
  %r = call i64 %fn(i64 %a0, i64 %a1, i64 %a2, i64 %a3, i64 %a4, i64 %a5, i64 %a6, i64 %a7)
  %d = sitofp i64 %r to double
  ret double %d
}

define double @gs_beep(double %freq, double %ms) {
  %f = fptosi double %freq to i32
  %m = fptosi double %ms to i32
  %r = call i32 @Beep(i32 %f, i32 %m)
  %d = sitofp i32 %r to double
  ret double %d
}

define void @gs_msg(ptr %text, ptr %title, i32 %flags) {
  call i32 @MessageBoxA(ptr null, ptr %text, ptr %title, i32 %flags)
  ret void
}

define void @gs_exit(i32 %code) {
  call void @ExitProcess(i32 %code)
  ret void
}

declare double @gs_reg_get(ptr, ptr, ptr, i32)
declare double @gs_firewall(ptr, ptr)
declare double @gs_json_set(ptr, ptr, ptr)
declare double @gs_vhd_unmount(ptr)
declare double @gs_vhd_create(ptr, double, ptr)
declare ptr @gs_xml_read(ptr, ptr)
declare double @gs_xml_write(ptr, ptr, ptr)
declare ptr @gs_aes(ptr, ptr, ptr, ptr)
declare double @gs_zip_extract(ptr, ptr)
declare double @gs_zip_create(ptr, ptr)
declare double @gs_tar_extract(ptr, ptr)
declare double @gs_gpm_install(ptr, ptr)
declare double @gs_gpm_uninstall(ptr)
declare ptr @gs_gpm_version(ptr)
declare i32 @gs_http(ptr, ptr, ptr, ptr, i32, ptr)
declare i32 @gs_down(ptr, ptr)
declare i32 @gs_upld(ptr, ptr, ptr, i32, ptr)
declare ptr @gs_regex(ptr, ptr, double)
declare double @gs_run_scripts(ptr)
declare double @gs_pecmd(ptr, ptr)
declare double @gs_winxshell(ptr)

declare ptr @gs_ui_form(ptr, i32, i32)
declare ptr @gs_ui_control(ptr, ptr, ptr, i32, i32, i32, i32)
declare void @gs_ui_show(ptr)
declare void @gs_ui_run()

define ptr @gs_forx_first(ptr %pattern, ptr %out) {
  %data = getelementptr inbounds [592 x i8], ptr @.forx_data, i32 0, i32 0
  %h = call ptr @FindFirstFileA(ptr %pattern, ptr %data)
  %inv = icmp eq ptr %h, inttoptr (i64 -1 to ptr)
  br i1 %inv, label %err, label %found
found:
  %name = getelementptr i8, ptr %data, i64 44
  call ptr @strcpy(ptr %out, ptr %name)
  ret ptr %h
err:
  ret ptr null
}

define i32 @gs_forx_next(ptr %h, ptr %out) {
  %data = getelementptr inbounds [592 x i8], ptr @.forx_data, i32 0, i32 0
  %ok = call i32 @FindNextFileA(ptr %h, ptr %data)
  %name = getelementptr i8, ptr %data, i64 44
  call ptr @strcpy(ptr %out, ptr %name)
  ret i32 %ok
}

define void @gs_forx_close(ptr %h) {
  call i32 @FindClose(ptr %h)
  ret void
}
