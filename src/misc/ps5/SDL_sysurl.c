/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>
*/

#include "SDL_internal.h"

#include "../SDL_sysurl.h"

int sceUserServiceInitialize(void *);
int sceSystemServiceLaunchWebBrowser(const char *, void *);

bool SDL_SYS_OpenURL(const char *url)
{
    if (sceUserServiceInitialize(NULL) != 0) {
        SDL_SetError("sceUserServiceInitialize failed");
        return false;
    }
    return sceSystemServiceLaunchWebBrowser(url, NULL) == 0;
}
