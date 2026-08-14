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
