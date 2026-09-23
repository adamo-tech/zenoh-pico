#include <assert.h>
#include <string.h>
#include "zenoh-pico/protocol/codec/transport.h"
#include "zenoh-pico/protocol/iobuf.h"
static void check(const uint8_t *bytes, size_t len, int valid, const char *token) {
    _z_slice_t slice = _z_slice_alias_buf(bytes, len);
    _z_zbuf_t buffer = _z_slice_as_zbuf(slice);
    _z_t_msg_open_t msg = {0};
    z_result_t result = _z_open_decode(&msg, &buffer, 0xa2);
    assert((result == _Z_RES_OK) == valid);
    if (valid && token) {
        assert(msg._signalling_token.len == strlen(token));
        assert(memcmp(msg._signalling_token.start, token, strlen(token)) == 0);
        _z_t_msg_open_t copy = {0};
        _z_t_msg_copy_open(&copy, &msg);
        _z_t_msg_open_clear(&msg);
        assert(memcmp(copy._signalling_token.start, token, strlen(token)) == 0);
        _z_t_msg_open_clear(&copy);
    } else {
        if (!valid) assert(_z_slice_is_empty(&msg._signalling_token));
        _z_t_msg_open_clear(&msg);
    }
}
int main(void) {
    // Outgoing handshakes never carry the server-issued capability. Constructors
    // must initialize the new owning field before the generic clear path sees it.
    _z_transport_message_t syn = _z_t_msg_make_open_syn(1000, 0, _z_slice_null());
    _z_transport_message_t ack = _z_t_msg_make_open_ack(1000, 0);
    assert(_z_slice_is_empty(&syn._body._open._signalling_token));
    assert(_z_slice_is_empty(&ack._body._open._signalling_token));
    _z_t_msg_clear(&syn);
    _z_t_msg_clear(&ack);
    const uint8_t valid[] = {1,0,0x4e,3,'a','b','c'};
    const uint8_t empty[] = {1,0,0x4e,0};
    const uint8_t truncated[] = {1,0,0x4e,4,'a'};
    const uint8_t duplicate[] = {1,0,0xce,1,'a',0x4e,1,'b'};
    const uint8_t mandatory[] = {1,0,0x5e,1,'a'};
    const uint8_t unknown[] = {1,0,0x01};
    uint8_t oversized[4102] = {1,0,0x4e,0x81,0x20};
    memset(oversized + 5, 'a', 4097);
    check(oversized,sizeof(oversized),0,0);
    check(valid,sizeof(valid),1,"abc");
    check(empty,sizeof(empty),0,0);
    check(truncated,sizeof(truncated),0,0);
    check(duplicate,sizeof(duplicate),0,0);
    check(mandatory,sizeof(mandatory),0,0);
    check(unknown,sizeof(unknown),1,0);
    return 0;
}
