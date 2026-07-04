#ifndef GS_LCL_RUNTIME_H
#define GS_LCL_RUNTIME_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
  #ifdef GS_LCL_RUNTIME_BUILD
    #define GS_LCL_API __declspec(dllexport)
  #else
    #define GS_LCL_API __declspec(dllimport)
  #endif
#else
  #define GS_LCL_API
#endif

GS_LCL_API void  __cdecl gslcl_init(void);
GS_LCL_API void* __cdecl gslcl_form_new(const char* title, int width, int height);
GS_LCL_API void* __cdecl gslcl_control(void* parent, const char* kind, const char* text, int x, int y, int w, int h);
GS_LCL_API void  __cdecl gslcl_show(void* form);
GS_LCL_API void  __cdecl gslcl_run(void);
GS_LCL_API void  __cdecl gslcl_free(void* obj);

#ifdef __cplusplus
}
#endif

#endif /* GS_LCL_RUNTIME_H */