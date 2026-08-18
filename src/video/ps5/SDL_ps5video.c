/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>
*/

#include "SDL_internal.h"

#ifdef SDL_VIDEO_DRIVER_PS5

#include <errno.h>
#include <pthread.h>
#include <sys/mman.h>

#include "SDL_ps5tilemap.inc"
#include "SDL_ps5video.h"
#include "SDL_ps5keyboard.h"
#include "SDL_ps5osmesa.h"

#define PS5_THREAD_COUNT 12
#define PS5_SOFTWARE_WIDTH 1920
#define PS5_SOFTWARE_HEIGHT 1080
#define PS5_SOFTWARE_BUFFER_COUNT 2
#define PS5_SOFTWARE_MEMORY_SIZE 0x4000000
#define PS5_SOFTWARE_PRESENT_TIMEOUT_US 1000000u

static void *PS5_DrawTileThread(void *arg)
{
    const PS5_DrawChunk *chunk = (const PS5_DrawChunk *)arg;
    for (size_t index = chunk->src_start; index < chunk->src_end; ++index) {
        const int x = (int)(index % (size_t)chunk->frame_width);
        const int y = (int)(index / (size_t)chunk->frame_width);
        const int tx = x / PS5_TILE_WIDTH;
        const int ty = y / PS5_TILE_HEIGHT;
        const int tile = PS5_TILE_SIZE * (tx + ty * (chunk->frame_width / PS5_TILE_WIDTH));
        chunk->dst[tile + PS5_tilemap[y % PS5_TILE_HEIGHT][x % PS5_TILE_WIDTH]] = chunk->src[index];
    }
    return NULL;
}

static void PS5_DrawPixelsAsTiles(uint32_t *src, uint32_t *dst, int width, int height)
{
    const size_t pixels = (size_t)width * (size_t)height;
    const size_t chunk_size = pixels / PS5_THREAD_COUNT;
    PS5_DrawChunk chunks[PS5_THREAD_COUNT];
    pthread_t threads[PS5_THREAD_COUNT];
    int started = 0;

    for (int i = 0; i < PS5_THREAD_COUNT; ++i) {
        chunks[i].src = src;
        chunks[i].dst = dst;
        chunks[i].frame_width = width;
        chunks[i].frame_height = height;
        chunks[i].src_start = (size_t)i * chunk_size;
        chunks[i].src_end = (i == PS5_THREAD_COUNT - 1) ? pixels : (size_t)(i + 1) * chunk_size;
        if (pthread_create(&threads[i], NULL, PS5_DrawTileThread, &chunks[i]) != 0) {
            break;
        }
        ++started;
    }
    for (int i = 0; i < started; ++i) {
        pthread_join(threads[i], NULL);
    }
    if (started != PS5_THREAD_COUNT) {
        /* A partial launch is not safe to present; finish synchronously. */
        for (size_t index = 0; index < pixels; ++index) {
            const int x = (int)(index % (size_t)width);
            const int y = (int)(index / (size_t)width);
            const int tile = PS5_TILE_SIZE * ((x / PS5_TILE_WIDTH) +
                                              (y / PS5_TILE_HEIGHT) * (width / PS5_TILE_WIDTH));
            dst[tile + PS5_tilemap[y % PS5_TILE_HEIGHT][x % PS5_TILE_WIDTH]] = src[index];
        }
    }
}

static void PS5_DestroyPresentation(SDL_VideoDevice *_this)
{
    PS5_DeviceData *data = (PS5_DeviceData *)_this->internal;
    if (data->evt_queue) {
        if (data->handle >= 0) {
            sceVideoOutDeleteFlipEvent(data->evt_queue, data->handle);
        }
        sceKernelDeleteEqueue(data->evt_queue);
        data->evt_queue = NULL;
    }
    if (data->handle >= 0) {
        sceVideoOutClose(data->handle);
        data->handle = -1;
    }
    if (data->mapped && data->vbuf[0].data) {
        munmap(data->vbuf[0].data, data->memsize);
        data->vbuf[0].data = NULL;
        data->vbuf[1].data = NULL;
        data->mapped = false;
    }
    if (data->paddr) {
        sceKernelReleaseDirectMemory(data->paddr, data->memsize);
        data->paddr = 0;
    }
    data->memsize = 0;
    if (data->surface) {
        SDL_DestroySurface(data->surface);
        data->surface = NULL;
    }
}

static bool PS5_CreatePresentation(SDL_VideoDevice *_this, int width, int height)
{
    PS5_DeviceData *data = (PS5_DeviceData *)_this->internal;
    PS5_VideoAttr attr;
    void *mapped = NULL;

    data->handle = sceVideoOutOpen(0xFF, 0, 0, NULL);
    if (data->handle < 0) {
        return SDL_SetError("sceVideoOutOpen failed: 0x%08x", (unsigned int)data->handle) == 0;
    }
    data->memsize = PS5_SOFTWARE_MEMORY_SIZE;
    if (sceKernelAllocateMainDirectMemory(data->memsize, 0x20000, 3, &data->paddr) != 0 ||
        sceKernelMapDirectMemory(&mapped, data->memsize, 0x33, 0, data->paddr, 0x20000) != 0) {
        PS5_DestroyPresentation(_this);
        return SDL_SetError("unable to map PS5 video memory") == 0;
    }
    data->vbuf[0].data = mapped;
    data->vbuf[1].data = (Uint8 *)mapped + data->memsize / 2;
    data->mapped = true;

    if (sceKernelCreateEqueue(&data->evt_queue, "SDL PS5 flip queue") != 0 ||
        sceVideoOutAddFlipEvent(data->evt_queue, data->handle, NULL) != 0 ||
        sceVideoOutSetFlipRate(data->handle, 0) != 0) {
        PS5_DestroyPresentation(_this);
        return SDL_SetError("unable to initialize PS5 video events") == 0;
    }

    SDL_zero(attr);
    sceVideoOutSetBufferAttribute2(&attr, 0x8000000022000000ULL, 0,
                                   (uint32_t)width, (uint32_t)height, 0, 0, 0);
    if (sceVideoOutRegisterBuffers2(data->handle, 0, 0, data->vbuf,
                                    PS5_SOFTWARE_BUFFER_COUNT, &attr, 0, NULL) != 0) {
        PS5_DestroyPresentation(_this);
        return SDL_SetError("sceVideoOutRegisterBuffers2 failed") == 0;
    }

    data->surface = SDL_CreateSurfaceZeroed(width, height, SDL_PIXELFORMAT_ABGR8888);
    if (!data->surface) {
        PS5_DestroyPresentation(_this);
        return false;
    }
    return true;
}

static bool PS5_VideoInit(SDL_VideoDevice *_this)
{
    PS5_DeviceData *data = (PS5_DeviceData *)_this->internal;
    SDL_DisplayMode mode;

    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_ABGR8888;
    mode.w = PS5_SOFTWARE_WIDTH;
    mode.h = PS5_SOFTWARE_HEIGHT;
    mode.refresh_rate = 60.0;
    sceSystemServiceHideSplashScreen();

    data->handle = -1;
    if (!PS5_CreatePresentation(_this, mode.w, mode.h)) {
        return false;
    }
    if (SDL_AddBasicVideoDisplay(&mode) == 0) {
        PS5_DestroyPresentation(_this);
        return false;
    }

    (void)PS5_Keyboard_Init();
    (void)PS5_Keyboard_Open();
    SDL_ClearError();
    return true;
}

static void PS5_VideoQuit(SDL_VideoDevice *_this)
{
    PS5_Keyboard_Close();
    PS5_DestroyPresentation(_this);
}

static bool PS5_SetDisplayMode(SDL_VideoDevice *_this, SDL_VideoDisplay *display, SDL_DisplayMode *mode)
{
    (void)_this;
    (void)display;
    (void)mode;
    return true;
}

static bool PS5_CreateWindow(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID create_props)
{
    PS5_WindowData *data;
    (void)_this;
    (void)create_props;
    data = (PS5_WindowData *)SDL_calloc(1, sizeof(*data));
    if (!data) {
        return SDL_OutOfMemory();
    }
    window->internal = (SDL_WindowData *)data;
    return true;
}

static void PS5_DestroyWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
    (void)_this;
    SDL_free(window->internal);
    window->internal = NULL;
}

static bool PS5_CreateWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window,
                                        SDL_PixelFormat *format, void **pixels, int *pitch)
{
    PS5_WindowData *data = (PS5_WindowData *)window->internal;
    int width, height;
    (void)_this;
    SDL_GetWindowSizeInPixels(window, &width, &height);
    data->pitch = width * 4;
    data->pixels = SDL_malloc((size_t)data->pitch * (size_t)height);
    if (!data->pixels) {
        return SDL_OutOfMemory();
    }
    SDL_memset(data->pixels, 0, (size_t)data->pitch * (size_t)height);
    *format = SDL_PIXELFORMAT_ABGR8888;
    *pixels = data->pixels;
    *pitch = data->pitch;
    return true;
}

static bool PS5_UpdateWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window,
                                        const SDL_Rect *rects, int numrects)
{
    PS5_DeviceData *device_data = (PS5_DeviceData *)_this->internal;
    SDL_Surface *surface = SDL_GetWindowSurface(window);
    SDL_Rect destination;
    struct kevent event;
    int count;
    uint timeout = PS5_SOFTWARE_PRESENT_TIMEOUT_US;
    uint8_t index = (uint8_t)(device_data->frame_id % PS5_SOFTWARE_BUFFER_COUNT);

    (void)rects;
    (void)numrects;
    if (!surface) {
        return SDL_SetError("Couldn't find PS5 window surface");
    }
    SDL_FillSurfaceRect(device_data->surface, NULL, 0);
    destination.x = (device_data->surface->w - surface->w) / 2;
    destination.y = (device_data->surface->h - surface->h) / 2;
    destination.w = surface->w;
    destination.h = surface->h;
    if (!SDL_BlitSurface(surface, NULL, device_data->surface, &destination)) {
        return false;
    }
    PS5_DrawPixelsAsTiles((uint32_t *)device_data->surface->pixels,
                          (uint32_t *)device_data->vbuf[index].data,
                          device_data->surface->w, device_data->surface->h);
    if (sceVideoOutSubmitFlip(device_data->handle, index, 1, device_data->frame_id) != 0) {
        return SDL_SetError("sceVideoOutSubmitFlip failed") == 0;
    }
    if (sceKernelWaitEqueue(device_data->evt_queue, &event, 1, &count, &timeout) != 0) {
        return SDL_SetError("sceKernelWaitEqueue failed") == 0;
    }
    ++device_data->frame_id;
    return true;
}

static void PS5_DestroyWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window)
{
    PS5_WindowData *data = (PS5_WindowData *)window->internal;
    (void)_this;
    if (data) {
        SDL_free(data->pixels);
        data->pixels = NULL;
        data->pitch = 0;
    }
}

static void PS5_PumpEvents(SDL_VideoDevice *_this)
{
    (void)_this;
    PS5_Keyboard_PumpEvents();
}

static void PS5_DestroyDevice(SDL_VideoDevice *device)
{
    SDL_free(device->internal);
    SDL_free(device);
}

static SDL_VideoDevice *PS5_CreateDevice(void)
{
    SDL_VideoDevice *device = (SDL_VideoDevice *)SDL_calloc(1, sizeof(*device));
    if (!device) {
        SDL_OutOfMemory();
        return NULL;
    }
    device->internal = (SDL_VideoData *)SDL_calloc(1, sizeof(PS5_DeviceData));
    if (!device->internal) {
        SDL_free(device);
        SDL_OutOfMemory();
        return NULL;
    }
    ((PS5_DeviceData *)device->internal)->handle = -1;
    device->VideoInit = PS5_VideoInit;
    device->VideoQuit = PS5_VideoQuit;
    device->SetDisplayMode = PS5_SetDisplayMode;
    device->PumpEvents = PS5_PumpEvents;
    device->CreateSDLWindow = PS5_CreateWindow;
    device->DestroyWindow = PS5_DestroyWindow;
    device->CreateWindowFramebuffer = PS5_CreateWindowFramebuffer;
    device->UpdateWindowFramebuffer = PS5_UpdateWindowFramebuffer;
    device->DestroyWindowFramebuffer = PS5_DestroyWindowFramebuffer;
    device->HasScreenKeyboardSupport = PS5_HasScreenKeyboardSupport;
    device->ShowScreenKeyboard = PS5_ShowScreenKeyboard;
    device->HideScreenKeyboard = PS5_HideScreenKeyboard;
    device->device_caps = VIDEO_DEVICE_CAPS_FULLSCREEN_ONLY | VIDEO_DEVICE_CAPS_SLOW_FRAMEBUFFER;
#ifdef SDL_VIDEO_OPENGL_OSMESA
    PS5_OSMesa_InitDevice(device);
#endif
    device->free = PS5_DestroyDevice;
    return device;
}

VideoBootStrap PS5_bootstrap = {
    "ps5", "Sony PS5 Video Driver", PS5_CreateDevice, NULL, false
};

#endif /* SDL_VIDEO_DRIVER_PS5 */
