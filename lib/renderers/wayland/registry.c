#include "wayland.h"

#include <unistd.h>
#include <sys/mman.h>

const char *BM_XKB_MASK_NAMES[MASK_LAST] = {
    XKB_MOD_NAME_SHIFT,
    XKB_MOD_NAME_CAPS,
    XKB_MOD_NAME_CTRL,
    XKB_MOD_NAME_ALT,
    "Mod2",
    "Mod3",
    XKB_MOD_NAME_LOGO,
    "Mod5",
};

const enum mod_bit BM_XKB_MODS[MASK_LAST] = {
    MOD_SHIFT,
    MOD_CAPS,
    MOD_CTRL,
    MOD_ALT,
    MOD_MOD2,
    MOD_MOD3,
    MOD_LOGO,
    MOD_MOD5
};

static void
xdg_shell_ping(void *data, struct xdg_shell *shell, uint32_t serial)
{
    (void)data;
    xdg_shell_pong(shell, serial);
}

static const struct xdg_shell_listener xdg_shell_listener = {
    .ping = xdg_shell_ping,
};

static void
shm_format(void *data, struct wl_shm *wl_shm, uint32_t format)
{
    (void)wl_shm;
    struct wayland *wayland = data;
    wayland->formats |= (1 << format);
}

struct wl_shm_listener shm_listener = {
    .format = shm_format
};

static void
keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int fd, uint32_t size)
{
    (void)keyboard;
    struct input *input = data;

    if (!data) {
        close(fd);
        return;
    }

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }

    char *map_str;
    if ((map_str = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0)) == MAP_FAILED) {
        close(fd);
        return;
    }

    struct xkb_keymap *keymap = xkb_keymap_new_from_string(input->xkb.context, map_str, XKB_KEYMAP_FORMAT_TEXT_V1, 0);
    munmap(map_str, size);
    close(fd);

    if (!keymap) {
        fprintf(stderr, "failed to compile keymap\n");
        return;
    }

    struct xkb_state *state;
    if (!(state = xkb_state_new(keymap))) {
        fprintf(stderr, "failed to create XKB state\n");
        xkb_keymap_unref(keymap);
        return;
    }

    xkb_keymap_unref(input->xkb.keymap);
    xkb_state_unref(input->xkb.state);
    input->xkb.keymap = keymap;
    input->xkb.state = state;

    for (uint32_t i = 0; i < MASK_LAST; ++i)
        input->xkb.masks[i] = xkb_keymap_mod_get_index(input->xkb.keymap, BM_XKB_MASK_NAMES[i]);
}

static void
keyboard_handle_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys)
{
    (void)data, (void)keyboard, (void)serial, (void)surface, (void)keys;
}

static void
keyboard_handle_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface)
{
    (void)data, (void)keyboard, (void)serial, (void)surface;
}

static void
keyboard_handle_key(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state_w)
{
    (void)keyboard, (void)serial, (void)time;
    struct input *input = data;
    enum wl_keyboard_key_state state = state_w;

    if (!input->xkb.state)
        return;

    uint32_t code = key + 8;
    xkb_keysym_t sym = xkb_state_key_get_one_sym(input->xkb.state, code);

    input->sym = (state == WL_KEYBOARD_KEY_STATE_PRESSED ? sym : XKB_KEY_NoSymbol);
    input->code = (state == WL_KEYBOARD_KEY_STATE_PRESSED ? code : 0);

    if (input->notify.key)
        input->notify.key(state, sym, code);

#if 0
    if (state == WL_KEYBOARD_KEY_STATE_RELEASED &&
            key == input->repeat_key) {
        its.it_interval.tv_sec = 0;
        its.it_interval.tv_nsec = 0;
        its.it_value.tv_sec = 0;
        its.it_value.tv_nsec = 0;
        timerfd_settime(input->repeat_timer_fd, 0, &its, NULL);
    } else if (state == WL_KEYBOARD_KEY_STATE_PRESSED &&
            xkb_keymap_key_repeats(input->xkb.keymap, code)) {
        input->repeat_sym = sym;
        input->repeat_key = key;
        input->repeat_time = time;
        its.it_interval.tv_sec = input->repeat_rate_sec;
        its.it_interval.tv_nsec = input->repeat_rate_nsec;
        its.it_value.tv_sec = input->repeat_delay_sec;
        its.it_value.tv_nsec = input->repeat_delay_nsec;
        timerfd_settime(input->repeat_timer_fd, 0, &its, NULL);
    }
#endif
}

static void
keyboard_handle_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group)
{
    (void)keyboard, (void)serial;
    struct input *input = data;

    if (!input->xkb.keymap)
        return;

    xkb_state_update_mask(input->xkb.state, mods_depressed, mods_latched, mods_locked, 0, 0, group);
    xkb_mod_mask_t mask = xkb_state_serialize_mods(input->xkb.state, XKB_STATE_MODS_DEPRESSED | XKB_STATE_MODS_LATCHED);

    input->modifiers = 0;
    for (uint32_t i = 0; i < MASK_LAST; ++i) {
        if (mask & input->xkb.masks[i])
            input->modifiers |= BM_XKB_MODS[i];
    }
}

static void
keyboard_handle_repeat_info(void *data, struct wl_keyboard *keyboard, int32_t rate, int32_t delay)
{
    (void)data, (void)keyboard, (void)rate, (void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_handle_keymap,
    .enter = keyboard_handle_enter,
    .leave = keyboard_handle_leave,
    .key = keyboard_handle_key,
    .modifiers = keyboard_handle_modifiers,
    .repeat_info = keyboard_handle_repeat_info
};

static void
seat_handle_capabilities(void *data, struct wl_seat *seat, enum wl_seat_capability caps)
{
    struct input *input = data;

    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !input->keyboard) {
        input->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(input->keyboard, &keyboard_listener, data);
    } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && input->keyboard) {
        wl_keyboard_destroy(input->keyboard);
        input->keyboard = NULL;
    }
}

static void
seat_handle_name(void *data, struct wl_seat *seat, const char *name)
{
    (void)data, (void)seat, (void)name;
}

static const struct wl_seat_listener seat_listener = {
    seat_handle_capabilities,
    seat_handle_name
};

static void
registry_handle_global(void *data, struct wl_registry *registry, uint32_t id, const char *interface, uint32_t version)
{
    (void)version;
    struct wayland *wayland = data;

    if (strcmp(interface, "wl_compositor") == 0) {
        wayland->compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 1);
    } else if (strcmp(interface, "xdg_shell") == 0) {
        wayland->xdg_shell = wl_registry_bind(registry, id, &xdg_shell_interface, 1);
        xdg_shell_use_unstable_version(wayland->xdg_shell, 4);
        xdg_shell_add_listener(wayland->xdg_shell, &xdg_shell_listener, data);
    } else if (strcmp(interface, "wl_shell") == 0) {
        wayland->shell = wl_registry_bind(registry, id, &wl_shell_interface, 1);
    } else if (strcmp(interface, "wl_seat") == 0) {
        wayland->seat = wl_registry_bind(registry, id, &wl_seat_interface, XDG_SHELL_VERSION_CURRENT);
        wl_seat_add_listener(wayland->seat, &seat_listener, &wayland->input);
    } else if (strcmp(interface, "wl_shm") == 0) {
        wayland->shm = wl_registry_bind(registry, id, &wl_shm_interface, 1);
        wl_shm_add_listener(wayland->shm, &shm_listener, data);
    }
}

static void
registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    (void)data, (void)registry, (void)name;
}

static const struct wl_registry_listener registry_listener = {
    registry_handle_global,
    registry_handle_global_remove
};

void
bm_wl_registry_destroy(struct wayland *wayland)
{
    assert(wayland);

    if (wayland->shm)
        wl_shm_destroy(wayland->shm);

    if (wayland->shell)
        wl_shell_destroy(wayland->shell);

    if (wayland->xdg_shell)
        xdg_shell_destroy(wayland->xdg_shell);

    if (wayland->compositor)
        wl_compositor_destroy(wayland->compositor);

    if (wayland->registry)
        wl_registry_destroy(wayland->registry);
}

bool
bm_wl_registry_register(struct wayland *wayland)
{
    assert(wayland);

    if (!(wayland->registry = wl_display_get_registry(wayland->display)))
        return false;

    wl_registry_add_listener(wayland->registry, &registry_listener, wayland);
    wl_display_roundtrip(wayland->display); // trip 1, registry globals
    if (!wayland->compositor || !wayland->seat || !wayland->shm || !(wayland->shell || wayland->xdg_shell))
        return false;

    wl_display_roundtrip(wayland->display); // trip 2, global listeners
    if (!wayland->input.keyboard || !(wayland->formats & (1 << WL_SHM_FORMAT_ARGB8888)))
        return false;

    return true;
}

/* vim: set ts=8 sw=4 tw=0 :*/
