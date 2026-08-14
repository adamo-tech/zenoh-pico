export interface PicoRuntimeOptions {
    certificateHash?: string;
    receiveBufferBytes?: number;
    locateFile?: (path: string) => string;
    print?: (message: string) => void;
    printErr?: (message: string) => void;
    onDiagnostic?: (diagnostic: Record<string, unknown>) => void;
    resolveWebTransportUrl?: (endpoint: string) => string | Promise<string>;
}

export interface PicoSessionOptions {
    certificateHash?: string;
    pollIntervalMs?: number;
    resolveWebTransportUrl?: (endpoint: string) => string | Promise<string>;
}

export interface PicoSample {
    keyExpr: string;
    payload: Uint8Array;
    kind: number;
    priority: number;
    congestionControl: number;
    reliability: number;
    express: boolean;
    timestamp?: PicoTimestamp;
}

export interface PicoQosOptions {
    priority?: number;
    congestionControl?: number;
    reliability?: number;
    express?: boolean;
    timestamp?: PicoTimestamp | {id: string; ntp64: bigint};
}

export interface PicoGetOptions extends Pick<PicoQosOptions,
    "priority" | "congestionControl" | "express"> {
    parameters?: string;
    payload?: Uint8Array | ArrayBuffer;
    timeoutMs?: number;
    target?: number;
    consolidation?: number;
    acceptReplies?: number;
}

export function createZenohPico(options?: PicoRuntimeOptions): Promise<PicoRuntime>;

export class PicoRuntime {
    open(endpoint: string, options?: PicoSessionOptions): Promise<PicoSession>;
}

export class PicoSession implements AsyncDisposable {
    readonly closed: boolean;
    put(keyExpr: string, payload: Uint8Array | ArrayBuffer, options?: PicoQosOptions): Promise<void>;
    delete(keyExpr: string, options?: PicoQosOptions): Promise<void>;
    get(keyExpr: string, options?: PicoGetOptions): Promise<PicoReceiver<PicoReply>>;
    diagnostics(): Array<Record<string, unknown>>;
    performanceDiagnostics(): {
        allocMs: number;
        copyMs: number;
        wasmCallMs: number;
        byteCalls: number;
        bytesCopied: number;
    };
    declareSubscriber(keyExpr: string, callback: (sample: PicoSample) => void): Promise<PicoSubscriber>;
    declarePublisher(keyExpr: string, options?: PicoQosOptions): Promise<PicoPublisher>;
    declareQueryable(
        keyExpr: string,
        callback: (query: PicoQuery) => void | Promise<void>,
        options?: {complete?: boolean},
    ): Promise<PicoQueryable>;
    info(): Promise<{zid: PicoZenohId}>;
    liveliness(): PicoLiveliness;
    close(): Promise<void>;
    [Symbol.asyncDispose](): Promise<void>;
}

export class PicoSubscriber implements AsyncDisposable {
    readonly keyExpr: string;
    undeclare(): Promise<void>;
    [Symbol.asyncDispose](): Promise<void>;
}

export class PicoPublisher implements AsyncDisposable {
    readonly keyExpr: string;
    put(payload: Uint8Array | ArrayBuffer, options?: Pick<PicoQosOptions, "timestamp">): Promise<void>;
    delete(options?: Pick<PicoQosOptions, "timestamp">): Promise<void>;
    matchingStatus(): Promise<{matching: boolean}>;
    declareMatchingListener(callback: (status: {matching: boolean}) => void): Promise<PicoMatchingListener>;
    undeclare(): Promise<void>;
    [Symbol.asyncDispose](): Promise<void>;
}

export class PicoMatchingListener implements AsyncDisposable {
    undeclare(): Promise<void>;
    [Symbol.asyncDispose](): Promise<void>;
}

export class PicoQueryable implements AsyncDisposable {
    readonly keyExpr: string;
    undeclare(): Promise<void>;
    [Symbol.asyncDispose](): Promise<void>;
}

export class PicoQuery {
    readonly keyExpr: string;
    readonly parameters: string;
    readonly payload: Uint8Array;
    readonly finished: boolean;
    reply(payload: Uint8Array | ArrayBuffer, options?: {keyExpr?: string}): Promise<void>;
    replyError(payload: Uint8Array | ArrayBuffer): Promise<void>;
    finish(): Promise<void>;
}

export class PicoReply {
    readonly sample?: PicoSample;
    readonly error?: PicoReplyError;
    isOk(): boolean;
    result(): PicoSample | PicoReplyError | undefined;
}

export class PicoReplyError {
    readonly payload: Uint8Array;
}

export class PicoLiveliness {
    declareToken(keyExpr: string): Promise<PicoLivelinessToken>;
    declareSubscriber(
        keyExpr: string,
        callback: (sample: PicoSample) => void,
        options?: {history?: boolean},
    ): Promise<PicoSubscriber>;
    get(keyExpr: string, options?: {timeoutMs?: number}): Promise<PicoReceiver<PicoSample>>;
}

export class PicoLivelinessToken implements AsyncDisposable {
    readonly keyExpr: string;
    undeclare(): Promise<void>;
    [Symbol.asyncDispose](): Promise<void>;
}

export class PicoReceiver<T> implements AsyncIterableIterator<T> {
    readonly closed: boolean;
    next(): Promise<IteratorResult<T>>;
    [Symbol.asyncIterator](): AsyncIterableIterator<T>;
}

export const SampleKind: Readonly<{PUT: 0; DELETE: 1}>;
export const Priority: Readonly<{
    REAL_TIME: 1; INTERACTIVE_HIGH: 2; INTERACTIVE_LOW: 3; DATA_HIGH: 4;
    DATA: 5; DATA_LOW: 6; BACKGROUND: 7;
}>;
export const CongestionControl: Readonly<{DROP: 0; BLOCK: 1}>;
export const Reliability: Readonly<{RELIABLE: 0; BEST_EFFORT: 1}>;
export const QueryTarget: Readonly<{BEST_MATCHING: 0; ALL: 1; ALL_COMPLETE: 2}>;
export const ConsolidationMode: Readonly<{AUTO: -1; NONE: 0; MONOTONIC: 1; LATEST: 2}>;
export const ReplyKeyExpr: Readonly<{ANY: 0; MATCHING_QUERY: 1}>;
export class PicoZenohId {
    constructor(value: string);
    toString(): string;
}
export class PicoTimestamp {
    constructor(id: PicoZenohId | string, ntp64: bigint);
    getId(): PicoZenohId;
    getNtp64(): bigint;
    getMsSinceUnixEpoch(): number;
    asDate(): Date;
}
