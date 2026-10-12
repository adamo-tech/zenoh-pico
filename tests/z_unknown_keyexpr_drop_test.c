//
// Copyright (c) 2026 ZettaScale Technology
//
// This program and the accompanying materials are made available under the
// terms of the Eclipse Public License 2.0 which is available at
// http://www.eclipse.org/legal/epl-2.0, or the Apache License, Version 2.0
// which is available at https://www.apache.org/licenses/LICENSE-2.0.
//
// SPDX-License-Identifier: EPL-2.0 OR Apache-2.0
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zenoh-pico/net/session.h"
#include "zenoh-pico/protocol/codec/network.h"
#include "zenoh-pico/protocol/codec/transport.h"
#include "zenoh-pico/protocol/definitions/network.h"
#include "zenoh-pico/protocol/iobuf.h"
#include "zenoh-pico/session/resource.h"
#include "zenoh-pico/session/utils.h"
#include "zenoh-pico/transport/unicast/rx.h"

#undef NDEBUG
#include <assert.h>

#if Z_FEATURE_UNICAST_TRANSPORT == 1 && Z_FEATURE_SUBSCRIPTION == 1

// A key-expression id the receiving session never declared (or already undeclared).
#define UNKNOWN_RID 7

static _z_session_t *make_session(void) {
    _z_session_t *s = (_z_session_t *)malloc(sizeof(_z_session_t));
    assert(s != NULL);
    memset(s, 0, sizeof(_z_session_t));
    _z_id_t zid = {0};
    zid.id[0] = 1;
    assert(_z_session_init(s, &zid) == _Z_RES_OK);
    return s;
}

// Wrap an encoded network message into a reliable Frame (SN 1) and hand it to the unicast rx path.
static z_result_t deliver_in_frame(_z_transport_unicast_t *transport, _z_transport_peer_unicast_t *peer,
                                   const _z_network_message_t *nmsg) {
    _z_wbuf_t wbf = _z_wbuf_make(1024, false);
    assert(_z_network_message_encode(&wbf, nmsg) == _Z_RES_OK);
    _z_zbuf_t encoded = _z_wbuf_to_zbuf(&wbf);
    size_t len = _z_zbuf_len(&encoded);
    assert(len > 0 && len < 1000);

    _z_zbuf_t buffer = _z_zbuf_make(len + 2);
    uint8_t *w = _z_zbuf_get_wptr(&buffer);
    w[0] = 0x25;  // Frame | R (reliable)
    w[1] = 1;     // SN
    _z_zbuf_read_bytes(&encoded, w + 2, 0, len);
    _z_zbuf_set_wpos(&buffer, len + 2);

    _z_transport_message_t message = {0};
    assert(_z_transport_message_decode(&message, &buffer) == _Z_RES_OK);
    z_result_t ret = _z_unicast_handle_transport_message(transport, &message, peer);
    // Whatever the outcome, the frame must have been consumed up to the batch end.
    assert(_z_zbuf_len(&buffer) == 0);

    _z_zbuf_clear(&buffer);
    _z_zbuf_clear(&encoded);
    _z_wbuf_clear(&wbf);
    return ret;
}

static void test_unknown_keyexpr_is_dropped_not_fatal(void) {
    _z_session_t *s = make_session();
    _z_session_rc_t rc = _z_session_rc_new(s);
    assert(rc._cnt != NULL);

    _z_link_t link = {0};
    link._cap._flow = Z_LINK_CAP_FLOW_STREAM;
    _z_transport_unicast_t transport = {0};
    transport._common._link = &link;
    transport._common._sn_res = 0xffff;
    transport._common._session = _z_session_rc_clone_as_weak(&rc);
    _z_transport_peer_unicast_t peer = {0};

    _z_wireexpr_t key = _z_wireexpr_null();
    key._id = UNKNOWN_RID;
    key._mapping = _Z_KEYEXPR_MAPPING_LOCAL;

    // Sanity: the id really is unresolvable on this session.
    _z_keyexpr_t resolved;
    assert(_z_get_keyexpr_from_wireexpr(s, &resolved, &key, &peer.common, true) == _Z_ERR_KEYEXPR_UNKNOWN);

    // 1. A Push (the in-flight video frame after a subscriber was closed).
    _z_bytes_t payload;
    assert(_z_bytes_from_buf(&payload, (const uint8_t *)"frame", 5) == _Z_RES_OK);
    _z_encoding_t encoding = _z_encoding_null();
    _z_timestamp_t timestamp = _z_timestamp_null();
    _z_bytes_t attachment = _z_bytes_null();
    _z_source_info_t source_info = _z_source_info_null();
    _z_network_message_t push;
    _z_n_msg_make_push_put(&push, &key, &payload, &encoding, _Z_N_QOS_DEFAULT, &timestamp, &attachment,
                           Z_RELIABILITY_RELIABLE, &source_info);
    assert(deliver_in_frame(&transport, &peer, &push) == _Z_RES_OK);
    _z_n_msg_clear(&push);
    assert(peer._sn_rx_reliable == 1);

    // 2. A DeclareSubscriber from the router whose key uses the unknown id.
    _z_declaration_t declaration = _z_make_decl_subscriber(&key, 42);
    _z_network_message_t declare;
    _z_n_msg_make_declare(&declare, declaration, _z_optional_id_make_none());
    peer._sn_rx_reliable = 0;  // frame SN is 1 again
    assert(deliver_in_frame(&transport, &peer, &declare) == _Z_RES_OK);
    _z_n_msg_clear(&declare);

    _z_session_weak_drop(&transport._common._session);
    printf("unknown key-expression ids are dropped without closing the session\n");
}

int main(void) {
    test_unknown_keyexpr_is_dropped_not_fatal();
    return 0;
}
#else
int main(void) {
    printf("Skipping: requires unicast transport and subscriptions\n");
    return 0;
}
#endif
