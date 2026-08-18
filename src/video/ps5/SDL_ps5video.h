/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>
*/

#ifndef SDL_ps5video_h_
#define SDL_ps5video_h_

#include <sys/event.h>

#include "../SDL_sysvideo.h"

typedef struct PS5_VideoBuf {
    void *data;
    uint64_t reserved[3];
} PS5_VideoBuf;

typedef struct PS5_VideoAttr {
    uint8_t reserved[80];
} PS5_VideoAttr;

typedef struct PS5_DeviceData {
    int handle;
    PS5_VideoBuf vbuf[2];
    struct kevent *evt_queue;
    intptr_t paddr;
    size_t memsize;
    SDL_Surface *surface;
    bool mapped;
    uint32_t frame_id;
} PS5_DeviceData;

typedef struct PS5_WindowData {
    void *pixels;
    int pitch;
} PS5_WindowData;

typedef struct PS5_DrawChunk {
    uint32_t *src;
    uint32_t *dst;
    int frame_width;
    int frame_height;
    size_t src_start;
    size_t src_end;
} PS5_DrawChunk;

int sceSystemServiceHideSplashScreen(void);
int sceKernelAllocateMainDirectMemory(size_t, size_t, int, intptr_t *);
int sceKernelMapDirectMemory(void **, size_t, int, int, intptr_t, size_t);
int sceKernelReleaseDirectMemory(intptr_t, size_t);
int sceKernelCreateEqueue(struct kevent **, const char *);
int sceKernelWaitEqueue(struct kevent *, struct kevent *, int, int *, uint *);
int sceKernelDeleteEqueue(struct kevent *);
int sceVideoOutOpen(int, int, int, const void *);
void sceVideoOutClose(int);
int sceVideoOutAddFlipEvent(struct kevent *, int, void *);
int sceVideoOutSetFlipRate(int, int);
int sceVideoOutSubmitFlip(int, int, uint32_t, int64_t);
int sceVideoOutDeleteFlipEvent(struct kevent *, int);
void sceVideoOutSetBufferAttribute2(PS5_VideoAttr *, uint64_t, uint32_t,
                                    uint32_t, uint32_t, uint64_t, uint32_t,
                                    uint64_t);
int sceVideoOutRegisterBuffers2(int, int, int, PS5_VideoBuf *, int,
                                PS5_VideoAttr *, int, void *);

#endif /* SDL_ps5video_h_ */
