/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>
*/

#ifndef SDL_ps5audio_h_
#define SDL_ps5audio_h_

#include "../SDL_sysaudio.h"

struct SDL_PrivateAudioData {
    int32_t handle;
    Uint8 *mixbuf;
    int mixlen;
};

#define PROSPERO_AUDIO_OUT_PORT_TYPE_MAIN 0
#define PROSPERO_AUDIO_OUT_PARAM_FORMAT_S16_MONO 0
#define PROSPERO_AUDIO_OUT_PARAM_FORMAT_S16_STEREO 1
#define PROSPERO_AUDIO_OUT_PARAM_FORMAT_FLOAT_MONO 3
#define PROSPERO_AUDIO_OUT_PARAM_FORMAT_FLOAT_STEREO 4
#define PROSPERO_USER_SERVICE_USER_ID_SYSTEM 0xFF

int32_t sceAudioOutInit(void);
int32_t sceAudioOutOpen(int32_t userId, int32_t type, int32_t index,
                        uint32_t len, uint32_t freq, uint32_t param);
int32_t sceAudioOutOutput(int32_t handle, const void *p);
int32_t sceAudioOutClose(int32_t handle);

#endif /* SDL_ps5audio_h_ */
