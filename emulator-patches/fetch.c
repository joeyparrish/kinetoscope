// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Emulation of Kinetoscope video streaming hardware.
// Separate implementation of fetching, with Curl+pthread for native and with
// emscripten_fetch for web.
// An emscripten build requires -s FETCH=1 at link time.

#if defined(__EMSCRIPTEN__)
# include <emscripten/fetch.h>
#elif defined(_WIN32) || defined(__MINGW32__)
# include <windows.h>
#else
# include <curl/curl.h>
# include <pthread.h>
#endif

#include <stdint.h>
#include <stdio.h>

//      bytes written           buffer, num_things, thing_size, context
typedef size_t (*WriteCallback)(char*, size_t, size_t, void *);
//                           ok,   context
typedef void (*DoneCallback)(bool, void *);
//                          terminated error string
typedef void (*ReportError)(const char* buf);

typedef struct FetchContext {
  void* user_ctx;
  char* url;
  WriteCallback write_callback;
  DoneCallback done_callback;
#if !defined(__EMSCRIPTEN__)
  CURL* handle;
#endif
} FetchContext;

#if defined(__EMSCRIPTEN__)
static void fetch_with_emscripten_success(emscripten_fetch_t* fetch) {
  FetchContext* ctx = (FetchContext*)fetch->userData;

  int http_status = fetch->status;
  printf("Kinetoscope: url = %s, http status = %d\n", ctx->url, http_status);

  bool ok = http_status == 200 || http_status == 206;
  if (ok) {
    ctx->write_callback((char*)fetch->data, fetch->numBytes, 1, ctx->user_ctx);
  }

  if (ctx->done_callback) {
    ctx->done_callback(ok, ctx->user_ctx);
  }

  free(ctx->url);
  free(ctx);
  emscripten_fetch_close(fetch);
}

static void fetch_with_emscripten_error(emscripten_fetch_t* fetch) {
  FetchContext* ctx = (FetchContext*)fetch->userData;

  printf("Kinetoscope: url = %s, error!\n", ctx->url);
  ctx->done_callback(/* ok= */ false, ctx->user_ctx);

  free(ctx->url);
  free(ctx);
  emscripten_fetch_close(fetch);
}
#else
static void fetch_with_curl_in_thread(void* thread_ctx) {
  FetchContext* ctx = (FetchContext*)thread_ctx;

  CURLcode res = curl_easy_perform(ctx->handle);
  long http_status = 0;
  curl_easy_getinfo(ctx->handle, CURLINFO_RESPONSE_CODE, &http_status);
  curl_easy_cleanup(ctx->handle);

  printf("Kinetoscope: url = %s, CURLcode = %d, http status = %ld\n",
         ctx->url, res, http_status);
  if (res != CURLE_OK) {
    printf("Curl error: %s\n", curl_easy_strerror(res));
  }

  bool ok = res == CURLE_OK && (http_status == 200 || http_status == 206);
  ctx->done_callback(ok, ctx->user_ctx);

  free(ctx->url);
  free(ctx);
}

# if defined(_WIN32) || defined(__MINGW32__)
static DWORD WINAPI fetch_with_curl_in_windows_thread(void* thread_ctx) {
  fetch_with_curl_in_thread(thread_ctx);
  return 0;
}
# else
static void* fetch_with_curl_in_pthread(void* thread_ctx) {
  fetch_with_curl_in_thread(thread_ctx);
  pthread_exit(NULL);
  return NULL;
}
# endif
#endif

static void fetch_range_async(const char* url, size_t first_byte, size_t size,
                              WriteCallback write_callback,
                              DoneCallback done_callback,
                              void* user_ctx) {
  char range[32];
  bool use_range = false;
  if (size != (size_t)-1) {
    size_t last_byte = first_byte + size - 1;
    snprintf(range, 32, "%zd-%zd", first_byte, last_byte);
    // snprintf doesn't guarantee a terminator when it overflows.
    range[31] = '\0';
    use_range = true;
  }

  FetchContext* ctx = (FetchContext*)malloc(sizeof(FetchContext));
  ctx->user_ctx = user_ctx;
  ctx->url = strdup(url);
  ctx->write_callback = write_callback;
  ctx->done_callback = done_callback;

#if defined(__EMSCRIPTEN__)
  emscripten_fetch_attr_t fetch_attributes;
  emscripten_fetch_attr_init(&fetch_attributes);

  strcpy(fetch_attributes.requestMethod, "GET");
  fetch_attributes.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;

  const char* headers[] = { "Range", range, NULL };
  fetch_attributes.requestHeaders = headers;

  fetch_attributes.userData = ctx;
  fetch_attributes.onsuccess = fetch_with_emscripten_success;
  fetch_attributes.onerror = fetch_with_emscripten_error;

  emscripten_fetch(&fetch_attributes, url);
#else
  CURL* handle = curl_easy_init();
  ctx->handle = handle;

  curl_easy_setopt(handle, CURLOPT_URL, url);
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, ctx);
  if (use_range) {
    curl_easy_setopt(handle, CURLOPT_RANGE, range);
  }

# if defined(_WIN32) || defined(__MINGW32__)
  CreateThread(/* security attributes= */ NULL,
               /* default stack size= */ 0,
               fetch_with_curl_in_windows_thread,
               /* thread context= */ NULL,
               /* creation flags= */ 0,
               /* thread id output= */ NULL);
# else
  pthread_t thread;
  pthread_create(&thread, NULL, fetch_with_curl_in_pthread, ctx);
# endif
#endif
}
