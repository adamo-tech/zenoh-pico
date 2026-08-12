#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
adamo_dir="$(cd "$repo_dir/.." && pwd)"

source "$adamo_dir/activate-emsdk.sh"

emcmake cmake -S "$repo_dir" -B "$repo_dir/build-wasm" \
  -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_C_STANDARD=11 \
  -DBUILD_EXAMPLES=OFF \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  -DBUILD_INTEGRATION=OFF \
  -DBUILD_TOOLS=OFF \
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
  -DZ_FEATURE_LINK_WS=1 \
  -DZ_FEATURE_LINK_WEBTRANSPORT=1 \
  -DZ_FEATURE_LINK_TCP=0 \
  -DZ_FEATURE_LINK_UDP_MULTICAST=0 \
  -DZ_FEATURE_LINK_UDP_UNICAST=0 \
  -DZ_FEATURE_SCOUTING=0

cmake --build "$repo_dir/build-wasm" --parallel
mkdir -p "$repo_dir/dist-wasm"

emcc "$repo_dir/wasm/smoke.c" \
  -Wl,--whole-archive "$repo_dir/build-wasm/lib/libzenohpico.a" -Wl,--no-whole-archive \
  -I"$repo_dir/include" \
  -I"$repo_dir/build-wasm/include" \
  -DZENOH_EMSCRIPTEN \
  -DZENOH_COMPILER_CLANG \
  -DZENOH_C_STANDARD=11 \
  -O3 \
  -sALLOW_MEMORY_GROWTH=1 \
  -sASYNCIFY=1 \
  -sEXPORTED_FUNCTIONS=_main,_zenoh_pico_wasm_smoke \
  -sEXPORTED_RUNTIME_METHODS=ccall \
  -o "$repo_dir/dist-wasm/zenoh-pico.js"

"$EMSDK_NODE" "$repo_dir/dist-wasm/zenoh-pico.js"
