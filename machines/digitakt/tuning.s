| SPDX-License-Identifier: GPL-2.0-or-later
| Native UI paths in Digitakt OS 1.54.
        .section .run, "ax"
        .globl modeldigi_view_value, modeldigi_native_encoder, modeldigi_parameter_set
modeldigi_view_value:
        jmp 0x4003171c
modeldigi_native_encoder:
        jmp 0x40008550
modeldigi_parameter_set:
        jmp 0x4000fef6

        .globl modeldigi_key_state
        .set modeldigi_key_state, MODELDIGI_KEY_STATE
