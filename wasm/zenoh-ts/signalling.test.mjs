import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {test} from 'node:test';
async function runtime() {
  let source=await readFile(new URL('./index.mjs',import.meta.url),'utf8');
  source=source.replace('import createZenohPicoModule from "./zenoh-pico.mjs";', `const createZenohPicoModule=async options=>{
    globalThis.testOptions=options;
    globalThis.testCalls=[];
    const module={ccall:async name=>{testCalls.push(name); return name==='zt_abi_version'?7:name==='zt_session_open'?1:0;}};
    globalThis.testModule=module;
    return module;
  };`);
  const api=await import('data:text/javascript;base64,'+Buffer.from(source).toString('base64'));
  return api.createZenohPico();
}
test('application owns replacement; capability and closure are exposed once',async()=>{
  const r=await runtime();
  const s=await r.open('webtransport/test.invalid:443',{reconnectOwner:'application'});
  assert.equal(testModule.zenohPicoOwnerReconnect,true);
  testOptions.onZenohSignallingToken('opaque');assert.equal(s.signallingToken,'opaque');
  let closed=0;s.onClosed(()=>closed++);
  testOptions.onZenohSessionLost();testOptions.onZenohSessionLost();
  await s.close();assert.equal(closed,1);
  s.onClosed(()=>closed++);assert.equal(closed,2);
  assert.equal(testCalls.filter(name=>name==='zt_session_open').length,1);
  assert.equal(testCalls.filter(name=>name==='zt_session_close').length,1);
});
test('default sessions retain native reconnect and unsubscribe suppresses callbacks',async()=>{
  const r=await runtime();const s=await r.open('webtransport/test.invalid:443');
  assert.equal(testModule.zenohPicoOwnerReconnect,false);
  let calls=0;const stop=s.onClosed(()=>calls++);stop();s.invalidate();await s.close();assert.equal(calls,0);
});
