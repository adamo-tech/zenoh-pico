import createZenohPicoModule from "./zenoh-pico.mjs";

function checkResult(operation, result) {
    if (result < 0) throw new Error(`${operation} failed with zenoh-pico error ${result}`);
    return result;
}

const defaultQos = Object.freeze({
    priority: 5,
    congestionControl: 1,
    reliability: 0,
    express: false,
});

function qosArguments(options = {}) {
    const qos = {...defaultQos, ...options};
    if (!Number.isInteger(qos.priority) || qos.priority < 1 || qos.priority > 7) {
        throw new TypeError(`priority must be an integer from 1 through 7, got ${qos.priority}`);
    }
    if (qos.congestionControl !== 0 && qos.congestionControl !== 1) {
        throw new TypeError(`congestionControl must be CongestionControl.DROP or BLOCK, got ${qos.congestionControl}`);
    }
    if (qos.reliability !== 0 && qos.reliability !== 1) {
        throw new TypeError(`reliability must be Reliability.RELIABLE or BEST_EFFORT, got ${qos.reliability}`);
    }
    if (typeof qos.express !== "boolean") throw new TypeError("express must be a boolean");
    return [qos.priority, qos.congestionControl, qos.reliability, Number(qos.express)];
}

function timestampArguments(options = {}) {
    const timestamp = options.timestamp;
    if (timestamp == null) return [0, null, 0, 0];
    const id = typeof timestamp.getId === "function" ? timestamp.getId().toString() : timestamp.id;
    const ntp64 = typeof timestamp.getNtp64 === "function" ? timestamp.getNtp64() : timestamp.ntp64;
    if (typeof id !== "string" || !/^[0-9a-fA-F]{32}$/.test(id)) {
        throw new TypeError("timestamp id must be exactly 32 hexadecimal characters");
    }
    if (typeof ntp64 !== "bigint" || ntp64 < 0n || ntp64 > 0xffffffffffffffffn) {
        throw new TypeError("timestamp ntp64 must be an unsigned 64-bit bigint");
    }
    return [1, id, Number((ntp64 >> 32n) & 0xffffffffn), Number(ntp64 & 0xffffffffn)];
}

function timeoutMilliseconds(value, name = "timeoutMs") {
    if (value == null) return 0;
    const number = Number(value);
    if (!Number.isSafeInteger(number) || number < 0 || number > 0xffffffff) {
        throw new TypeError(`${name} must be an integer from 0 through 4294967295, got ${value}`);
    }
    return number;
}

async function createBinding(options) {
    const subscribers = new Map();
    const matchingListeners = new Map();
    const queryables = new Map();
    const queryReceivers = new Map();
    const pendingQueryEvents = new Map();
    const deliverQueryEvent = (state, event) => {
        if (event.done) state.receiver.close();
        else if (event.error !== undefined) state.receiver.push(state.wrapError(event.error));
        else state.receiver.push(state.wrapSample(event.sample));
    };
    const dispatchQueryEvent = (handle, event) => {
        const state = queryReceivers.get(handle);
        if (state) deliverQueryEvent(state, event);
        else {
            const pending = pendingQueryEvents.get(handle) ?? [];
            pending.push(event); pendingQueryEvents.set(handle, pending);
        }
        if (event.done) queryReceivers.delete(handle);
    };
    const performanceStats = {allocMs: 0, copyMs: 0, wasmCallMs: 0, byteCalls: 0, bytesCopied: 0};
    let transportWake = () => {};
    const module = await createZenohPicoModule({
        // Leave the default unset so bundlers can rewrite Emscripten's static
        // `new URL("zenoh-pico.wasm", import.meta.url)` reference to a hashed
        // production asset. A dynamic fallback here defeats that rewrite and
        // makes SPA hosts return index.html for the missing unhashed URL.
        locateFile: options.locateFile,
        zenohPicoReceiveBufferBytes: options.receiveBufferBytes,
        print: options.print,
        printErr: options.printErr,
        onZenohDiagnostic: options.onDiagnostic,
        onZenohTransportData() { transportWake(); },
        onZenohSample(handle, keyExpr, payload, kind, metadata) {
            const timestamp = metadata.timestamp
                ? new PicoTimestamp(metadata.timestamp.id,
                    (BigInt(metadata.timestamp.ntp64High) << 32n) | BigInt(metadata.timestamp.ntp64Low))
                : undefined;
            const sample = {keyExpr, payload, kind, ...metadata, timestamp};
            const subscriber = subscribers.get(handle);
            subscriber ? subscriber(sample) : dispatchQueryEvent(handle, {sample});
        },
        onZenohMatchingStatus(handle, matching) { matchingListeners.get(handle)?.({matching}); },
        onZenohQueryError(handle, payload) { dispatchQueryEvent(handle, {error: payload}); },
        onZenohQueryDone(handle) { dispatchQueryEvent(handle, {done: true}); },
        onZenohIncomingQuery(queryableHandle, queryHandle, keyExpr, parameters, payload) {
            const callback = queryables.get(queryableHandle);
            const query = new PicoQuery(binding, queryHandle, keyExpr, parameters, payload);
            if (!callback) {
                void query.finish();
                return;
            }
            void Promise.resolve().then(() => callback(query)).catch(async error => {
                const message = new TextEncoder().encode(error?.message ?? String(error));
                await query.replyError(message).catch(() => {});
            }).finally(() => query.finish()).catch(error => {
                options.onDiagnostic?.({type: "query-finish-error", queryHandle, error});
            });
        },
    });
    let queue = Promise.resolve();
    const call = (name, returnType, argTypes = [], args = []) => {
        const operation = queue.then(() => module.ccall(name, returnType, argTypes, args, {async: true}));
        queue = operation.catch(() => {});
        return operation;
    };
    const callWithBytes = (name, leadingTypes, leadingArgs, bytes) => {
        const operation = queue.then(async () => {
            const allocStarted = performance.now();
            const pointer = module._malloc(Math.max(1, bytes.byteLength));
            performanceStats.allocMs += performance.now() - allocStarted;
            if (!pointer) throw new Error(`Unable to allocate ${bytes.byteLength} WASM bytes`);
            try {
                const copyStarted = performance.now();
                module.writeToHeap(bytes, pointer);
                performanceStats.copyMs += performance.now() - copyStarted;
                performanceStats.bytesCopied += bytes.byteLength;
                performanceStats.byteCalls++;
                const callStarted = performance.now();
                const result = await module.ccall(name, "number", [...leadingTypes, "number", "number"],
                    [...leadingArgs, pointer, bytes.byteLength], {async: true});
                performanceStats.wasmCallMs += performance.now() - callStarted;
                return result;
            } finally {
                module._free(pointer);
            }
        });
        queue = operation.catch(() => {});
        return operation;
    };
    const abiVersion = await call("zt_abi_version", "number");
    if (abiVersion !== 7) throw new Error(`Unsupported zenoh-pico WASM ABI ${abiVersion}`);
    const binding = {
        module, call, callWithBytes, subscribers, matchingListeners, queryables, queryReceivers, pendingQueryEvents,
        attachQueryReceiver(handle, receiver, wrappers = {}) {
            const state = {
                receiver,
                wrapSample: wrappers.wrapSample ?? (sample => sample),
                wrapError: wrappers.wrapError ?? (error => error),
            };
            queryReceivers.set(handle, state);
            for (const event of pendingQueryEvents.get(handle) ?? []) deliverQueryEvent(state, event);
            pendingQueryEvents.delete(handle);
            if (receiver.closed) queryReceivers.delete(handle);
        },
        performanceStats() { return {...performanceStats}; },
        setTransportWake(callback) { transportWake = callback; },
    };
    return binding;
}

export async function createZenohPico(options = {}) {
    // Eagerly instantiate the first session runtime so module/WASM/ABI errors
    // are reported by the factory. Further sessions receive independent
    // instances, avoiding Asyncify re-entrancy and cross-session backpressure.
    const firstBinding = await createBinding(options);
    return new PicoRuntime(options, firstBinding);
}

class PicoRuntime {
    constructor(options, firstBinding) {
        this.options = options;
        this.firstBinding = firstBinding;
    }

    async open(endpoint, options = {}) {
        const binding = this.firstBinding ?? await createBinding(this.options);
        this.firstBinding = undefined;
        binding.module.zenohPicoResolveWebTransportUrl =
            options.resolveWebTransportUrl ?? this.options.resolveWebTransportUrl;
        const handle = checkResult("session open", await binding.call(
            "zt_session_open", "number", ["string", "string"],
            [endpoint, options.certificateHash ?? this.options.certificateHash ?? null],
        ));
        const session = new PicoSession(binding, handle, options.pollIntervalMs ?? 0);
        session.startPolling();
        return session;
    }
}

export class PicoSession {
    constructor(runtime, handle, pollIntervalMs) {
        this.runtime = runtime;
        this.handle = handle;
        this.pollIntervalMs = pollIntervalMs;
        this.closed = false;
        this.pollTimer = undefined;
        this.polling = false;
        this.pollRequested = false;
        this.runtime.setTransportWake(() => this.requestPoll());
    }

    startPolling() {
        this.requestPoll();
    }

    requestPoll() {
        if (this.closed) return;
        if (this.pollTimer !== undefined) {
            clearTimeout(this.pollTimer);
            this.pollTimer = undefined;
        }
        this.pollRequested = true;
        if (this.polling) return;
        this.polling = true;
        const poll = async () => {
            try {
                do {
                    this.pollRequested = false;
                    await this.runtime.call("zt_session_poll", "number", ["number", "number"], [this.handle, 64]);
                } while (!this.closed && this.pollRequested);
            } finally {
                this.polling = false;
                if (!this.closed) {
                    // Periodic work remains necessary for keepalive and reconnect;
                    // inbound data is handled immediately by the transport wake-up.
                    this.pollTimer = setTimeout(() => this.requestPoll(), Math.max(10, this.pollIntervalMs));
                }
            }
        };
        void poll();
    }

    async put(keyExpr, payload, options = {}) {
        const bytes = payload instanceof Uint8Array ? payload : new Uint8Array(payload);
        const qos = qosArguments(options);
        const timestamp = timestampArguments(options);
        checkResult("put", await this.runtime.callWithBytes(
            "zt_session_put", ["number", "string", "number", "number", "number", "number", "number", "string", "number", "number"],
            [this.handle, keyExpr, ...qos, ...timestamp], bytes,
        ));
    }

    diagnostics() {
        return this.runtime.module.getZenohTransportStats?.() ?? [];
    }

    performanceDiagnostics() {
        return this.runtime.performanceStats();
    }

    async delete(keyExpr, options = {}) {
        const qos = qosArguments(options);
        const timestamp = timestampArguments(options);
        checkResult("delete", await this.runtime.call(
            "zt_session_delete", "number",
            ["number", "string", "number", "number", "number", "number", "number", "string", "number", "number"],
            [this.handle, keyExpr, ...qos, ...timestamp],
        ));
    }

    async get(keyExpr, options = {}) {
        const payload = options.payload == null
            ? new Uint8Array(0)
            : options.payload instanceof Uint8Array ? options.payload : new Uint8Array(options.payload);
        const qos = qosArguments(options);
        const receiver = new PicoReceiver();
        const handle = checkResult("get", await this.runtime.callWithBytes(
            "zt_session_get",
            ["number", "string", "string", "number", "number", "number", "number",
                "number", "number", "number", "number"],
            [this.handle, keyExpr, options.parameters ?? "", timeoutMilliseconds(options.timeoutMs),
                qos[0], qos[1], qos[3], options.target ?? QueryTarget.BEST_MATCHING,
                options.consolidation ?? ConsolidationMode.AUTO,
                options.acceptReplies ?? ReplyKeyExpr.MATCHING_QUERY,
                Number(options.payload != null)],
            payload,
        ));
        this.runtime.attachQueryReceiver(handle, receiver, {
            wrapSample: sample => new PicoReply(sample, undefined),
            wrapError: error => new PicoReply(undefined, new PicoReplyError(error)),
        });
        return receiver;
    }

    async declareSubscriber(keyExpr, callback) {
        const handle = checkResult("subscriber declaration", await this.runtime.call(
            "zt_subscriber_declare", "number", ["number", "string"], [this.handle, keyExpr],
        ));
        this.runtime.subscribers.set(handle, callback);
        return new PicoSubscriber(this.runtime, handle, keyExpr);
    }

    async declarePublisher(keyExpr, options = {}) {
        const qos = qosArguments(options);
        const handle = checkResult("publisher declaration", await this.runtime.call(
            "zt_publisher_declare", "number",
            ["number", "string", "number", "number", "number", "number"],
            [this.handle, keyExpr, ...qos],
        ));
        return new PicoPublisher(this.runtime, handle, keyExpr);
    }

    async declareQueryable(keyExpr, callback, options = {}) {
        const handle = checkResult("queryable declaration", await this.runtime.call(
            "zt_queryable_declare", "number", ["number", "string", "number"],
            [this.handle, keyExpr, Number(options.complete ?? false)],
        ));
        this.runtime.queryables.set(handle, callback);
        return new PicoQueryable(this.runtime, handle, keyExpr);
    }

    async info() {
        checkResult("session info", await this.runtime.call(
            "zt_session_info_zid", "number", ["number"], [this.handle],
        ));
        return {zid: new PicoZenohId(this.runtime.module.zenohPicoLastSessionZid)};
    }

    liveliness() { return new PicoLiveliness(this.runtime, this.handle); }

    async close() {
        if (this.closed) return;
        this.closed = true;
        this.runtime.setTransportWake(() => {});
        if (this.pollTimer !== undefined) clearTimeout(this.pollTimer);
        checkResult("session close", await this.runtime.call(
            "zt_session_close", "number", ["number"], [this.handle],
        ));
    }

    async [Symbol.asyncDispose]() { await this.close(); }
}

export class PicoSubscriber {
    constructor(runtime, handle, keyExpr) {
        this.runtime = runtime;
        this.handle = handle;
        this.keyExpr = keyExpr;
        this.declared = true;
    }

    async undeclare() {
        if (!this.declared) return;
        this.declared = false;
        this.runtime.subscribers.delete(this.handle);
        checkResult("subscriber undeclaration", await this.runtime.call(
            "zt_subscriber_undeclare", "number", ["number"], [this.handle],
        ));
    }

    async [Symbol.asyncDispose]() { await this.undeclare(); }
}

export class PicoPublisher {
    constructor(runtime, handle, keyExpr) {
        this.runtime = runtime;
        this.handle = handle;
        this.keyExpr = keyExpr;
        this.declared = true;
    }

    async put(payload, options = {}) {
        const bytes = payload instanceof Uint8Array ? payload : new Uint8Array(payload);
        const timestamp = timestampArguments(options);
        checkResult("publisher put", await this.runtime.callWithBytes(
            "zt_publisher_put", ["number", "number", "string", "number", "number"],
            [this.handle, ...timestamp], bytes,
        ));
    }

    async delete(options = {}) {
        const timestamp = timestampArguments(options);
        checkResult("publisher delete", await this.runtime.call(
            "zt_publisher_delete", "number", ["number", "number", "string", "number", "number"],
            [this.handle, ...timestamp],
        ));
    }

    async matchingStatus() {
        return {matching: Boolean(checkResult("publisher matching status", await this.runtime.call(
            "zt_publisher_matching_status", "number", ["number"], [this.handle],
        )))};
    }

    async declareMatchingListener(callback) {
        const handle = checkResult("publisher matching listener declaration", await this.runtime.call(
            "zt_publisher_matching_listener_declare", "number", ["number"], [this.handle],
        ));
        this.runtime.matchingListeners.set(handle, callback);
        return new PicoMatchingListener(this.runtime, handle);
    }

    async undeclare() {
        if (!this.declared) return;
        this.declared = false;
        checkResult("publisher undeclaration", await this.runtime.call(
            "zt_publisher_undeclare", "number", ["number"], [this.handle],
        ));
    }

    async [Symbol.asyncDispose]() { await this.undeclare(); }
}

export class PicoMatchingListener {
    constructor(runtime, handle) {
        this.runtime = runtime;
        this.handle = handle;
        this.declared = true;
    }

    async undeclare() {
        if (!this.declared) return;
        this.declared = false;
        this.runtime.matchingListeners.delete(this.handle);
        checkResult("matching listener undeclaration", await this.runtime.call(
            "zt_matching_listener_undeclare", "number", ["number"], [this.handle],
        ));
    }

    async [Symbol.asyncDispose]() { await this.undeclare(); }
}

export class PicoQueryable {
    constructor(runtime, handle, keyExpr) {
        this.runtime = runtime;
        this.handle = handle;
        this.keyExpr = keyExpr;
        this.declared = true;
    }

    async undeclare() {
        if (!this.declared) return;
        this.declared = false;
        this.runtime.queryables.delete(this.handle);
        checkResult("queryable undeclaration", await this.runtime.call(
            "zt_queryable_undeclare", "number", ["number"], [this.handle],
        ));
    }

    async [Symbol.asyncDispose]() { await this.undeclare(); }
}

export class PicoQuery {
    constructor(runtime, handle, keyExpr, parameters, payload) {
        this.runtime = runtime;
        this.handle = handle;
        this.keyExpr = keyExpr;
        this.parameters = parameters;
        this.payload = payload;
        this.finished = false;
    }

    async reply(payload, options = {}) {
        if (this.finished) throw new Error("query is already finished");
        const bytes = payload instanceof Uint8Array ? payload : new Uint8Array(payload);
        checkResult("query reply", await this.runtime.callWithBytes(
            "zt_query_reply", ["number", "string"], [this.handle, options.keyExpr ?? this.keyExpr], bytes,
        ));
    }

    async replyError(payload) {
        if (this.finished) throw new Error("query is already finished");
        const bytes = payload instanceof Uint8Array ? payload : new Uint8Array(payload);
        checkResult("query error reply", await this.runtime.callWithBytes(
            "zt_query_reply_error", ["number"], [this.handle], bytes,
        ));
    }

    async finish() {
        if (this.finished) return;
        this.finished = true;
        checkResult("query completion", await this.runtime.call(
            "zt_query_finish", "number", ["number"], [this.handle],
        ));
    }
}

export class PicoReply {
    constructor(sample, error) {
        this.sample = sample;
        this.error = error;
    }
    isOk() { return this.sample !== undefined; }
    result() { return this.sample ?? this.error; }
}

export class PicoReplyError {
    constructor(payload) { this.payload = payload; }
}

export class PicoLiveliness {
    constructor(runtime, sessionHandle) {
        this.runtime = runtime;
        this.sessionHandle = sessionHandle;
    }

    async declareToken(keyExpr) {
        const handle = checkResult("liveliness token declaration", await this.runtime.call(
            "zt_liveliness_token_declare", "number", ["number", "string"], [this.sessionHandle, keyExpr],
        ));
        return new PicoLivelinessToken(this.runtime, handle, keyExpr);
    }

    async declareSubscriber(keyExpr, callback, options = {}) {
        const handle = checkResult("liveliness subscriber declaration", await this.runtime.call(
            "zt_liveliness_subscriber_declare", "number", ["number", "string", "number"],
            [this.sessionHandle, keyExpr, Number(options.history ?? false)],
        ));
        this.runtime.subscribers.set(handle, callback);
        return new PicoSubscriber(this.runtime, handle, keyExpr);
    }


    async get(keyExpr, options = {}) {
        const receiver = new PicoReceiver();
        const handle = checkResult("liveliness get", await this.runtime.call(
            "zt_liveliness_get", "number", ["number", "string", "number"],
            [this.sessionHandle, keyExpr, options.timeoutMs ?? 0],
        ));
        this.runtime.attachQueryReceiver(handle, receiver);
        return receiver;
    }
}

export class PicoLivelinessToken {
    constructor(runtime, handle, keyExpr) {
        this.runtime = runtime;
        this.handle = handle;
        this.keyExpr = keyExpr;
        this.declared = true;
    }

    async undeclare() {
        if (!this.declared) return;
        this.declared = false;
        checkResult("liveliness token undeclaration", await this.runtime.call(
            "zt_liveliness_token_undeclare", "number", ["number"], [this.handle],
        ));
    }

    async [Symbol.asyncDispose]() { await this.undeclare(); }
}

export class PicoReceiver {
    constructor() {
        this.values = [];
        this.waiters = [];
        this.closed = false;
    }
    push(value) {
        if (this.closed) return;
        const waiter = this.waiters.shift();
        waiter ? waiter({value, done: false}) : this.values.push(value);
    }
    close() {
        if (this.closed) return;
        this.closed = true;
        for (const waiter of this.waiters.splice(0)) waiter({value: undefined, done: true});
    }
    next() {
        if (this.values.length) return Promise.resolve({value: this.values.shift(), done: false});
        if (this.closed) return Promise.resolve({value: undefined, done: true});
        return new Promise(resolve => this.waiters.push(resolve));
    }
    [Symbol.asyncIterator]() { return this; }
}

export const SampleKind = Object.freeze({PUT: 0, DELETE: 1});
export const Priority = Object.freeze({
    REAL_TIME: 1, INTERACTIVE_HIGH: 2, INTERACTIVE_LOW: 3, DATA_HIGH: 4,
    DATA: 5, DATA_LOW: 6, BACKGROUND: 7,
});
export const CongestionControl = Object.freeze({DROP: 0, BLOCK: 1});
export const Reliability = Object.freeze({RELIABLE: 0, BEST_EFFORT: 1});
export const QueryTarget = Object.freeze({BEST_MATCHING: 0, ALL: 1, ALL_COMPLETE: 2});
export const ConsolidationMode = Object.freeze({AUTO: -1, NONE: 0, MONOTONIC: 1, LATEST: 2});
export const ReplyKeyExpr = Object.freeze({ANY: 0, MATCHING_QUERY: 1});

export class PicoZenohId {
    constructor(value) {
        if (typeof value !== "string" || !/^[0-9a-fA-F]{32}$/.test(value)) {
            throw new TypeError("Zenoh id must be exactly 32 hexadecimal characters");
        }
        this.value = value.toLowerCase();
    }
    toString() { return this.value; }
}

export class PicoTimestamp {
    constructor(id, ntp64) {
        this.id = id instanceof PicoZenohId ? id : new PicoZenohId(id);
        if (typeof ntp64 !== "bigint" || ntp64 < 0n || ntp64 > 0xffffffffffffffffn) {
            throw new TypeError("ntp64 must be an unsigned 64-bit bigint");
        }
        this.ntp64 = ntp64;
    }
    getId() { return this.id; }
    getNtp64() { return this.ntp64; }
    getMsSinceUnixEpoch() {
        return Number(this.ntp64 >> 32n) * 1000 + Number(this.ntp64 & 0xffffffffn) * 1000 / 4294967296;
    }
    asDate() { return new Date(this.getMsSinceUnixEpoch()); }
}
