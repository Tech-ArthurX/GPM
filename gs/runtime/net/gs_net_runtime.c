#define CURL_STATICLIB
#include "gs_net_runtime.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct gs_mem {
    char* data;
    size_t len;
    size_t cap;
};

static int gs_net_ready;

static void gs_net_init(void) {
    if (!gs_net_ready) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        gs_net_ready = 1;
    }
}

static size_t gs_write_mem(void* ptr, size_t size, size_t nmemb, void* userdata) {
    struct gs_mem* m = (struct gs_mem*)userdata;
    size_t n = size * nmemb;
    if (!m || !m->data || m->cap == 0) return n;
    size_t room = (m->len + 1 < m->cap) ? (m->cap - m->len - 1) : 0;
    size_t take = n < room ? n : room;
    if (take) {
        memcpy(m->data + m->len, ptr, take);
        m->len += take;
        m->data[m->len] = 0;
    }
    return n;
}

static size_t gs_write_file(void* ptr, size_t size, size_t nmemb, void* userdata) {
    return fwrite(ptr, size, nmemb, (FILE*)userdata);
}

static void gs_apply_common(CURL* c, const char* url) {
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "gs/1.0");
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 0L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
}

int gs_http(const char* method, const char* url, const char* body, char* out, int out_size, int* status_code) {
    gs_net_init();
    if (out && out_size > 0) out[0] = 0;
    if (status_code) *status_code = 0;
    if (!method || !url) return 0;

    CURL* c = curl_easy_init();
    if (!c) return 0;

    struct gs_mem mem;
    mem.data = out;
    mem.len = 0;
    mem.cap = out_size > 0 ? (size_t)out_size : 0;

    gs_apply_common(c, url);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, gs_write_mem);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &mem);

    if (_stricmp(method, "GET") == 0) {
        curl_easy_setopt(c, CURLOPT_HTTPGET, 1L);
    } else if (_stricmp(method, "POST") == 0) {
        curl_easy_setopt(c, CURLOPT_POST, 1L);
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, body ? body : "");
    } else {
        curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method);
        if (body && body[0]) curl_easy_setopt(c, CURLOPT_POSTFIELDS, body);
    }

    CURLcode rc = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    if (status_code) *status_code = (int)code;
    curl_easy_cleanup(c);
    return rc == CURLE_OK ? 1 : 0;
}

int gs_down(const char* url, const char* file) {
    gs_net_init();
    if (!url || !file) return 0;
    FILE* f = fopen(file, "wb");
    if (!f) return 0;
    CURL* c = curl_easy_init();
    if (!c) {
        fclose(f);
        return 0;
    }
    gs_apply_common(c, url);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, gs_write_file);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
    CURLcode rc = curl_easy_perform(c);
    curl_easy_cleanup(c);
    fclose(f);
    return rc == CURLE_OK ? 1 : 0;
}

int gs_upld(const char* file, const char* url, char* out, int out_size, int* status_code) {
    gs_net_init();
    if (out && out_size > 0) out[0] = 0;
    if (status_code) *status_code = 0;
    if (!file || !url) return 0;

    CURL* c = curl_easy_init();
    if (!c) return 0;

    struct gs_mem mem;
    mem.data = out;
    mem.len = 0;
    mem.cap = out_size > 0 ? (size_t)out_size : 0;

    curl_mime* form = curl_mime_init(c);
    curl_mimepart* part = curl_mime_addpart(form);
    curl_mime_name(part, "file");
    curl_mime_filedata(part, file);

    gs_apply_common(c, url);
    curl_easy_setopt(c, CURLOPT_MIMEPOST, form);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, gs_write_mem);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &mem);

    CURLcode rc = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    if (status_code) *status_code = (int)code;

    curl_mime_free(form);
    curl_easy_cleanup(c);
    return rc == CURLE_OK ? 1 : 0;
}
