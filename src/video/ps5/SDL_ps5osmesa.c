/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>
*/

#include "SDL_internal.h"

#ifdef SDL_VIDEO_OPENGL_OSMESA

#include <SDL3/SDL_opengl.h>

#include "SDL_ps5osmesa.h"

#ifndef OSMESA_Y_UP
#define OSMESA_Y_UP 0x11
#endif
#define OSMESA_FORMAT 0x22
#define OSMESA_DEPTH_BITS 0x30
#define OSMESA_STENCIL_BITS 0x31
#define OSMESA_PROFILE 0x33
#define OSMESA_CORE_PROFILE 0x34
#define OSMESA_COMPAT_PROFILE 0x35
#define OSMESA_CONTEXT_MAJOR_VERSION 0x36
#define OSMESA_CONTEXT_MINOR_VERSION 0x37

typedef struct SDL_GLDriverData {
    void *(*OSMesaGetProcAddress)(const char *);
    void *(*OSMesaCreateContext)(GLenum, void *);
    void *(*OSMesaCreateContextAttribs)(const int *, void *);
    void (*OSMesaDestroyContext)(void *);
    GLboolean (*OSMesaMakeCurrent)(void *, void *, GLenum, GLsizei, GLsizei);
    void (*OSMesaPixelStore)(GLint, GLint);
    void (*OSMesaFlush)(void);
} SDL_GLDriverData;

static void PS5_OSMesa_UnloadLibrary(SDL_VideoDevice *_this);

static bool PS5_OSMesa_LoadLibrary(SDL_VideoDevice *_this, const char *path)
{
    static Uint32 dummy_pixel;
    SDL_GLDriverData *data;
    SDL_SharedObject *handle;
    void *dummy_context;

    if (_this->gl_data) {
        return SDL_SetError("OSMesa library already loaded") == 0;
    }
    if (!path || !*path) {
        path = "libOSMesa.so.8";
    }
    handle = SDL_LoadObject(path);
    if (!handle) {
        handle = SDL_LoadObject("libOSMesa.so");
    }
    if (!handle) {
        return SDL_SetError("unable to load %s", path) == 0;
    }

    data = (SDL_GLDriverData *)SDL_calloc(1, sizeof(*data));
    if (!data) {
        SDL_UnloadObject(handle);
        return SDL_OutOfMemory();
    }
#define PS5_OSMESA_LOAD(name) \
    data->name = (void *)SDL_LoadFunction(handle, #name); \
    if (!data->name) { SDL_free(data); SDL_UnloadObject(handle); return SDL_SetError("missing OSMesa symbol %s", #name) == 0; }
    PS5_OSMESA_LOAD(OSMesaGetProcAddress)
    PS5_OSMESA_LOAD(OSMesaCreateContext)
    PS5_OSMESA_LOAD(OSMesaDestroyContext)
    PS5_OSMESA_LOAD(OSMesaMakeCurrent)
    PS5_OSMESA_LOAD(OSMesaPixelStore)
#undef PS5_OSMESA_LOAD
    data->OSMesaCreateContextAttribs = (void *)SDL_LoadFunction(handle, "OSMesaCreateContextAttribs");
    data->OSMesaFlush = (void (*)(void))data->OSMesaGetProcAddress("glFlush");
    if (!data->OSMesaFlush) {
        SDL_free(data);
        SDL_UnloadObject(handle);
        return SDL_SetError("missing OSMesa glFlush") == 0;
    }

    _this->gl_config.dll_handle = handle;
    SDL_strlcpy(_this->gl_config.driver_path, path, sizeof(_this->gl_config.driver_path));
    _this->gl_data = data;

    dummy_context = data->OSMesaCreateContext(GL_RGBA, NULL);
    if (!dummy_context || !data->OSMesaMakeCurrent(dummy_context, &dummy_pixel,
                                                     GL_UNSIGNED_BYTE, 1, 1)) {
        if (dummy_context) {
            data->OSMesaDestroyContext(dummy_context);
        }
        PS5_OSMesa_UnloadLibrary(_this);
        return SDL_SetError("unable to initialize OSMesa") == 0;
    }
    data->OSMesaDestroyContext(dummy_context);
    return true;
}

static void PS5_OSMesa_UnloadLibrary(SDL_VideoDevice *_this)
{
    SDL_GLDriverData *data = (SDL_GLDriverData *)_this->gl_data;
    if (data) {
        SDL_free(data);
        _this->gl_data = NULL;
    }
    if (_this->gl_config.dll_handle) {
        SDL_UnloadObject(_this->gl_config.dll_handle);
        _this->gl_config.dll_handle = NULL;
    }
}

static SDL_FunctionPointer PS5_OSMesa_GetProcAddress(SDL_VideoDevice *_this, const char *proc)
{
    SDL_GLDriverData *data = (SDL_GLDriverData *)_this->gl_data;
    return data ? (SDL_FunctionPointer)data->OSMesaGetProcAddress(proc) : NULL;
}

static bool PS5_OSMesa_MakeCurrent(SDL_VideoDevice *_this, SDL_Window *window, SDL_GLContext context)
{
    SDL_GLDriverData *data = (SDL_GLDriverData *)_this->gl_data;
    SDL_Surface *surface = SDL_GetWindowSurface(window);
    if (!data || !surface) {
        return SDL_SetError("OSMesa framebuffer is not available") == 0;
    }
    if (!data->OSMesaMakeCurrent(context, surface->pixels, GL_UNSIGNED_BYTE,
                                 surface->w, surface->h)) {
        return SDL_SetError("OSMesaMakeCurrent failed") == 0;
    }
    data->OSMesaPixelStore(OSMESA_Y_UP, 0);
    return true;
}

static SDL_GLContext PS5_OSMesa_CreateContext(SDL_VideoDevice *_this, SDL_Window *window)
{
    SDL_GLDriverData *data = (SDL_GLDriverData *)_this->gl_data;
    void *context = NULL;
    (void)window;
    if (!data) {
        SDL_SetError("OSMesa is not loaded");
        return NULL;
    }
    if (data->OSMesaCreateContextAttribs) {
        const int profile = (_this->gl_config.profile_mask & SDL_GL_CONTEXT_PROFILE_CORE) ?
            OSMESA_CORE_PROFILE : OSMESA_COMPAT_PROFILE;
        const int attribs[] = {
            OSMESA_FORMAT, GL_RGBA,
            OSMESA_PROFILE, profile,
            OSMESA_CONTEXT_MAJOR_VERSION, _this->gl_config.major_version > 0 ? _this->gl_config.major_version : 3,
            OSMESA_CONTEXT_MINOR_VERSION, _this->gl_config.minor_version,
            OSMESA_DEPTH_BITS, _this->gl_config.depth_size,
            OSMESA_STENCIL_BITS, _this->gl_config.stencil_size,
            0
        };
        context = data->OSMesaCreateContextAttribs(attribs, NULL);
    }
    if (!context) {
        context = data->OSMesaCreateContext(GL_RGBA, NULL);
    }
    if (!context || !PS5_OSMesa_MakeCurrent(_this, window, context)) {
        if (context) {
            data->OSMesaDestroyContext(context);
        }
        return NULL;
    }
    return (SDL_GLContext)context;
}

static bool PS5_OSMesa_SetSwapInterval(SDL_VideoDevice *_this, int interval)
{
    (void)_this;
    (void)interval;
    return true;
}

static bool PS5_OSMesa_GetSwapInterval(SDL_VideoDevice *_this, int *interval)
{
    (void)_this;
    *interval = 0;
    return true;
}

static bool PS5_OSMesa_SwapWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
    SDL_GLDriverData *data = (SDL_GLDriverData *)_this->gl_data;
    if (!data) {
        return SDL_SetError("OSMesa is not loaded") == 0;
    }
    data->OSMesaFlush();
    return SDL_UpdateWindowSurface(window);
}

static bool PS5_OSMesa_DestroyContext(SDL_VideoDevice *_this, SDL_GLContext context)
{
    SDL_GLDriverData *data = (SDL_GLDriverData *)_this->gl_data;
    if (data && context) {
        data->OSMesaDestroyContext(context);
    }
    return true;
}

int PS5_OSMesa_InitDevice(SDL_VideoDevice *device)
{
    device->GL_LoadLibrary = PS5_OSMesa_LoadLibrary;
    device->GL_GetProcAddress = PS5_OSMesa_GetProcAddress;
    device->GL_UnloadLibrary = PS5_OSMesa_UnloadLibrary;
    device->GL_CreateContext = PS5_OSMesa_CreateContext;
    device->GL_MakeCurrent = PS5_OSMesa_MakeCurrent;
    device->GL_SetSwapInterval = PS5_OSMesa_SetSwapInterval;
    device->GL_GetSwapInterval = PS5_OSMesa_GetSwapInterval;
    device->GL_SwapWindow = PS5_OSMesa_SwapWindow;
    device->GL_DestroyContext = PS5_OSMesa_DestroyContext;
    return 0;
}

#endif /* SDL_VIDEO_OPENGL_OSMESA */
