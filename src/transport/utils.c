//
// Copyright (c) 2022 ZettaScale Technology
//
// This program and the accompanying materials are made available under the
// terms of the Eclipse Public License 2.0 which is available at
// http://www.eclipse.org/legal/epl-2.0, or the Apache License, Version 2.0
// which is available at https://www.apache.org/licenses/LICENSE-2.0.
//
// SPDX-License-Identifier: EPL-2.0 OR Apache-2.0
//
// Contributors:
//   ZettaScale Zenoh Team, <zenoh@zettascale.tech>
//

#include "zenoh-pico/transport/utils.h"

#include <string.h>

#include "zenoh-pico/protocol/core.h"

#define U8_MAX 0xFF
#define U16_MAX 0xFFFF
#define U32_MAX 0xFFFFFFFF
#define U64_MAX 0xFFFFFFFFFFFFFFFF

_z_zint_t _z_sn_max(uint8_t bits) {
    _z_zint_t ret = 0;
    switch (bits) {
        case 0x00: {
            ret = U8_MAX >> 1;
        } break;

        case 0x01: {
            ret = U16_MAX >> 2;
        } break;

        case 0x02: {
            ret = U32_MAX >> 4;
        } break;

        case 0x03: {
            ret = (_z_zint_t)(U64_MAX >> 1);
        } break;

        default: {
            // Do nothing
        } break;
    }

    return ret;
}

_z_zint_t _z_sn_half(_z_zint_t sn) { return sn >> 1; }

_z_zint_t _z_sn_modulo_mask(uint8_t bits) {
    _z_zint_t ret = 0;
    switch (bits) {
        case 0x00: {
            ret = U8_MAX >> 1;
        } break;

        case 0x01: {
            ret = U16_MAX >> 2;
        } break;

        case 0x02: {
            ret = U32_MAX >> 4;
        } break;

        case 0x03: {
            ret = (_z_zint_t)(U64_MAX >> 1);
        } break;

        default: {
            // Do nothing
        } break;
    }

    return ret;
}

bool _z_sn_precedes(const _z_zint_t sn_resolution, const _z_zint_t sn_left, const _z_zint_t sn_right) {
    _z_zint_t distance = (sn_right - sn_left) & sn_resolution;
    return ((distance <= _z_sn_half(sn_resolution)) && (distance != 0));
}

bool _z_sn_consecutive(const _z_zint_t sn_resolution, const _z_zint_t sn_left, const _z_zint_t sn_right) {
    _z_zint_t distance = (sn_right - sn_left) & sn_resolution;
    return distance == 1;
}

_z_zint_t _z_sn_increment(const _z_zint_t sn_resolution, const _z_zint_t sn) {
    _z_zint_t ret = sn + 1;
    return (ret &= sn_resolution);
}

_z_zint_t _z_sn_decrement(const _z_zint_t sn_resolution, const _z_zint_t sn) {
    _z_zint_t ret = sn - 1;
    return (ret &= sn_resolution);
}

static bool _z_sn_window_contains(const _z_sn_window_t *window, size_t distance) {
    return (window->_seen[distance / _Z_SN_WINDOW_WORD_BITS] & ((uint64_t)1 << (distance % _Z_SN_WINDOW_WORD_BITS))) !=
           0;
}

static void _z_sn_window_mark(_z_sn_window_t *window, size_t distance) {
    window->_seen[distance / _Z_SN_WINDOW_WORD_BITS] |= (uint64_t)1 << (distance % _Z_SN_WINDOW_WORD_BITS);
}

static void _z_sn_window_advance(_z_sn_window_t *window, size_t distance) {
    if (distance >= Z_BEST_EFFORT_REORDER_WINDOW) {
        memset(window->_seen, 0, sizeof(window->_seen));
        return;
    }

    const size_t word_shift = distance / _Z_SN_WINDOW_WORD_BITS;
    const size_t bit_shift = distance % _Z_SN_WINDOW_WORD_BITS;
    for (size_t dst = _Z_SN_WINDOW_WORDS; dst-- > 0;) {
        uint64_t value = 0;
        if (dst >= word_shift) {
            const size_t src = dst - word_shift;
            value = window->_seen[src] << bit_shift;
            if ((bit_shift != 0) && (src != 0)) {
                value |= window->_seen[src - 1] >> (_Z_SN_WINDOW_WORD_BITS - bit_shift);
            }
        }
        window->_seen[dst] = value;
    }

    const size_t excess = (_Z_SN_WINDOW_WORDS * _Z_SN_WINDOW_WORD_BITS) - Z_BEST_EFFORT_REORDER_WINDOW;
    if (excess != 0) {
        window->_seen[_Z_SN_WINDOW_WORDS - 1] &= UINT64_MAX >> excess;
    }
}

static size_t _z_sn_window_capacity(_z_zint_t sn_resolution) {
    const _z_zint_t unambiguous_capacity = _z_sn_half(sn_resolution) + 1;
    return unambiguous_capacity < (_z_zint_t)Z_BEST_EFFORT_REORDER_WINDOW ? (size_t)unambiguous_capacity
                                                                          : Z_BEST_EFFORT_REORDER_WINDOW;
}

void _z_sn_window_reset(_z_sn_window_t *window, _z_zint_t high_water) {
    window->_high_water = high_water;
    memset(window->_seen, 0, sizeof(window->_seen));
}

_z_sn_window_result_t _z_sn_window_observe(_z_sn_window_t *window, _z_zint_t sn_resolution, _z_zint_t value) {
    if (_z_sn_precedes(sn_resolution, window->_high_water, value)) {
        const _z_zint_t distance = (value - window->_high_water) & sn_resolution;
        _z_sn_window_advance(window, (size_t)distance);
        window->_high_water = value;
        _z_sn_window_mark(window, 0);
        return _Z_SN_WINDOW_AHEAD;
    }

    const _z_zint_t distance = (window->_high_water - value) & sn_resolution;
    if (distance >= (_z_zint_t)_z_sn_window_capacity(sn_resolution)) {
        return _Z_SN_WINDOW_TOO_OLD;
    }
    if (_z_sn_window_contains(window, (size_t)distance)) {
        return _Z_SN_WINDOW_DUPLICATE;
    }
    _z_sn_window_mark(window, (size_t)distance);
    return _Z_SN_WINDOW_REORDERED;
}

void _z_conduit_sn_list_copy(_z_conduit_sn_list_t *dst, const _z_conduit_sn_list_t *src) {
    dst->_is_qos = src->_is_qos;
    if (dst->_is_qos == false) {
        dst->_val._plain._best_effort = src->_val._plain._best_effort;
        dst->_val._plain._reliable = src->_val._plain._reliable;
    } else {
        for (uint8_t i = 0; i < Z_PRIORITIES_NUM; i++) {
            dst->_val._qos[i]._best_effort = src->_val._qos[i]._best_effort;
            dst->_val._qos[i]._reliable = src->_val._qos[i]._reliable;
        }
    }
}

void _z_conduit_sn_list_decrement(const _z_zint_t sn_resolution, _z_conduit_sn_list_t *sns) {
    if (sns->_is_qos == false) {
        sns->_val._plain._best_effort = _z_sn_decrement(sn_resolution, sns->_val._plain._best_effort);
        sns->_val._plain._reliable = _z_sn_decrement(sn_resolution, sns->_val._plain._reliable);
    } else {
        for (uint8_t i = 0; i < Z_PRIORITIES_NUM; i++) {
            sns->_val._qos[i]._best_effort = _z_sn_decrement(sn_resolution, sns->_val._qos[i]._best_effort);
            sns->_val._qos[i]._best_effort = _z_sn_decrement(sn_resolution, sns->_val._qos[i]._reliable);
        }
    }
}
