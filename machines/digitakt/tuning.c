/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "digitakt-mk1/core3.h"
#include "controls.h"

extern int32_t modeldigi_view_value(void *view, int32_t id, uint8_t *locked);
extern void modeldigi_native_encoder(void *brain, const int32_t *event);
extern int32_t modeldigi_parameter_set(void *parameter, int32_t id, int32_t value,
                                     int32_t, int32_t, int32_t, int32_t);

extern volatile const uint8_t modeldigi_key_state[7];

static void *controller, *owner;
static int32_t last_value;
static int last_id, last_wide, last_function, direction, hold, coarse;

static void *pointer_at(const void *object, int offset)
{
    return *(void *const *)((const uint8_t *)object + offset);
}

static int named(const void *view, const char *expected)
{
    void *const *vt = pointer_at(view, 0);
    const char *name = ((const char *const *)vt[-1])[1];
    while (*expected && *name == *expected) name++, expected++;
    return !*expected && !*name;
}

void modeldigi_tuning_tick(void *ctrl)
{
    controller = ctrl;
}

int modeldigi_tuning_key(void *brain, const int32_t *event)
{
    (void)brain;
    if (event[3] != 1) owner = 0;
    return 0;
}

static void *source_view(void)
{
    if (!controller || fw_active_track >= 8) return 0;
    void *view = 0, *end = (uint8_t *)controller + 20;
    void *node = pointer_at(controller, 28);
    /* A later modal view owns encoder input. */
    for (int count = 0; node != end && count < 32; count++, node = pointer_at(node, 0)) {
        void *candidate = pointer_at(node, 8);
        if (!((const uint8_t *)candidate)[21]) continue;
        if (named(candidate, "14SamplePageView")) view = candidate;
        else if (view && !named(candidate, "14SamplerLedView") && !named(candidate, "12TempoLedView") &&
                 !named(candidate, "15PatternGridView") && !named(candidate, "12KeyboardView") &&
                 !named(candidate, "14HelpBubbleView") &&
                 !((modeldigi_key_state[2] & 2) && named(candidate, "17QuickMuteMenuView"))) return 0;
    }
    return node == end ? view : 0;
}

int modeldigi_tuning_encoder(void *brain, const int32_t *event)
{
    /* Pads/TRK may target locks or several tracks; preserve that native path. */
    if (modeldigi_key_state[0] || modeldigi_key_state[1] || (modeldigi_key_state[2] & 1)) {
        owner = 0;
        return 0;
    }
    int knob = event[3] - 1;
    if ((knob != 3 && knob != 6) || !event[4]) { owner = 0; return 0; }
    void *view = source_view();
    if (!view) { owner = 0; return 0; }
    uintptr_t *vt = pointer_at(view, 0);
    int id = ((int32_t (*)(void *, int32_t))vt[39])(view, knob);
    if (id != 132 + knob) { owner = 0; return 0; }
    void *parameter = (void *)((uintptr_t (*)(void *, int32_t, int32_t))vt[41])(view, id, -1);
    void *reference = parameter ? pointer_at(parameter, 16) : 0;
    uint8_t *sound = reference ? pointer_at(reference, 16) : 0;
    uint8_t locked;
    if (!sound || sound[FW_SOUND_MACHINE] != 42) { owner = 0; return 0; }
    int32_t before = modeldigi_view_value(view, id, &locked);
    /* Held-step edits keep the native lock path. */
    if (locked) { owner = 0; return 0; }
    int wide = knob == 6 && (int16_t)(sound[114] << 8 | sound[115]) < (64 << 8);
    int sign = event[4] > 0 ? 1 : -1;
    int amount = event[4] * sign;
    int function = (modeldigi_key_state[2] & 2) != 0;
    if (owner != sound || last_id != id || last_wide != wide || last_value != before || direction != sign || last_function != function) {
        hold = modeldigi_frq_anchor(before, wide) ? 12 : 0;
        coarse = 0;
    }
    owner = sound; last_id = id; last_wide = wide; direction = sign; last_function = function;
    int32_t after = before;
    if (function) {
        /* 128 detents over the full fourteen- or seventy-semitone span. */
        int steps = modeldigi_frq_coarse(&coarse, amount, wide);
        if (steps > 70) steps = 70;
        while (steps--) after = modeldigi_frq_step(after, sign, wide);
        hold = modeldigi_frq_anchor(after, wide) ? 12 : 0;
    } else {
        if (amount <= hold) {
            hold -= amount;
        } else {
            int32_t adjusted[7];
            for (int i = 0; i < 7; i++) adjusted[i] = event[i];
            adjusted[4] = sign * (amount - hold);
            hold = 0;
            modeldigi_native_encoder(brain, adjusted);
            after = modeldigi_view_value(view, id, &locked);
            int32_t anchor = modeldigi_frq_step(before, sign, wide);
            if ((sign > 0 && before < anchor && after >= anchor) ||
                (sign < 0 && before > anchor && after <= anchor)) {
                after = anchor;
                hold = 12;
            }
        }
    }
    if (function || after != modeldigi_view_value(view, id, &locked))
        modeldigi_parameter_set(parameter, id, after, 0, 0, 0, 0);
    last_value = after;
    return 1;
}
