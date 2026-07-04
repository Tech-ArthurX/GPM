; ======================
; gs complete test: info passing + PGCB + AGCB
; ======================

; ---------- memory-based info passing ----------
CGSB
static char gs_mem_buf[256];
static void gs_mem_write(const char* d) {
  int i = 0; while (d[i] && i < 255) { gs_mem_buf[i] = d[i]; i++; }
  gs_mem_buf[i] = 0;
}
static const char* gs_mem_read(void) { return gs_mem_buf; }
static void gs_test_mem(void) {
  gs_mem_write("hello from memory");
  gs_mem_read();
}
CGSE
CGSC gs_test_mem();
LOGS INFO, mem done

; ---------- XOR encrypted info passing ----------
CGSB
#define GS_XOR_KEY 0xAB
static char gs_xor_buf[256];
static void gs_xor_write(const char* d) {
  int i = 0; while (d[i] && i < 255) { gs_xor_buf[i] = d[i] ^ GS_XOR_KEY; i++; }
  gs_xor_buf[i] = 0;
}
static const char* gs_xor_read(void) {
  int i = 0; while (gs_xor_buf[i]) { gs_xor_buf[i] ^= GS_XOR_KEY; i++; }
  return gs_xor_buf;
}
static void gs_test_xor(void) {
  gs_xor_write("sensitive payload");
  gs_xor_read();
}
CGSE
CGSC gs_test_xor();
LOGS INFO, enc done

; ---------- PGCB pascal block ----------
PGCB
procedure pgcHello; stdcall;
begin
end;
PGCE

; ---------- AGCB asm block ----------
AGCB
section .text
global agcHello
agcHello:
  ret
AGCE

LOGS INFO, all done