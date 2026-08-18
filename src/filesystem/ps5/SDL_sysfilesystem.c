/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>
*/

#include "SDL_internal.h"

#ifdef SDL_FILESYSTEM_PS5

#include "../SDL_sysfilesystem.h"

#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

int sceUserServiceInitialize(void *);
int sceUserServiceGetForegroundUser(uint32_t *);

const char **getargv(void);

char *SDL_SYS_GetBasePath(void)
{
    const char **argv = getargv();
    char path[PATH_MAX];
    char *slash;

    if (!argv || !argv[0]) {
        return SDL_strdup("/data/");
    }
    if (argv[0][0] == '/') {
        SDL_strlcpy(path, argv[0], sizeof(path));
    } else if (!getcwd(path, sizeof(path))) {
        return SDL_strdup("/data/");
    } else {
        SDL_strlcat(path, "/", sizeof(path));
        SDL_strlcat(path, argv[0], sizeof(path));
    }
    slash = SDL_strrchr(path, '/');
    if (slash) {
        slash[1] = '\0';
    }
    return SDL_strdup(path);
}

char *SDL_SYS_GetExeName(void)
{
    return NULL;
}

char *SDL_SYS_GetPrefPath(const char *org, const char *app)
{
    uint32_t user_id;
    char root[64];
    char *result;
    char *cursor;
    size_t length;

    if (!app) {
        SDL_InvalidParamError("app");
        return NULL;
    }
    if (!org) {
        org = "";
    }
    if (sceUserServiceInitialize(NULL) != 0 || sceUserServiceGetForegroundUser(&user_id) != 0) {
        SDL_strlcpy(root, "/data/", sizeof(root));
    } else {
        SDL_snprintf(root, sizeof(root), "/user/home/%04x/", user_id);
    }
    length = SDL_strlen(root) + SDL_strlen(org) + SDL_strlen(app) + 3;
    result = (char *)SDL_malloc(length);
    if (!result) {
        SDL_OutOfMemory();
        return NULL;
    }
    SDL_snprintf(result, length, "%s%s%s%s/", root, *org ? org : "",
                 *org ? "/" : "", app);
    for (cursor = result + 1; *cursor; ++cursor) {
        if (*cursor == '/') {
            *cursor = '\0';
            mkdir(result, 0777);
            *cursor = '/';
        }
    }
    mkdir(result, 0777);
    return result;
}

char *SDL_SYS_GetUserFolder(SDL_Folder folder)
{
    (void)folder;
    SDL_Unsupported();
    return NULL;
}

#endif /* SDL_FILESYSTEM_PS5 */
