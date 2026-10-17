#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

__attribute__((constructor))
static void init_copystrings_and_vscode(void) {
    const char *home = getenv("HOME");
    if (!home) return;
    char p1[1024], p2[1024];
    snprintf(p1, sizeof(p1), "%s/.local/share/codeblocks/libcopystrings_orig.so", home);
    snprintf(p2, sizeof(p2), "%s/.local/share/codeblocks/libvscode_helper.so", home);
    dlopen(p1, RTLD_NOW | RTLD_GLOBAL | RTLD_NODELETE);
    dlopen(p2, RTLD_NOW | RTLD_GLOBAL | RTLD_NODELETE);
}
