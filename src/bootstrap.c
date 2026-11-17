#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Preloaded into Steam Link. Loads native.so only into Valve's Qt 5.14.1, then
 * forwards QApplication::exec and QControllerEvent::setButton to it. */
static void (*start_tool)(void);
static void (*button_observer)(void *, int, bool);

__attribute__((constructor)) static void load_tool(void)
{
    const char *path = getenv("LINKPAD_MODULE");
    const char *(*version)(void) = dlsym(RTLD_DEFAULT, "qVersion");
    if (!path || !version) return;
    if (strcmp(version(), "5.14.1")) {
        fprintf(stderr, "LINKPAD disabled: unsupported Qt %s\n", version());
        return;
    }
    void *module = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!module) {
        fprintf(stderr, "LINKPAD disabled: %s\n", dlerror());
        return;
    }
    start_tool = (void (*)(void))dlsym(module, "linkpad_start");
    button_observer = (void (*)(void *, int, bool))dlsym(module, "linkpad_native_button");
    fprintf(stderr, "LINKPAD loaded qt=%s\n", version());
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
    if (start_tool) start_tool();
    return original ? original() : 1;
}
