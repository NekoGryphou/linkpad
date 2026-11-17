#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* Temporary diagnostic interposer. It never changes a widget or reads pixels. */
static void (*start_probe)(void);
static void (*button_observer)(void *, int, bool);

__attribute__((constructor)) static void load_probe(void)
{
    const char *path = getenv("STEAMLINK_PROBE_MODULE");
    const char *(*version)(void) = dlsym(RTLD_DEFAULT, "qVersion");
    if (!path || !version) return;
    if (strcmp(version(), "5.14.1")) {
        fprintf(stderr, "STEAMLINK_SHORTCUT disabled: unsupported Qt %s\n", version());
        return;
    }
    void *module = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!module) {
        fprintf(stderr, "STEAMLINK_PROBE_DISABLED %s\n", dlerror());
        return;
    }
    start_probe = (void (*)(void))dlsym(module, "steamlink_probe_start");
    button_observer = (void (*)(void *, int, bool))dlsym(module, "steamlink_native_button");
    fprintf(stderr, "STEAMLINK_PROBE_LOADED qt=%s\n", version());
}

void intercept_button(void *, int, bool) __asm__("_ZN16QControllerEvent9setButtonENS_9EventTypeEb");
void intercept_button(void *event, int button, bool down)
{
    static void (*original)(void *, int, bool);
    if (!original) original = dlsym(RTLD_NEXT, "_ZN16QControllerEvent9setButtonENS_9EventTypeEb");
    if (original) original(event, button, down);
    if (button_observer) button_observer(event, button, down);
}

int intercept_exec(void) __asm__("_ZN12QApplication4execEv");

int intercept_exec(void)
{
    int (*original)(void) = (int (*)(void))dlsym(RTLD_NEXT, "_ZN12QApplication4execEv");
    if (start_probe) start_probe();
    return original ? original() : 1;
}
