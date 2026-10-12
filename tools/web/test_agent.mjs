// Node 22+: server gating checks. Set KISAK_AGENT_SESSION for live browser diagnostics checks.
import assert from "node:assert/strict";
import { test } from "node:test";
import fs from "node:fs";
import path from "node:path";
import net from "node:net";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";
import { CDP, sleep } from "./cdp-client.mjs";
const here = path.dirname(fileURLToPath(import.meta.url));
test("closed CDP sockets reject immediately without leaving pending requests",async()=>{
  const cdp=new CDP({readyState:WebSocket.CLOSED});
  await assert.rejects(cdp.send("Runtime.enable"),/not open/);
  assert.equal(cdp.pending.size,0);
});
test("failed CDP sends remove their timeout and pending request",async()=>{
  const cdp=new CDP({readyState:WebSocket.OPEN,send(){throw new Error("socket failed");}});
  await assert.rejects(cdp.send("Runtime.enable"),/socket failed/);
  assert.equal(cdp.pending.size,0);
});
async function withServer(flags, check) {
  const reserve = net.createServer();
  await new Promise(resolve => reserve.listen(0, "127.0.0.1", resolve));
  const port = reserve.address().port;
  await new Promise(resolve => reserve.close(resolve));
  const child = spawn(process.execPath, [path.join(here,"serve.mjs"),"--port",String(port),...flags],
    { cwd: path.resolve(here,"../.."), windowsHide: true, stdio: "ignore" });
  try {
    for (let i=0;i<50;++i) {
      try { const r = await fetch(`http://127.0.0.1:${port}/__agent/config`); await check(r,port); return; }
      catch (e) { if (e instanceof assert.AssertionError) throw e; await sleep(50); }
    }
    throw new Error("server startup timed out");
  } finally { child.kill(); }
}
test("ordinary server refuses agent config", () => withServer([], async r => {
  assert.equal(r.status,404); assert.deepEqual(await r.json(),{enabled:false});
}));
test("explicit local test server enables agent config without caching", () => withServer(["--agent-test"], async r => {
  assert.equal(r.status,200); assert.deepEqual(await r.json(),{enabled:true});
  assert.equal(r.headers.get("cache-control"),"no-store"); assert.equal(r.headers.get("access-control-allow-origin"),null);
}));
test("agent server refuses a public bind", async () => {
  const child = spawn(process.execPath,[path.join(here,"serve.mjs"),"--agent-test","--host","0.0.0.0"],{ windowsHide:true,stdio:"pipe" });
  let error=""; child.stderr.on("data",d=>error+=d);
  const code=await new Promise(resolve=>child.on("exit",resolve));
  assert.notEqual(code,0); assert.match(error,/requires a loopback/);
});

test("live page requires both the URL opt-in and the server flag", {skip:!process.env.KISAK_AGENT_SESSION}, async () => {
  const s=JSON.parse(fs.readFileSync(process.env.KISAK_AGENT_SESSION)), cdp=await CDP.connect(s.debugPort,s.browserUrl);
  async function probe(url) {
    const {targetId}=await cdp.send("Target.createTarget",{url});
    const sid=await cdp.attach(targetId);
    try {
      for(let i=0;i<100;++i) {
        const result=await cdp.evaluate("window.kisak?.cfg ? {enabled:window.kisak.cfg.agent,api:!!window.kisak.agent} : null",sid);
        if(result) return result;
        await sleep(50);
      }
      throw new Error("page did not initialize");
    } finally { await cdp.send("Target.closeTarget",{targetId}); }
  }
  try {
    const normal=new URL(s.url); normal.searchParams.delete("agent");
    assert.deepEqual(await probe(normal.href),{enabled:false,api:false});
    await withServer([],async (r,port)=> {
      assert.equal(r.status,404);
      assert.deepEqual(await probe(`http://127.0.0.1:${port}/?agent=1`),{enabled:false,api:false});
    });
  } finally { cdp.close(); }
});

test("live errors survive in the recorder; a stalled wait saves its last state", {skip:!process.env.KISAK_AGENT_SESSION}, async () => {
  const file=path.resolve(process.env.KISAK_AGENT_SESSION), s=JSON.parse(fs.readFileSync(file)), dir=path.dirname(file);
  const cdp=await CDP.connect(s.debugPort,s.browserUrl), sid=await cdp.attach(s.targets.host);
  try {
    const marker=`agent-diagnostic-${Date.now()}`;
    await cdp.evaluate(`(() => {
      setTimeout(() => { throw new Error(${JSON.stringify(marker+"-page")}); },100);
      window.__agentWorker=new Worker(URL.createObjectURL(new Blob([
        ${JSON.stringify(`setTimeout(()=>{console.error('${marker}-worker');throw new Error('${marker}-worker');},750);`)}
      ],{type:'text/javascript'})));
    })()`,sid);
    await sleep(2000);
    const record=fs.readFileSync(path.join(dir,"browser.jsonl"),"utf8");
    assert.match(record,new RegExp(marker+"-page")); assert.match(record,new RegExp(marker+"-worker"));
    const logs=await cdp.evaluate("window.kisak.agent.logs()",sid);
    assert.ok(logs.records.some(r=>r.text.includes(marker+"-page")));
    await cdp.send("Page.reload",{},sid);
    // Starting/loading cannot satisfy active-game state, regardless of machine speed.
    const child=spawn(process.execPath,[path.join(here,"cdp.mjs"),"wait","game","--timeout","250","--session",file],{windowsHide:true,stdio:"pipe"});
    let stderr=""; child.stderr.on("data",d=>stderr+=d);
    const code=await new Promise(resolve=>child.on("exit",resolve));
    assert.equal(code,1); assert.match(stderr,/Timed out waiting.*Last game state/);
    assert.ok(fs.existsSync(path.join(dir,"failure.png")));
    const failure=JSON.parse(fs.readFileSync(path.join(dir,"failure.json")));
    assert.ok(failure.state.status); assert.match(failure.error,/Timed out/);
  } finally { cdp.close(); }
});
