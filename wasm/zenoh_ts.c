#include <emscripten/emscripten.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zenoh-pico.h>

typedef struct zt_session_entry {
    uint32_t id;
    z_owned_session_t value;
    struct zt_session_entry *next;
} zt_session_entry_t;

typedef struct zt_subscriber_entry {
    uint32_t id;
    uint32_t session_id;
    z_owned_subscriber_t value;
    struct zt_subscriber_entry *next;
} zt_subscriber_entry_t;

typedef struct zt_publisher_entry {
    uint32_t id;
    uint32_t session_id;
    z_owned_publisher_t value;
    struct zt_publisher_entry *next;
} zt_publisher_entry_t;

typedef struct zt_liveliness_token_entry {
    uint32_t id;
    uint32_t session_id;
    z_owned_liveliness_token_t value;
    struct zt_liveliness_token_entry *next;
} zt_liveliness_token_entry_t;

typedef struct zt_matching_listener_entry {
    uint32_t id;
    uint32_t session_id;
    uint32_t publisher_id;
    z_owned_matching_listener_t value;
    struct zt_matching_listener_entry *next;
} zt_matching_listener_entry_t;

typedef struct zt_query_entry {
    uint32_t id;
    uint32_t session_id;
    struct zt_query_entry *next;
} zt_query_entry_t;

typedef struct zt_queryable_entry {
    uint32_t id;
    uint32_t session_id;
    z_owned_queryable_t value;
    struct zt_queryable_entry *next;
} zt_queryable_entry_t;

typedef struct zt_incoming_query_entry {
    uint32_t id;
    uint32_t session_id;
    uint32_t queryable_id;
    z_owned_query_t value;
    struct zt_incoming_query_entry *next;
} zt_incoming_query_entry_t;

static zt_session_entry_t *zt_sessions;
static zt_subscriber_entry_t *zt_subscribers;
static zt_publisher_entry_t *zt_publishers;
static zt_liveliness_token_entry_t *zt_liveliness_tokens;
static zt_matching_listener_entry_t *zt_matching_listeners;
static zt_query_entry_t *zt_queries;
static zt_queryable_entry_t *zt_queryables;
static zt_incoming_query_entry_t *zt_incoming_queries;
static uint32_t zt_next_handle = 1;

EMSCRIPTEN_KEEPALIVE
uint32_t zt_abi_version(void) { return 7; }

EM_JS(void, zt_set_certificate_hash, (const char *hash), {
    Module.zenohPicoServerCertificateHash = hash ? UTF8ToString(hash) : undefined;
});

EM_JS(void, zt_begin_sample,
      (uint32_t subscriber_id, const char *key, size_t key_len, size_t payload_len, int kind,
       int priority, int congestion_control, int reliability, int is_express,
       int has_timestamp, uint32_t timestamp_hi, uint32_t timestamp_lo,
       const char *timestamp_id, size_t timestamp_id_len), {
    if (!Module.zenohPicoPendingSamples) Module.zenohPicoPendingSamples = new Map();
    Module.zenohPicoPendingSamples.set(subscriber_id, {
        keyExpr: UTF8ToString(key, key_len), payload: new Uint8Array(payload_len), offset: 0, kind,
        priority, congestionControl: congestion_control, reliability, express: Boolean(is_express),
        timestamp: has_timestamp ? {
            id: UTF8ToString(timestamp_id, timestamp_id_len),
            ntp64High: timestamp_hi >>> 0,
            ntp64Low: timestamp_lo >>> 0,
        } : undefined,
    });
});

EM_JS(void, zt_set_session_zid, (const char *zid, size_t zid_len), {
    Module.zenohPicoLastSessionZid = UTF8ToString(zid, zid_len);
});

EM_JS(void, zt_report_matching_status, (uint32_t listener_id, int matching), {
    if (Module.onZenohMatchingStatus) Module.onZenohMatchingStatus(listener_id, Boolean(matching));
});

EM_JS(void, zt_report_query_done, (uint32_t query_id), {
    if (Module.onZenohQueryDone) Module.onZenohQueryDone(query_id);
});

EM_JS(void, zt_begin_query_error, (uint32_t query_id, size_t payload_len), {
    if (!Module.zenohPicoPendingQueryErrors) Module.zenohPicoPendingQueryErrors = new Map();
    Module.zenohPicoPendingQueryErrors.set(query_id,
        {payload: new Uint8Array(payload_len), offset: 0});
});

EM_JS(void, zt_append_query_error, (uint32_t query_id, const uint8_t *payload, size_t payload_len), {
    const pending = Module.zenohPicoPendingQueryErrors?.get(query_id);
    if (!pending) return;
    pending.payload.set(HEAPU8.subarray(payload, payload + payload_len), pending.offset);
    pending.offset += payload_len;
});

EM_JS(void, zt_finish_query_error, (uint32_t query_id), {
    const pending = Module.zenohPicoPendingQueryErrors?.get(query_id);
    if (!pending) return;
    Module.zenohPicoPendingQueryErrors.delete(query_id);
    if (pending.offset === pending.payload.length && Module.onZenohQueryError) {
        Module.onZenohQueryError(query_id, pending.payload);
    }
});

EM_JS(void, zt_begin_incoming_query,
      (uint32_t queryable_id, uint32_t query_id, const char *key, size_t key_len,
       const char *parameters, size_t parameters_len, size_t payload_len), {
    if (!Module.zenohPicoPendingIncomingQueries) Module.zenohPicoPendingIncomingQueries = new Map();
    Module.zenohPicoPendingIncomingQueries.set(query_id, {
        queryableId: queryable_id,
        keyExpr: UTF8ToString(key, key_len),
        parameters: UTF8ToString(parameters, parameters_len),
        payload: new Uint8Array(payload_len),
        offset: 0,
    });
});

EM_JS(void, zt_append_incoming_query, (uint32_t query_id, const uint8_t *payload, size_t payload_len), {
    const pending = Module.zenohPicoPendingIncomingQueries?.get(query_id);
    if (!pending) return;
    pending.payload.set(HEAPU8.subarray(payload, payload + payload_len), pending.offset);
    pending.offset += payload_len;
});

EM_JS(void, zt_finish_incoming_query, (uint32_t query_id), {
    const pending = Module.zenohPicoPendingIncomingQueries?.get(query_id);
    if (!pending) return;
    Module.zenohPicoPendingIncomingQueries.delete(query_id);
    if (pending.offset === pending.payload.length && Module.onZenohIncomingQuery) {
        Module.onZenohIncomingQuery(pending.queryableId, query_id, pending.keyExpr,
                                    pending.parameters, pending.payload);
    }
});

EM_JS(void, zt_append_sample, (uint32_t subscriber_id, const uint8_t *payload, size_t payload_len), {
    const pending = Module.zenohPicoPendingSamples?.get(subscriber_id);
    if (!pending) return;
    pending.payload.set(HEAPU8.subarray(payload, payload + payload_len), pending.offset);
    pending.offset += payload_len;
});

EM_JS(void, zt_finish_sample, (uint32_t subscriber_id), {
    const pending = Module.zenohPicoPendingSamples?.get(subscriber_id);
    if (!pending) return;
    Module.zenohPicoPendingSamples.delete(subscriber_id);
    if (pending.offset !== pending.payload.length) {
        if (Module.onZenohDiagnostic) Module.onZenohDiagnostic({type: 'sample-error', subscriberId: subscriber_id,
            stage: 'payload-iteration', payloadLength: pending.payload.length, copiedLength: pending.offset});
        return;
    }
    if (Module.onZenohSample) {
        Module.onZenohSample(subscriber_id, pending.keyExpr, pending.payload, pending.kind, {
            priority: pending.priority,
            congestionControl: pending.congestionControl,
            reliability: pending.reliability,
            express: pending.express,
            timestamp: pending.timestamp,
        });
    }
});

EM_JS(void, zt_report_sample_error, (uint32_t subscriber_id, int stage, size_t payload_len, int result), {
    if (Module.onZenohDiagnostic) {
        Module.onZenohDiagnostic({type: 'sample-error', subscriberId: subscriber_id,
            stage: stage === 1 ? 'key-expression' : 'payload-copy', payloadLength: payload_len, result});
    }
});

static uint32_t zt_allocate_handle(void) {
    uint32_t handle = zt_next_handle++;
    if (zt_next_handle == 0) zt_next_handle = 1;
    return handle;
}

static int zt_hex_nibble(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static int zt_make_timestamp(z_timestamp_t *timestamp, int has_timestamp, const char *id,
                             uint32_t high, uint32_t low) {
    if (!has_timestamp) return 0;
    if (id == NULL || strlen(id) != ZENOH_ID_SIZE * 2) return -1;
    *timestamp = (_z_timestamp_t){0};
    for (size_t i = 0; i < ZENOH_ID_SIZE; i++) {
        size_t offset = (ZENOH_ID_SIZE - 1 - i) * 2;
        int hi = zt_hex_nibble(id[offset]);
        int lo = zt_hex_nibble(id[offset + 1]);
        if (hi < 0 || lo < 0) return -1;
        timestamp->id.id[i] = (uint8_t)((hi << 4) | lo);
    }
    timestamp->time = ((uint64_t)high << 32) | low;
    timestamp->valid = true;
    return 0;
}

static zt_session_entry_t *zt_find_session(uint32_t id) {
    for (zt_session_entry_t *entry = zt_sessions; entry != NULL; entry = entry->next) {
        if (entry->id == id) return entry;
    }
    return NULL;
}

static zt_subscriber_entry_t *zt_find_subscriber(uint32_t id) {
    for (zt_subscriber_entry_t *entry = zt_subscribers; entry != NULL; entry = entry->next) {
        if (entry->id == id) return entry;
    }
    return NULL;
}

static zt_publisher_entry_t *zt_find_publisher(uint32_t id) {
    for (zt_publisher_entry_t *entry = zt_publishers; entry != NULL; entry = entry->next) {
        if (entry->id == id) return entry;
    }
    return NULL;
}

static zt_incoming_query_entry_t *zt_find_incoming_query(uint32_t id) {
    for (zt_incoming_query_entry_t *entry = zt_incoming_queries; entry != NULL; entry = entry->next) {
        if (entry->id == id) return entry;
    }
    return NULL;
}

static void zt_drop_incoming_query(zt_incoming_query_entry_t *entry) {
    zt_incoming_query_entry_t **cursor = &zt_incoming_queries;
    while (*cursor != NULL && *cursor != entry) cursor = &(*cursor)->next;
    if (*cursor == entry) *cursor = entry->next;
    z_drop(z_move(entry->value));
    z_free(entry);
}

static void zt_append_bytes_to_query_error(uint32_t id, const z_loaned_bytes_t *payload) {
    zt_begin_query_error(id, z_bytes_len(payload));
    z_bytes_slice_iterator_t iterator = z_bytes_get_slice_iterator(payload);
    z_view_slice_t slice;
    while (z_bytes_slice_iterator_next(&iterator, &slice)) {
        zt_append_query_error(id, z_slice_data(z_loan(slice)), z_slice_len(z_loan(slice)));
    }
    zt_finish_query_error(id);
}

static void zt_emit_sample(uint32_t id, z_loaned_sample_t *sample) {
    z_view_string_t key;
    int result = z_keyexpr_as_view_string(z_sample_keyexpr(sample), &key);
    if (result < 0) {
        zt_report_sample_error(id, 1, z_bytes_len(z_sample_payload(sample)), result);
        return;
    }
    const z_loaned_bytes_t *payload = z_sample_payload(sample);
    int reliability = Z_RELIABILITY_RELIABLE;
#ifdef Z_FEATURE_UNSTABLE_API
    reliability = (int)z_sample_reliability(sample);
#endif
    const z_timestamp_t *timestamp = z_sample_timestamp(sample);
    z_owned_string_t timestamp_id_string;
    const char *timestamp_id = NULL;
    size_t timestamp_id_len = 0;
    uint64_t ntp64 = 0;
    if (timestamp != NULL) {
        z_id_t timestamp_zid = z_timestamp_id(timestamp);
        if (z_id_to_string(&timestamp_zid, &timestamp_id_string) < 0) {
            zt_report_sample_error(id, 3, z_bytes_len(payload), -1);
            return;
        }
        timestamp_id = z_string_data(z_loan(timestamp_id_string));
        timestamp_id_len = z_string_len(z_loan(timestamp_id_string));
        ntp64 = z_timestamp_ntp64_time(timestamp);
    }
    zt_begin_sample(id, z_string_data(z_loan(key)), z_string_len(z_loan(key)), z_bytes_len(payload),
                    (int)z_sample_kind(sample), (int)z_sample_priority(sample),
                    (int)z_sample_congestion_control(sample), reliability, (int)z_sample_express(sample),
                    timestamp != NULL, (uint32_t)(ntp64 >> 32), (uint32_t)ntp64,
                    timestamp_id, timestamp_id_len);
    if (timestamp != NULL) z_drop(z_move(timestamp_id_string));
    z_bytes_slice_iterator_t iterator = z_bytes_get_slice_iterator(payload);
    z_view_slice_t slice;
    while (z_bytes_slice_iterator_next(&iterator, &slice)) {
        zt_append_sample(id, z_slice_data(z_loan(slice)), z_slice_len(z_loan(slice)));
    }
    zt_finish_sample(id);
}

static void zt_sample_handler(z_loaned_sample_t *sample, void *context) {
    zt_subscriber_entry_t *entry = (zt_subscriber_entry_t *)context;
    zt_emit_sample(entry->id, sample);
}

static void zt_matching_status_handler(const z_matching_status_t *status, void *context) {
    zt_matching_listener_entry_t *entry = (zt_matching_listener_entry_t *)context;
    zt_report_matching_status(entry->id, status->matching);
}

static void zt_query_reply_handler(z_loaned_reply_t *reply, void *context) {
    zt_query_entry_t *entry = (zt_query_entry_t *)context;
    if (z_reply_is_ok(reply)) {
        zt_emit_sample(entry->id, (z_loaned_sample_t *)z_reply_ok(reply));
    } else {
        zt_append_bytes_to_query_error(entry->id, z_reply_err_payload(z_reply_err(reply)));
    }
}

static void zt_queryable_handler(z_loaned_query_t *query, void *context) {
    zt_queryable_entry_t *queryable = (zt_queryable_entry_t *)context;
    zt_incoming_query_entry_t *entry =
        (zt_incoming_query_entry_t *)z_malloc(sizeof(zt_incoming_query_entry_t));
    if (entry == NULL) return;
    entry->id = zt_allocate_handle();
    entry->session_id = queryable->session_id;
    entry->queryable_id = queryable->id;
    if (z_query_take_from_loaned(&entry->value, query) < 0) {
        z_free(entry);
        return;
    }
    entry->next = zt_incoming_queries;
    zt_incoming_queries = entry;

    z_view_string_t key;
    z_view_string_t parameters;
    if (z_keyexpr_as_view_string(z_query_keyexpr(z_loan(entry->value)), &key) < 0) {
        zt_drop_incoming_query(entry);
        return;
    }
    z_query_parameters(z_loan(entry->value), &parameters);
    const z_loaned_bytes_t *payload = z_query_payload(z_loan(entry->value));
    zt_begin_incoming_query(queryable->id, entry->id,
                            z_string_data(z_loan(key)), z_string_len(z_loan(key)),
                            z_string_data(z_loan(parameters)), z_string_len(z_loan(parameters)),
                            z_bytes_len(payload));
    z_bytes_slice_iterator_t iterator = z_bytes_get_slice_iterator(payload);
    z_view_slice_t slice;
    while (z_bytes_slice_iterator_next(&iterator, &slice)) {
        zt_append_incoming_query(entry->id, z_slice_data(z_loan(slice)), z_slice_len(z_loan(slice)));
    }
    zt_finish_incoming_query(entry->id);
}

static void zt_query_drop_handler(void *context) {
    zt_query_entry_t *entry = (zt_query_entry_t *)context;
    zt_query_entry_t **cursor = &zt_queries;
    while (*cursor != NULL && *cursor != entry) cursor = &(*cursor)->next;
    if (*cursor == entry) *cursor = entry->next;
    zt_report_query_done(entry->id);
    z_free(entry);
}

EMSCRIPTEN_KEEPALIVE
int32_t zt_session_open(const char *endpoint, const char *certificate_hash) {
    if (endpoint == NULL) return -1;
    zt_session_entry_t *entry = (zt_session_entry_t *)z_malloc(sizeof(zt_session_entry_t));
    if (entry == NULL) return -1;
    entry->id = zt_allocate_handle();
    entry->next = NULL;

    zt_set_certificate_hash(certificate_hash);
    z_owned_config_t config;
    z_config_default(&config);
    if (zp_config_insert(z_loan_mut(config), Z_CONFIG_MODE_KEY, "client") < 0 ||
        zp_config_insert(z_loan_mut(config), Z_CONFIG_CONNECT_KEY, endpoint) < 0) {
        z_drop(z_move(config));
        z_free(entry);
        return -1;
    }
    int result = z_open(&entry->value, z_move(config), NULL);
    if (result < 0) {
        z_free(entry);
        return result;
    }
    entry->next = zt_sessions;
    zt_sessions = entry;
    return (int32_t)entry->id;
}

EMSCRIPTEN_KEEPALIVE
int zt_session_poll(uint32_t session_id, uint32_t max_work) {
    zt_session_entry_t *entry = zt_find_session(session_id);
    if (entry == NULL) return -1;
    if (max_work == 0) max_work = 64;
    uint32_t work = 0;
    while (work < max_work && zp_spin_once(z_loan(entry->value))) work++;
    return (int)work;
}

EMSCRIPTEN_KEEPALIVE
int zt_session_put(uint32_t session_id, const char *keyexpr, int priority, int congestion_control,
                   int reliability, int is_express, int has_timestamp, const char *timestamp_id,
                   uint32_t timestamp_hi, uint32_t timestamp_lo, const uint8_t *data, size_t len) {
    zt_session_entry_t *entry = zt_find_session(session_id);
    if (entry == NULL || keyexpr == NULL || (data == NULL && len != 0)) return -1;
    z_view_keyexpr_t key;
    if (z_view_keyexpr_from_str(&key, keyexpr) < 0) return -2;
    z_timestamp_t timestamp;
    if (zt_make_timestamp(&timestamp, has_timestamp, timestamp_id, timestamp_hi, timestamp_lo) < 0) return -3;
    z_owned_bytes_t payload;
    int result = z_bytes_copy_from_buf(&payload, data, len);
    if (result < 0) return result;
    z_put_options_t options;
    z_put_options_default(&options);
    options.priority = (z_priority_t)priority;
    options.congestion_control = (z_congestion_control_t)congestion_control;
    options.is_express = is_express != 0;
    options.timestamp = has_timestamp ? &timestamp : NULL;
#ifdef Z_FEATURE_UNSTABLE_API
    options.reliability = (z_reliability_t)reliability;
#endif
    return z_put(z_loan(entry->value), z_loan(key), z_move(payload), &options);
}

EMSCRIPTEN_KEEPALIVE
int zt_session_delete(uint32_t session_id, const char *keyexpr, int priority, int congestion_control,
                      int reliability, int is_express, int has_timestamp, const char *timestamp_id,
                      uint32_t timestamp_hi, uint32_t timestamp_lo) {
    zt_session_entry_t *entry = zt_find_session(session_id);
    if (entry == NULL || keyexpr == NULL) return -1;
    z_view_keyexpr_t key;
    if (z_view_keyexpr_from_str(&key, keyexpr) < 0) return -2;
    z_delete_options_t options;
    z_delete_options_default(&options);
    options.priority = (z_priority_t)priority;
    options.congestion_control = (z_congestion_control_t)congestion_control;
    options.is_express = is_express != 0;
    z_timestamp_t timestamp;
    if (zt_make_timestamp(&timestamp, has_timestamp, timestamp_id, timestamp_hi, timestamp_lo) < 0) return -3;
    options.timestamp = has_timestamp ? &timestamp : NULL;
#ifdef Z_FEATURE_UNSTABLE_API
    options.reliability = (z_reliability_t)reliability;
#endif
    return z_delete(z_loan(entry->value), z_loan(key), &options);
}

EMSCRIPTEN_KEEPALIVE
int32_t zt_session_get(uint32_t session_id, const char *keyexpr, const char *parameters,
                       uint32_t timeout_ms, int priority, int congestion_control, int is_express,
                       int target, int consolidation, int accept_replies, int has_payload,
                       const uint8_t *data, size_t len) {
    zt_session_entry_t *session = zt_find_session(session_id);
    if (session == NULL || keyexpr == NULL || (has_payload && data == NULL && len != 0)) return -1;
    zt_query_entry_t *entry = (zt_query_entry_t *)z_malloc(sizeof(zt_query_entry_t));
    if (entry == NULL) return -1;
    entry->id = zt_allocate_handle();
    uint32_t query_id = entry->id;
    entry->session_id = session_id;
    entry->next = zt_queries;
    zt_queries = entry;

    z_view_keyexpr_t key;
    z_owned_closure_reply_t callback;
    if (z_view_keyexpr_from_str(&key, keyexpr) < 0) {
        zt_query_drop_handler(entry);
        return -2;
    }
    if (z_closure_reply(&callback, zt_query_reply_handler, zt_query_drop_handler, entry) < 0) {
        zt_query_drop_handler(entry);
        return -2;
    }
    z_get_options_t options;
    z_get_options_default(&options);
    options.timeout_ms = timeout_ms;
    options.priority = (z_priority_t)priority;
    options.congestion_control = (z_congestion_control_t)congestion_control;
    options.is_express = is_express != 0;
    options.target = (z_query_target_t)target;
    options.consolidation.mode = (z_consolidation_mode_t)consolidation;
    options.accept_replies = (z_reply_keyexpr_t)accept_replies;
    z_owned_bytes_t payload;
    if (has_payload) {
        int payload_result = z_bytes_copy_from_buf(&payload, data, len);
        if (payload_result < 0) {
            z_drop(z_move(callback));
            return payload_result;
        }
        options.payload = z_move(payload);
    }
    int result = z_get(z_loan(session->value), z_loan(key), parameters == NULL ? "" : parameters,
                       z_move(callback), &options);
    return result < 0 ? result : (int32_t)query_id;
}

EMSCRIPTEN_KEEPALIVE
int32_t zt_subscriber_declare(uint32_t session_id, const char *keyexpr) {
    zt_session_entry_t *session = zt_find_session(session_id);
    if (session == NULL || keyexpr == NULL) return -1;
    zt_subscriber_entry_t *entry = (zt_subscriber_entry_t *)z_malloc(sizeof(zt_subscriber_entry_t));
    if (entry == NULL) return -1;
    entry->id = zt_allocate_handle();
    entry->session_id = session_id;
    entry->next = NULL;

    z_view_keyexpr_t key;
    z_owned_closure_sample_t callback;
    if (z_view_keyexpr_from_str(&key, keyexpr) < 0 ||
        z_closure_sample(&callback, zt_sample_handler, NULL, entry) < 0) {
        z_free(entry);
        return -2;
    }
    int result = z_declare_subscriber(z_loan(session->value), &entry->value, z_loan(key), z_move(callback), NULL);
    if (result < 0) {
        z_free(entry);
        return result;
    }
    entry->next = zt_subscribers;
    zt_subscribers = entry;
    return (int32_t)entry->id;
}

EMSCRIPTEN_KEEPALIVE
int32_t zt_liveliness_subscriber_declare(uint32_t session_id, const char *keyexpr, int history) {
    zt_session_entry_t *session = zt_find_session(session_id);
    if (session == NULL || keyexpr == NULL) return -1;
    zt_subscriber_entry_t *entry = (zt_subscriber_entry_t *)z_malloc(sizeof(zt_subscriber_entry_t));
    if (entry == NULL) return -1;
    entry->id = zt_allocate_handle();
    entry->session_id = session_id;
    entry->next = NULL;

    z_view_keyexpr_t key;
    z_owned_closure_sample_t callback;
    if (z_view_keyexpr_from_str(&key, keyexpr) < 0 ||
        z_closure_sample(&callback, zt_sample_handler, NULL, entry) < 0) {
        z_free(entry);
        return -2;
    }
    z_liveliness_subscriber_options_t options;
    z_liveliness_subscriber_options_default(&options);
    options.history = history != 0;
    int result = z_liveliness_declare_subscriber(z_loan(session->value), &entry->value, z_loan(key),
                                                 z_move(callback), &options);
    if (result < 0) {
        z_free(entry);
        return result;
    }
    entry->next = zt_subscribers;
    zt_subscribers = entry;
    return (int32_t)entry->id;
}

EMSCRIPTEN_KEEPALIVE
int zt_subscriber_undeclare(uint32_t subscriber_id) {
    zt_subscriber_entry_t **cursor = &zt_subscribers;
    while (*cursor != NULL && (*cursor)->id != subscriber_id) cursor = &(*cursor)->next;
    if (*cursor == NULL) return -1;
    zt_subscriber_entry_t *entry = *cursor;
    *cursor = entry->next;
    z_drop(z_move(entry->value));
    z_free(entry);
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int32_t zt_publisher_declare(uint32_t session_id, const char *keyexpr, int priority,
                             int congestion_control, int reliability, int is_express) {
    zt_session_entry_t *session = zt_find_session(session_id);
    if (session == NULL || keyexpr == NULL) return -1;
    zt_publisher_entry_t *entry = (zt_publisher_entry_t *)z_malloc(sizeof(zt_publisher_entry_t));
    if (entry == NULL) return -1;
    entry->id = zt_allocate_handle();
    entry->session_id = session_id;
    entry->next = NULL;
    z_view_keyexpr_t key;
    if (z_view_keyexpr_from_str(&key, keyexpr) < 0) {
        z_free(entry);
        return -2;
    }
    z_publisher_options_t options;
    z_publisher_options_default(&options);
    options.priority = (z_priority_t)priority;
    options.congestion_control = (z_congestion_control_t)congestion_control;
    options.is_express = is_express != 0;
#ifdef Z_FEATURE_UNSTABLE_API
    options.reliability = (z_reliability_t)reliability;
#endif
    int result = z_declare_publisher(z_loan(session->value), &entry->value, z_loan(key), &options);
    if (result < 0) {
        z_free(entry);
        return result;
    }
    entry->next = zt_publishers;
    zt_publishers = entry;
    return (int32_t)entry->id;
}

EMSCRIPTEN_KEEPALIVE
int zt_publisher_put(uint32_t publisher_id, int has_timestamp, const char *timestamp_id,
                     uint32_t timestamp_hi, uint32_t timestamp_lo, const uint8_t *data, size_t len) {
    zt_publisher_entry_t *entry = zt_find_publisher(publisher_id);
    if (entry == NULL || (data == NULL && len != 0)) return -1;
    z_timestamp_t timestamp;
    if (zt_make_timestamp(&timestamp, has_timestamp, timestamp_id, timestamp_hi, timestamp_lo) < 0) return -3;
    z_owned_bytes_t payload;
    int result = z_bytes_copy_from_buf(&payload, data, len);
    if (result < 0) return result;
    z_publisher_put_options_t options;
    z_publisher_put_options_default(&options);
    options.timestamp = has_timestamp ? &timestamp : NULL;
    return z_publisher_put(z_loan(entry->value), z_move(payload), &options);
}

EMSCRIPTEN_KEEPALIVE
int zt_publisher_delete(uint32_t publisher_id, int has_timestamp, const char *timestamp_id,
                        uint32_t timestamp_hi, uint32_t timestamp_lo) {
    zt_publisher_entry_t *entry = zt_find_publisher(publisher_id);
    if (entry == NULL) return -1;
    z_publisher_delete_options_t options;
    z_publisher_delete_options_default(&options);
    z_timestamp_t timestamp;
    if (zt_make_timestamp(&timestamp, has_timestamp, timestamp_id, timestamp_hi, timestamp_lo) < 0) return -3;
    options.timestamp = has_timestamp ? &timestamp : NULL;
    return z_publisher_delete(z_loan(entry->value), &options);
}

EMSCRIPTEN_KEEPALIVE
int zt_session_info_zid(uint32_t session_id) {
    zt_session_entry_t *entry = zt_find_session(session_id);
    if (entry == NULL) return -1;
    z_id_t zid = z_info_zid(z_loan(entry->value));
    z_owned_string_t string;
    int result = z_id_to_string(&zid, &string);
    if (result < 0) return result;
    zt_set_session_zid(z_string_data(z_loan(string)), z_string_len(z_loan(string)));
    z_drop(z_move(string));
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int zt_publisher_undeclare(uint32_t publisher_id) {
    zt_matching_listener_entry_t **listener_cursor = &zt_matching_listeners;
    while (*listener_cursor != NULL) {
        if ((*listener_cursor)->publisher_id == publisher_id) {
            zt_matching_listener_entry_t *listener = *listener_cursor;
            *listener_cursor = listener->next;
            z_drop(z_move(listener->value));
            z_free(listener);
        } else {
            listener_cursor = &(*listener_cursor)->next;
        }
    }
    zt_publisher_entry_t **cursor = &zt_publishers;
    while (*cursor != NULL && (*cursor)->id != publisher_id) cursor = &(*cursor)->next;
    if (*cursor == NULL) return -1;
    zt_publisher_entry_t *entry = *cursor;
    *cursor = entry->next;
    z_drop(z_move(entry->value));
    z_free(entry);
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int zt_publisher_matching_status(uint32_t publisher_id) {
    zt_publisher_entry_t *entry = zt_find_publisher(publisher_id);
    if (entry == NULL) return -1;
    z_matching_status_t status;
    int result = z_publisher_get_matching_status(z_loan(entry->value), &status);
    return result < 0 ? result : (status.matching ? 1 : 0);
}

EMSCRIPTEN_KEEPALIVE
int32_t zt_publisher_matching_listener_declare(uint32_t publisher_id) {
    zt_publisher_entry_t *publisher = zt_find_publisher(publisher_id);
    if (publisher == NULL) return -1;
    zt_matching_listener_entry_t *entry =
        (zt_matching_listener_entry_t *)z_malloc(sizeof(zt_matching_listener_entry_t));
    if (entry == NULL) return -1;
    entry->id = zt_allocate_handle();
    entry->session_id = publisher->session_id;
    entry->publisher_id = publisher_id;
    entry->next = NULL;
    z_owned_closure_matching_status_t callback;
    if (z_closure_matching_status(&callback, zt_matching_status_handler, NULL, entry) < 0) {
        z_free(entry);
        return -2;
    }
    int result = z_publisher_declare_matching_listener(z_loan(publisher->value), &entry->value, z_move(callback));
    if (result < 0) {
        z_free(entry);
        return result;
    }
    entry->next = zt_matching_listeners;
    zt_matching_listeners = entry;
    return (int32_t)entry->id;
}

EMSCRIPTEN_KEEPALIVE
int zt_matching_listener_undeclare(uint32_t listener_id) {
    zt_matching_listener_entry_t **cursor = &zt_matching_listeners;
    while (*cursor != NULL && (*cursor)->id != listener_id) cursor = &(*cursor)->next;
    if (*cursor == NULL) return -1;
    zt_matching_listener_entry_t *entry = *cursor;
    *cursor = entry->next;
    int result = z_undeclare_matching_listener(z_move(entry->value));
    z_free(entry);
    return result;
}

EMSCRIPTEN_KEEPALIVE
int32_t zt_queryable_declare(uint32_t session_id, const char *keyexpr, int complete) {
    zt_session_entry_t *session = zt_find_session(session_id);
    if (session == NULL || keyexpr == NULL) return -1;
    zt_queryable_entry_t *entry = (zt_queryable_entry_t *)z_malloc(sizeof(zt_queryable_entry_t));
    if (entry == NULL) return -1;
    entry->id = zt_allocate_handle();
    entry->session_id = session_id;
    entry->next = NULL;
    z_view_keyexpr_t key;
    z_owned_closure_query_t callback;
    if (z_view_keyexpr_from_str(&key, keyexpr) < 0 ||
        z_closure_query(&callback, zt_queryable_handler, NULL, entry) < 0) {
        z_free(entry);
        return -2;
    }
    z_queryable_options_t options;
    z_queryable_options_default(&options);
    options.complete = complete != 0;
    int result = z_declare_queryable(z_loan(session->value), &entry->value, z_loan(key),
                                     z_move(callback), &options);
    if (result < 0) {
        z_free(entry);
        return result;
    }
    entry->next = zt_queryables;
    zt_queryables = entry;
    return (int32_t)entry->id;
}

EMSCRIPTEN_KEEPALIVE
int zt_query_reply(uint32_t query_id, const char *keyexpr, const uint8_t *data, size_t len) {
    zt_incoming_query_entry_t *entry = zt_find_incoming_query(query_id);
    if (entry == NULL || keyexpr == NULL || (data == NULL && len != 0)) return -1;
    z_view_keyexpr_t key;
    if (z_view_keyexpr_from_str(&key, keyexpr) < 0) return -2;
    z_owned_bytes_t payload;
    int result = z_bytes_copy_from_buf(&payload, data, len);
    if (result < 0) return result;
    return z_query_reply(z_loan(entry->value), z_loan(key), z_move(payload), NULL);
}

EMSCRIPTEN_KEEPALIVE
int zt_query_reply_error(uint32_t query_id, const uint8_t *data, size_t len) {
    zt_incoming_query_entry_t *entry = zt_find_incoming_query(query_id);
    if (entry == NULL || (data == NULL && len != 0)) return -1;
    z_owned_bytes_t payload;
    int result = z_bytes_copy_from_buf(&payload, data, len);
    if (result < 0) return result;
    return z_query_reply_err(z_loan(entry->value), z_move(payload), NULL);
}

EMSCRIPTEN_KEEPALIVE
int zt_query_finish(uint32_t query_id) {
    zt_incoming_query_entry_t *entry = zt_find_incoming_query(query_id);
    if (entry == NULL) return -1;
    zt_drop_incoming_query(entry);
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int zt_queryable_undeclare(uint32_t queryable_id) {
    zt_queryable_entry_t **cursor = &zt_queryables;
    while (*cursor != NULL && (*cursor)->id != queryable_id) cursor = &(*cursor)->next;
    if (*cursor == NULL) return -1;
    zt_queryable_entry_t *entry = *cursor;
    *cursor = entry->next;
    int result = z_undeclare_queryable(z_move(entry->value));
    z_free(entry);
    return result;
}

EMSCRIPTEN_KEEPALIVE
int32_t zt_liveliness_token_declare(uint32_t session_id, const char *keyexpr) {
    zt_session_entry_t *session = zt_find_session(session_id);
    if (session == NULL || keyexpr == NULL) return -1;
    zt_liveliness_token_entry_t *entry =
        (zt_liveliness_token_entry_t *)z_malloc(sizeof(zt_liveliness_token_entry_t));
    if (entry == NULL) return -1;
    entry->id = zt_allocate_handle();
    entry->session_id = session_id;
    entry->next = NULL;
    z_view_keyexpr_t key;
    if (z_view_keyexpr_from_str(&key, keyexpr) < 0) {
        z_free(entry);
        return -2;
    }
    int result = z_liveliness_declare_token(z_loan(session->value), &entry->value, z_loan(key), NULL);
    if (result < 0) {
        z_free(entry);
        return result;
    }
    entry->next = zt_liveliness_tokens;
    zt_liveliness_tokens = entry;
    return (int32_t)entry->id;
}

EMSCRIPTEN_KEEPALIVE
int zt_liveliness_token_undeclare(uint32_t token_id) {
    zt_liveliness_token_entry_t **cursor = &zt_liveliness_tokens;
    while (*cursor != NULL && (*cursor)->id != token_id) cursor = &(*cursor)->next;
    if (*cursor == NULL) return -1;
    zt_liveliness_token_entry_t *entry = *cursor;
    *cursor = entry->next;
    int result = z_liveliness_undeclare_token(z_move(entry->value));
    z_free(entry);
    return result;
}

EMSCRIPTEN_KEEPALIVE
int32_t zt_liveliness_get(uint32_t session_id, const char *keyexpr, uint32_t timeout_ms) {
    zt_session_entry_t *session = zt_find_session(session_id);
    if (session == NULL || keyexpr == NULL) return -1;
    zt_query_entry_t *entry = (zt_query_entry_t *)z_malloc(sizeof(zt_query_entry_t));
    if (entry == NULL) return -1;
    entry->id = zt_allocate_handle();
    uint32_t query_id = entry->id;
    entry->session_id = session_id;
    entry->next = zt_queries;
    zt_queries = entry;
    z_view_keyexpr_t key;
    z_owned_closure_reply_t callback;
    if (z_view_keyexpr_from_str(&key, keyexpr) < 0 ||
        z_closure_reply(&callback, zt_query_reply_handler, zt_query_drop_handler, entry) < 0) {
        zt_query_drop_handler(entry);
        return -2;
    }
    z_liveliness_get_options_t options;
    z_liveliness_get_options_default(&options);
    options.timeout_ms = timeout_ms;
    int result = z_liveliness_get(z_loan(session->value), z_loan(key), z_move(callback), &options);
    return result < 0 ? result : (int32_t)query_id;
}

EMSCRIPTEN_KEEPALIVE
int zt_session_close(uint32_t session_id) {
    zt_session_entry_t **session_cursor = &zt_sessions;
    while (*session_cursor != NULL && (*session_cursor)->id != session_id) session_cursor = &(*session_cursor)->next;
    if (*session_cursor == NULL) return -1;

    zt_incoming_query_entry_t **incoming_cursor = &zt_incoming_queries;
    while (*incoming_cursor != NULL) {
        if ((*incoming_cursor)->session_id == session_id) {
            zt_incoming_query_entry_t *entry = *incoming_cursor;
            *incoming_cursor = entry->next;
            z_drop(z_move(entry->value));
            z_free(entry);
        } else {
            incoming_cursor = &(*incoming_cursor)->next;
        }
    }
    zt_queryable_entry_t **queryable_cursor = &zt_queryables;
    while (*queryable_cursor != NULL) {
        if ((*queryable_cursor)->session_id == session_id) {
            zt_queryable_entry_t *entry = *queryable_cursor;
            *queryable_cursor = entry->next;
            z_drop(z_move(entry->value));
            z_free(entry);
        } else {
            queryable_cursor = &(*queryable_cursor)->next;
        }
    }

    zt_matching_listener_entry_t **listener_cursor = &zt_matching_listeners;
    while (*listener_cursor != NULL) {
        if ((*listener_cursor)->session_id == session_id) {
            zt_matching_listener_entry_t *entry = *listener_cursor;
            *listener_cursor = entry->next;
            z_drop(z_move(entry->value));
            z_free(entry);
        } else {
            listener_cursor = &(*listener_cursor)->next;
        }
    }
    zt_subscriber_entry_t **sub_cursor = &zt_subscribers;
    while (*sub_cursor != NULL) {
        if ((*sub_cursor)->session_id == session_id) {
            zt_subscriber_entry_t *entry = *sub_cursor;
            *sub_cursor = entry->next;
            z_drop(z_move(entry->value));
            z_free(entry);
        } else {
            sub_cursor = &(*sub_cursor)->next;
        }
    }
    zt_publisher_entry_t **pub_cursor = &zt_publishers;
    while (*pub_cursor != NULL) {
        if ((*pub_cursor)->session_id == session_id) {
            zt_publisher_entry_t *entry = *pub_cursor;
            *pub_cursor = entry->next;
            z_drop(z_move(entry->value));
            z_free(entry);
        } else {
            pub_cursor = &(*pub_cursor)->next;
        }
    }
    zt_liveliness_token_entry_t **token_cursor = &zt_liveliness_tokens;
    while (*token_cursor != NULL) {
        if ((*token_cursor)->session_id == session_id) {
            zt_liveliness_token_entry_t *entry = *token_cursor;
            *token_cursor = entry->next;
            z_drop(z_move(entry->value));
            z_free(entry);
        } else {
            token_cursor = &(*token_cursor)->next;
        }
    }

    zt_session_entry_t *session = *session_cursor;
    *session_cursor = session->next;
    z_drop(z_move(session->value));
    z_free(session);
    return 0;
}

int main(void) { return 0; }
