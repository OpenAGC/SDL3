/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>
*/

#include "SDL_internal.h"

#ifdef SDL_VIDEO_DRIVER_PS5

#include <wchar.h>

#include "../../events/SDL_keyboard_c.h"
#include "SDL_ps5keyboard.h"

typedef struct PS5_KeyboardState {
    uint64_t reserved0[2];
    uint8_t available;
    uint32_t reserved1[2];
    uint32_t modifiers;
    uint16_t scankey[16];
    uint64_t reserved2[4];
} PS5_KeyboardState;

typedef enum SceImeDialogStatus {
    SCE_IME_DIALOG_STATUS_NONE,
    SCE_IME_DIALOG_STATUS_RUNNING,
    SCE_IME_DIALOG_STATUS_FINISHED
} SceImeDialogStatus;

typedef int (*SceImeTextFilter)(wchar_t *, uint32_t *, const wchar_t *, uint32_t);

typedef struct SceImeDialogParam {
    int userId;
    int type;
    uint64_t supportedLanguages;
    int enterLabel;
    int inputMethod;
    SceImeTextFilter filter;
    uint32_t option;
    uint32_t maxTextLength;
    wchar_t *inputTextBuffer;
    float posx;
    float posy;
    int halign;
    int valign;
    const wchar_t *placeholder;
    const wchar_t *title;
    int8_t reserved[16];
} SceImeDialogParam;

typedef struct SceImeDialogResult {
    int outcome;
    int8_t reserved[12];
} SceImeDialogResult;

#define SCE_IME_DIALOG_END_STATUS_OK 0
#define SCE_IME_DIALOG_END_STATUS_USER_CANCELED 1
#define SCE_IME_DIALOG_END_STATUS_ABORTED 2

int sceKeyboardInit(void);
int sceKeyboardOpen(int, int, int, void *);
int sceKeyboardReadState(int, PS5_KeyboardState *);
int sceKeyboardClose(int);
int sceUserServiceInitialize(void *);
int sceUserServiceGetForegroundUser(int *);
int sceImeDialogInit(const SceImeDialogParam *, void *);
int sceImeDialogGetResult(SceImeDialogResult *);
int sceImeDialogTerm(void);
SceImeDialogStatus sceImeDialogGetStatus(void);

static int keyboard_handle = -1;
static PS5_KeyboardState previous_state;
static SceImeDialogStatus ime_status = SCE_IME_DIALOG_STATUS_NONE;
static wchar_t ime_title[128];
static wchar_t ime_text[2048];
static SceImeDialogParam ime_param = {
    .maxTextLength = SDL_arraysize(ime_text),
    .inputTextBuffer = ime_text,
    .title = ime_title
};

static void PS5_SendKey(SDL_Scancode scancode, bool down)
{
    SDL_SendKeyboardKey(0, SDL_GLOBAL_KEYBOARD_ID, 0, scancode, down);
}

static bool PS5_KeyPresent(const uint16_t keys[16], uint16_t key)
{
    for (int i = 0; i < 16; ++i) {
        if (keys[i] == key) {
            return true;
        }
    }
    return false;
}

bool PS5_Keyboard_Init(void)
{
    int error = sceUserServiceInitialize(NULL);
    if (error != 0 && error != (int)0x80960003) {
        return SDL_SetError("sceUserServiceInitialize failed: 0x%08x", (unsigned int)error);
    }
    error = sceKeyboardInit();
    if (error != 0) {
        return SDL_SetError("sceKeyboardInit failed: 0x%08x", (unsigned int)error);
    }
    return true;
}

bool PS5_Keyboard_Open(void)
{
    int user_id;
    long reserved = 0;
    int error = sceUserServiceGetForegroundUser(&user_id);
    if (error != 0) {
        return SDL_SetError("sceUserServiceGetForegroundUser failed: 0x%08x", (unsigned int)error);
    }
    ime_param.userId = user_id;
    keyboard_handle = sceKeyboardOpen(user_id, 0, 0, &reserved);
    if (keyboard_handle <= 0) {
        return SDL_SetError("sceKeyboardOpen failed: 0x%08x", (unsigned int)keyboard_handle);
    }
    SDL_zero(previous_state);
    return true;
}

void PS5_Keyboard_Close(void)
{
    if (keyboard_handle > 0) {
        sceKeyboardClose(keyboard_handle);
        keyboard_handle = -1;
    }
}

static void PS5_ImeDialogPumpEvents(void)
{
    const SceImeDialogStatus status = sceImeDialogGetStatus();
    SceImeDialogResult result;
    char text[4096];

    if (status == ime_status) {
        return;
    }
    ime_status = status;
    if (status == SCE_IME_DIALOG_STATUS_RUNNING) {
        SDL_SendScreenKeyboardShown();
        return;
    }
    if (status != SCE_IME_DIALOG_STATUS_FINISHED) {
        return;
    }

    SDL_zero(result);
    if (sceImeDialogGetResult(&result) == 0 && result.outcome == SCE_IME_DIALOG_END_STATUS_OK) {
        if (wcstombs(text, ime_text, sizeof(text)) != (size_t)-1) {
            text[sizeof(text) - 1] = '\0';
            SDL_SendKeyboardText(text);
        }
        SDL_SendKeyboardKeyAutoRelease(0, SDL_SCANCODE_RETURN);
    }
    sceImeDialogTerm();
    ime_status = SCE_IME_DIALOG_STATUS_NONE;
    SDL_SendScreenKeyboardHidden();
}

void PS5_Keyboard_PumpEvents(void)
{
    static const struct {
        uint32_t mask;
        SDL_Scancode scancode;
    } modifiers[] = {
        { 0x01, SDL_SCANCODE_LCTRL }, { 0x02, SDL_SCANCODE_LSHIFT },
        { 0x04, SDL_SCANCODE_LALT },  { 0x08, SDL_SCANCODE_LGUI },
        { 0x10, SDL_SCANCODE_RCTRL }, { 0x20, SDL_SCANCODE_RSHIFT },
        { 0x40, SDL_SCANCODE_RALT },  { 0x80, SDL_SCANCODE_RGUI }
    };
    PS5_KeyboardState current;
    uint32_t changed;

    PS5_ImeDialogPumpEvents();
    if (keyboard_handle <= 0) {
        return;
    }
    SDL_zero(current);
    if (sceKeyboardReadState(keyboard_handle, &current) != 0 || !current.available) {
        return;
    }

    changed = previous_state.modifiers ^ current.modifiers;
    for (int i = 0; i < SDL_arraysize(modifiers); ++i) {
        if (changed & modifiers[i].mask) {
            PS5_SendKey(modifiers[i].scancode, (current.modifiers & modifiers[i].mask) != 0);
        }
    }
    for (int i = 0; i < 16; ++i) {
        const uint16_t old_key = previous_state.scankey[i];
        const uint16_t new_key = current.scankey[i];
        if (old_key && !PS5_KeyPresent(current.scankey, old_key)) {
            PS5_SendKey((SDL_Scancode)old_key, false);
        }
        if (new_key && !PS5_KeyPresent(previous_state.scankey, new_key)) {
            PS5_SendKey((SDL_Scancode)new_key, true);
        }
    }
    previous_state = current;
}

bool PS5_HasScreenKeyboardSupport(SDL_VideoDevice *_this)
{
    (void)_this;
    return true;
}

void PS5_ShowScreenKeyboard(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID props)
{
    (void)_this;
    (void)window;
    (void)props;
    SDL_memset(ime_text, 0, sizeof(ime_text));
    if (sceImeDialogInit(&ime_param, NULL) != 0) {
        SDL_SetError("sceImeDialogInit failed");
    }
}

void PS5_HideScreenKeyboard(SDL_VideoDevice *_this, SDL_Window *window)
{
    (void)_this;
    (void)window;
    if (ime_status != SCE_IME_DIALOG_STATUS_NONE) {
        sceImeDialogTerm();
        ime_status = SCE_IME_DIALOG_STATUS_NONE;
        SDL_SendScreenKeyboardHidden();
    }
}

#endif /* SDL_VIDEO_DRIVER_PS5 */
