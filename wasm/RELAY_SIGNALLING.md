# Optional relay capability and application-owned recovery

Adds support for the optional Adamo OpenAck ZBuf extension `0x4e`, carrying an opaque connection capability (1–4096 bytes). Unknown optional extensions remain skippable; unknown mandatory, duplicate, truncated and oversized capabilities fail decoding. Ownership follows transport-message copy/clear. The browser receives the token only after an accepted OpenAck.

`PicoSession.signallingToken` exposes the capability; never log it. This library does not choose a notifier or make a signalling connection. The embedding application supplies the trusted service.

`runtime.open(endpoint, {reconnectOwner: "application"})` disables native automatic reopening for that independent WASM module. Lease/read failures signal `session.onClosed(listener)`; the application closes and replaces the session, rebuilding declarations. `session.invalidate()` does the same for an authenticated out-of-band failure hint. Closure subscriptions replay an already-closed state and fire once. Default transport-owned reconnection is unchanged.

The changes are stacked on existing WebTransport recovery PR #1. No native robot client is deployed by this change. Existing application-side credential redaction is carried into the maintained source so rebuilding preserves it.

Build: `docker run --rm -v "$PWD:/src" -w /src emscripten/emsdk:4.0.15 bash wasm/build.sh`.
Codec test (same container, after build):
```
emcc wasm/signalling_codec_test.c .build-wasm/lib/libzenohpico.a -Iinclude -I.build-wasm/include -DZENOH_EMSCRIPTEN -DZENOH_COMPILER_CLANG -DZENOH_C_STANDARD=11 -sASYNCIFY=1 -o /tmp/signalling-test.js
node /tmp/signalling-test.js
```

Browser session event/lifetime tests and live London video checks are recorded in the consuming adamo-web PR. This extension is an Adamo protocol extension, not a claim of upstream standardization.
