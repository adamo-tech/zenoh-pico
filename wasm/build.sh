#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${ZENOH_PICO_WASM_BUILD_DIR:-$repo_dir/.build-wasm}"

command -v emcmake >/dev/null || { echo "emcmake is required" >&2; exit 1; }
command -v emcc >/dev/null || { echo "emcc is required" >&2; exit 1; }
node_bin="${EMSDK_NODE:-$(command -v node)}"

emcmake cmake -S "$repo_dir" -B "$build_dir" \
  -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_C_STANDARD=11 \
  -DBUILD_EXAMPLES=OFF \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  -DBUILD_INTEGRATION=OFF \
  -DBUILD_TOOLS=OFF \
  -DCHECK_THREADS=OFF \
  -DZENOH_LOG=info \
  -DZ_FEATURE_UNSTABLE_API=1 \
  -DZ_FEATURE_CONNECTIVITY=1 \
  -DZ_FEATURE_PUBLICATION=1 \
  -DZ_FEATURE_SUBSCRIPTION=1 \
  -DZ_FEATURE_QUERY=1 \
  -DZ_FEATURE_QUERYABLE=1 \
  -DZ_FEATURE_LIVELINESS=1 \
  -DZ_FEATURE_INTEREST=1 \
  -DZ_FEATURE_FRAGMENTATION=1 \
  -DZ_FEATURE_ENCODING_VALUES=1 \
  -DZ_FEATURE_MATCHING=1 \
  -DZ_FEATURE_AUTO_RECONNECT=1 \
  -DZ_FEATURE_UNICAST_TRANSPORT=1 \
  -DZ_FEATURE_LOCAL_SUBSCRIBER=1 \
  -DZ_FEATURE_LOCAL_QUERYABLE=1 \
  -DZ_FEATURE_MULTI_THREAD=0 \
  -DZ_FEATURE_LINK_WS=1 \
  -DZ_FEATURE_LINK_WEBTRANSPORT=1 \
  -DZ_FEATURE_LINK_TCP=0 \
  -DZ_FEATURE_LINK_UDP_MULTICAST=0 \
  -DZ_FEATURE_LINK_UDP_UNICAST=0 \
  -DZ_FEATURE_SCOUTING=0 \
  -DBATCH_UNICAST_SIZE=65535 \
  -DZ_TRANSPORT_LEASE=3000 \
  -DFRAG_MAX_SIZE=16777216

cmake --build "$build_dir" --parallel
mkdir -p "$repo_dir/dist-wasm"

emcc "$repo_dir/wasm/smoke.c" \
  -Wl,--whole-archive "$build_dir/lib/libzenohpico.a" -Wl,--no-whole-archive \
  -I"$repo_dir/include" \
  -I"$build_dir/include" \
  -DZENOH_EMSCRIPTEN \
  -DZENOH_COMPILER_CLANG \
  -DZENOH_C_STANDARD=11 \
  -O3 \
  -sALLOW_MEMORY_GROWTH=1 \
  -sASYNCIFY=1 \
  -sEXPORTED_FUNCTIONS=_main,_zenoh_pico_wasm_smoke \
  -sEXPORTED_RUNTIME_METHODS=ccall \
  -o "$repo_dir/dist-wasm/zenoh-pico.js"

"$node_bin" "$repo_dir/dist-wasm/zenoh-pico.js"

mkdir -p "$repo_dir/dist-wasm/browser"
emcc "$repo_dir/wasm/browser.c" \
  -Wl,--whole-archive "$build_dir/lib/libzenohpico.a" -Wl,--no-whole-archive \
  -I"$repo_dir/include" -I"$build_dir/include" \
  -DZENOH_EMSCRIPTEN -DZENOH_COMPILER_CLANG -DZENOH_C_STANDARD=11 -O3 \
  -sALLOW_MEMORY_GROWTH=1 -sASYNCIFY=1 \
  -sEXPORTED_FUNCTIONS=_main,_malloc,_free,_zenoh_pico_browser_open,_zenoh_pico_browser_put,_zenoh_pico_browser_put_binary,_zenoh_pico_browser_put_batch,_zenoh_pico_browser_subscribe_video,_zenoh_pico_browser_poll,_zenoh_pico_browser_close \
  -sEXPORTED_RUNTIME_METHODS=ccall \
  -o "$repo_dir/dist-wasm/browser/zenoh-pico-browser.js"
cp "$repo_dir/wasm/browser/index.html" "$repo_dir/dist-wasm/browser/index.html"
cp "$repo_dir/wasm/browser/video.html" "$repo_dir/dist-wasm/browser/video.html"
cp "$repo_dir/wasm/browser/video-node.html" "$repo_dir/dist-wasm/browser/video-node.html"

mkdir -p "$repo_dir/dist-wasm/zenoh-ts"
emcc "$repo_dir/wasm/zenoh_ts.c" \
  -Wl,--whole-archive "$build_dir/lib/libzenohpico.a" -Wl,--no-whole-archive \
  -I"$repo_dir/include" -I"$build_dir/include" \
  -DZENOH_EMSCRIPTEN -DZENOH_COMPILER_CLANG -DZENOH_C_STANDARD=11 -O3 \
  -sALLOW_MEMORY_GROWTH=1 -sASYNCIFY=1 -sMODULARIZE=1 -sEXPORT_ES6=1 \
  -sEXPORT_NAME=createZenohPicoModule -sENVIRONMENT=web \
  --post-js "$repo_dir/wasm/zenoh-ts/post.js" \
  -sEXPORTED_FUNCTIONS=_main,_malloc,_free,_zt_abi_version,_zt_session_open,_zt_session_poll,_zt_session_put,_zt_session_delete,_zt_session_get,_zt_session_info_zid,_zt_session_close,_zt_subscriber_declare,_zt_subscriber_undeclare,_zt_publisher_declare,_zt_publisher_put,_zt_publisher_delete,_zt_publisher_undeclare,_zt_liveliness_token_declare,_zt_liveliness_token_undeclare,_zt_liveliness_subscriber_declare,_zt_liveliness_get,_zt_publisher_matching_status,_zt_publisher_matching_listener_declare,_zt_matching_listener_undeclare,_zt_queryable_declare,_zt_queryable_undeclare,_zt_query_reply,_zt_query_reply_error,_zt_query_finish \
  -sEXPORTED_RUNTIME_METHODS=ccall \
  -o "$repo_dir/dist-wasm/zenoh-ts/zenoh-pico.mjs"
cp "$repo_dir/wasm/zenoh-ts/index.mjs" "$repo_dir/dist-wasm/zenoh-ts/index.mjs"
if command -v shasum >/dev/null 2>&1; then
  module_build_id="$(shasum -a 256 "$repo_dir/dist-wasm/zenoh-ts/zenoh-pico.mjs" | cut -d' ' -f1)"
  wasm_build_id="$(shasum -a 256 "$repo_dir/dist-wasm/zenoh-ts/zenoh-pico.wasm" | cut -d' ' -f1)"
else
  module_build_id="$(sha256sum "$repo_dir/dist-wasm/zenoh-ts/zenoh-pico.mjs" | cut -d' ' -f1)"
  wasm_build_id="$(sha256sum "$repo_dir/dist-wasm/zenoh-ts/zenoh-pico.wasm" | cut -d' ' -f1)"
fi
sed \
  -e "s|\"./zenoh-pico.mjs\"|\"./zenoh-pico.mjs?v=$module_build_id\"|" \
  -e "s|locateFile: options.locateFile,|locateFile: options.locateFile ?? (path => path === \"zenoh-pico.wasm\" ? new URL(\"./zenoh-pico.wasm?v=$wasm_build_id\", import.meta.url).href : path),|" \
  "$repo_dir/wasm/zenoh-ts/index.mjs" > "$repo_dir/dist-wasm/zenoh-ts/browser-index.mjs"
cp "$repo_dir/wasm/zenoh-ts/index.d.ts" "$repo_dir/dist-wasm/zenoh-ts/index.d.ts"
cp "$repo_dir/wasm/zenoh-ts/e2e.html" "$repo_dir/dist-wasm/zenoh-ts/e2e.html"
cp "$repo_dir/wasm/zenoh-ts/browser-tests.html" "$repo_dir/dist-wasm/zenoh-ts/browser-tests.html"
cp "$repo_dir/wasm/zenoh-ts/chaos-tests.html" "$repo_dir/dist-wasm/zenoh-ts/chaos-tests.html"
cp "$repo_dir/wasm/zenoh-ts/hosted-auth-tests.html" "$repo_dir/dist-wasm/zenoh-ts/hosted-auth-tests.html"
cp "$repo_dir/wasm/zenoh-ts/package.json" "$repo_dir/dist-wasm/zenoh-ts/package.json"
