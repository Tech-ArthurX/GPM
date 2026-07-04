#ifndef GS_NET_RUNTIME_H
#define GS_NET_RUNTIME_H

#ifdef __cplusplus
extern "C" {
#endif

int gs_http(const char* method, const char* url, const char* body, char* out, int out_size, int* status_code);
int gs_down(const char* url, const char* file);
int gs_upld(const char* file, const char* url, char* out, int out_size, int* status_code);

#ifdef __cplusplus
}
#endif

#endif
