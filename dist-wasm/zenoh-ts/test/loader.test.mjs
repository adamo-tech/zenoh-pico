import assert from "node:assert/strict";
import {readFile} from "node:fs/promises";
import test from "node:test";

async function instrumentedFactory() {
    const source = await readFile(new URL("../index.mjs", import.meta.url), "utf8");
    const instrumented = source.replace(
        'import createZenohPicoModule from "./zenoh-pico.mjs";',
        `const createZenohPicoModule = async options => {
            globalThis.__zenohPicoLoaderOptions = options;
            return {ccall: async name => name === "zt_abi_version" ? 7 : 0};
        };`,
    );
    assert.notEqual(instrumented, source, "Emscripten import was not instrumented");
    return import(`data:text/javascript;base64,${Buffer.from(instrumented).toString("base64")}`);
}

test("default loader leaves the WASM URL available for bundler rewriting", async () => {
    const {createZenohPico} = await instrumentedFactory();
    await createZenohPico();
    assert.equal(globalThis.__zenohPicoLoaderOptions.locateFile, undefined);
});

test("an explicit locateFile override is preserved", async () => {
    const {createZenohPico} = await instrumentedFactory();
    const locateFile = path => `/custom/${path}`;
    await createZenohPico({locateFile});
    assert.equal(globalThis.__zenohPicoLoaderOptions.locateFile, locateFile);
});
