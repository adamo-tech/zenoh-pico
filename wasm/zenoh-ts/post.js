Module["writeToHeap"] = (bytes, pointer) => HEAPU8.set(bytes, pointer);
Module["getZenohTransportStats"] = () => {
    const links = globalThis.__zenohPicoWebTransport?.links;
    if (!links) return [];
    return Array.from(links.entries())
        .filter(([, link]) => link.owner === Module)
        .map(([handle, link]) => ({
            handle,
            ...link.stats,
            queuedBytes: link.incomingBytes,
            queuedChunks: link.incoming.length,
            closed: link.closed,
            writeError: link.writeError,
            lastError: link.lastError,
        }));
};
Module["getZenohMemoryStats"] = () => {
    const links = globalThis.__zenohPicoWebTransport?.links;
    return {
        wasmHeapBytes: HEAPU8.buffer.byteLength,
        ownedTransportLinks: links ? Array.from(links.values()).filter(link => link.owner === Module).length : 0,
    };
};
Module["forceZenohTransportReconnect"] = reason => {
    const links = globalThis.__zenohPicoWebTransport?.links;
    if (!links) return 0;
    let interrupted = 0;
    for (const [handle, link] of links.entries()) {
        if (link.owner !== Module || link.closed) continue;
        interrupted++;
        link.lastError = reason || "application watchdog requested reconnect";
        link.writeError = true;
        link.closed = true;
        Module.onZenohDiagnostic?.({
            type: "webtransport", event: "forced-failure", handle, reason: link.lastError,
            stats: {...link.stats}, queuedBytes: link.incomingBytes,
        });
        if (link.dataSignalResolve) link.dataSignalResolve();
        if (link.spaceSignalResolve) link.spaceSignalResolve();
        if (Module.onZenohTransportData) Module.onZenohTransportData(handle, 0);
        try { link.transport.close(); } catch (_) {}
    }
    return interrupted;
};
