// SPDX-License-Identifier: EPL-2.0 OR Apache-2.0

#include <string.h>

#include "zenoh-pico/protocol/codec/transport.h"
#include "zenoh-pico/transport/common/tx.h"
#include "zenoh-pico/transport/unicast/rx.h"

#undef NDEBUG
#include <assert.h>

// A Frame has no payload length: the next transport header ends its list of
// network messages. Exercise the same decode/dispatch loop as unicast read.
static void test_frame_boundaries(void) {
    uint8_t bytes[] = {
        0x25, 1, 0x1f, 0, 0x1f, 0,  // reliable Frame, SN 1, two unit OAMs
        0x05, 1, 0x1f, 0,           // best-effort Frame, SN 1, one unit OAM
        0x04,                       // KeepAlive
        0x25, 2, 0x1f, 0,           // reliable Frame, SN 2
    };
    _z_zbuf_t buffer = _z_zbuf_make(sizeof(bytes));
    memcpy(_z_zbuf_get_wptr(&buffer), bytes, sizeof(bytes));
    _z_zbuf_set_wpos(&buffer, sizeof(bytes));
    _z_link_t link = {0};
    link._cap._flow = Z_LINK_CAP_FLOW_STREAM;
    _z_transport_unicast_t transport = {0};
    transport._common._link = &link;
    transport._common._sn_res = 0xffff;
    _z_transport_peer_unicast_t peer = {0};
    const size_t boundaries[] = {6, 10, 11, 15};
    for (size_t i = 0; i < sizeof(boundaries) / sizeof(boundaries[0]); ++i) {
        _z_transport_message_t message = {0};
        assert(_z_transport_message_decode(&message, &buffer) == _Z_RES_OK);
        assert(_z_unicast_handle_transport_message(&transport, &message, &peer) == _Z_RES_OK);
        assert(_z_zbuf_get_rpos(&buffer) == boundaries[i]);
    }
    assert(_z_zbuf_len(&buffer) == 0);
    assert(peer._sn_rx_reliable == 2);
    assert(peer._sn_rx_best_effort == 1);
    _z_zbuf_clear(&buffer);
}

static void test_invalid_network_message(uint8_t header) {
    // An unknown network ID or a truncated OAM must still fail, rather than
    // being accepted as a frame boundary.
    uint8_t bytes[] = {0x25, 1, 0x1f, 0, header};
    _z_zbuf_t buffer = _z_zbuf_make(sizeof(bytes));
    memcpy(_z_zbuf_get_wptr(&buffer), bytes, sizeof(bytes));
    _z_zbuf_set_wpos(&buffer, sizeof(bytes));
    _z_transport_unicast_t transport = {0};
    transport._common._sn_res = 0xffff;
    _z_transport_peer_unicast_t peer = {0};
    _z_transport_message_t message = {0};
    assert(_z_transport_message_decode(&message, &buffer) == _Z_RES_OK);
    assert(_z_unicast_handle_transport_message(&transport, &message, &peer) != _Z_RES_OK);
    _z_zbuf_clear(&buffer);
}

static void test_fragment_reliability(z_reliability_t reliability) {
    _z_wbuf_t payload = _z_wbuf_make(16, false);
    _z_wbuf_write(&payload, 0x1f);
    _z_wbuf_t fragment = _z_wbuf_make(16, false);
    assert(__unsafe_z_serialize_zenoh_fragment(&fragment, &payload, reliability, 1, true) == _Z_RES_OK);
    uint8_t header = _z_wbuf_get_iosli(&fragment, 0)->_buf[0];
    assert(_Z_HAS_FLAG(header, _Z_FLAG_T_FRAGMENT_R) == (reliability == Z_RELIABILITY_RELIABLE));
    _z_wbuf_clear(&fragment);
    _z_wbuf_clear(&payload);
}

int main(void) {
    test_frame_boundaries();
    test_fragment_reliability(Z_RELIABILITY_RELIABLE);
    test_fragment_reliability(Z_RELIABILITY_BEST_EFFORT);
    test_invalid_network_message(0x18);
    test_invalid_network_message(0x1f);
    return 0;
}
