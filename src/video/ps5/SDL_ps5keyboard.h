/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>
*/

#ifndef SDL_ps5keyboard_h_
#define SDL_ps5keyboard_h_

#include "../SDL_sysvideo.h"

bool PS5_Keyboard_Init(void);
bool PS5_Keyboard_Open(void);
void PS5_Keyboard_PumpEvents(void);
void PS5_Keyboard_Close(void);
bool PS5_HasScreenKeyboardSupport(SDL_VideoDevice *_this);
void PS5_ShowScreenKeyboard(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID props);
void PS5_HideScreenKeyboard(SDL_VideoDevice *_this, SDL_Window *window);

#endif /* SDL_ps5keyboard_h_ */
