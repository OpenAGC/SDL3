/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#include "SDL_internal.h"

#ifdef SDL_AUDIO_DRIVER_PS5

#include "../SDL_audiodev_c.h"
#include "../SDL_sysaudio.h"
#include "SDL_ps5audio.h"

static int PS5AUDIO_SampleFrames(const int requested)
{
    if (requested >= 2048) return 2048;
    if (requested >= 1792) return 1792;
    if (requested >= 1536) return 1536;
    if (requested >= 1280) return 1280;
    if (requested >= 1024) return 1024;
    if (requested >= 768) return 768;
    if (requested >= 512) return 512;
    return 256;
}

static bool PS5AUDIO_OpenDevice(SDL_AudioDevice *device)
{
    const SDL_AudioFormat *formats;
    SDL_AudioFormat format = 0;
    Uint8 hardware_format;

    device->hidden = (struct SDL_PrivateAudioData *)SDL_calloc(1, sizeof(*device->hidden));
    if (!device->hidden) {
        return false;
    }

    formats = SDL_ClosestAudioFormats(device->spec.format);
    while (*formats) {
        if (*formats == SDL_AUDIO_S16LE || *formats == SDL_AUDIO_F32LE) {
            format = *formats;
            break;
        }
        ++formats;
    }
    if (!format) {
        return SDL_SetError("PS5 audio does not support the requested format");
    }

    device->spec.format = format;
    device->sample_frames = PS5AUDIO_SampleFrames(device->sample_frames);
    device->spec.freq = 48000;
    device->spec.channels = (device->spec.channels == 1) ? 1 : 2;

    if (format == SDL_AUDIO_S16LE) {
        hardware_format = (device->spec.channels == 1) ?
            PROSPERO_AUDIO_OUT_PARAM_FORMAT_S16_MONO :
            PROSPERO_AUDIO_OUT_PARAM_FORMAT_S16_STEREO;
    } else {
        hardware_format = (device->spec.channels == 1) ?
            PROSPERO_AUDIO_OUT_PARAM_FORMAT_FLOAT_MONO :
            PROSPERO_AUDIO_OUT_PARAM_FORMAT_FLOAT_STEREO;
    }

    SDL_UpdatedAudioDeviceFormat(device);
    device->hidden->handle = sceAudioOutOpen(
        PROSPERO_USER_SERVICE_USER_ID_SYSTEM,
        PROSPERO_AUDIO_OUT_PORT_TYPE_MAIN,
        0, device->sample_frames, device->spec.freq, hardware_format);
    if (device->hidden->handle < 0) {
        const int error = device->hidden->handle;
        SDL_free(device->hidden);
        device->hidden = NULL;
        return SDL_SetError("sceAudioOutOpen failed: 0x%08x", (unsigned int)error) == 0;
    }

    device->hidden->mixlen = device->buffer_size;
    device->hidden->mixbuf = (Uint8 *)SDL_calloc(1, device->hidden->mixlen);
    if (!device->hidden->mixbuf) {
        sceAudioOutClose(device->hidden->handle);
        device->hidden->handle = -1;
        SDL_free(device->hidden);
        device->hidden = NULL;
        return SDL_OutOfMemory();
    }
    SDL_memset(device->hidden->mixbuf, device->silence_value, device->hidden->mixlen);
    return true;
}

static bool PS5AUDIO_PlayDevice(SDL_AudioDevice *device, const Uint8 *buffer, int buflen)
{
    (void)buffer;
    (void)buflen;
    return sceAudioOutOutput(device->hidden->handle, device->hidden->mixbuf) >= 0;
}

static bool PS5AUDIO_WaitDevice(SDL_AudioDevice *device)
{
    (void)device;
    return true;  /* sceAudioOutOutput() blocks until the fragment is accepted. */
}

static Uint8 *PS5AUDIO_GetDeviceBuf(SDL_AudioDevice *device, int *buffer_size)
{
    *buffer_size = device->hidden->mixlen;
    return device->hidden->mixbuf;
}

static void PS5AUDIO_CloseDevice(SDL_AudioDevice *device)
{
    if (!device->hidden) {
        return;
    }
    if (device->hidden->handle >= 0) {
        sceAudioOutClose(device->hidden->handle);
        device->hidden->handle = -1;
    }
    SDL_free(device->hidden->mixbuf);
    SDL_free(device->hidden);
    device->hidden = NULL;
}

static bool PS5AUDIO_Init(SDL_AudioDriverImpl *impl)
{
    if (sceAudioOutInit() != 0) {
        SDL_SetError("sceAudioOutInit failed");
        return false;
    }

    impl->OpenDevice = PS5AUDIO_OpenDevice;
    impl->PlayDevice = PS5AUDIO_PlayDevice;
    impl->WaitDevice = PS5AUDIO_WaitDevice;
    impl->GetDeviceBuf = PS5AUDIO_GetDeviceBuf;
    impl->CloseDevice = PS5AUDIO_CloseDevice;
    impl->OnlyHasDefaultPlaybackDevice = true;
    return true;
}

AudioBootStrap PS5AUDIO_bootstrap = {
    "ps5", "PS5 SceAudioOut audio driver", PS5AUDIO_Init, false, false
};

#endif /* SDL_AUDIO_DRIVER_PS5 */
