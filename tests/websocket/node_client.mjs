// Node's built-in WebSocket client (undici, RFC 6455) against the GlowLang server.
const port = process.argv[2];
let failures = 0;
const ok = (name, cond) => { if (!cond) { failures++; console.log("FAIL:", name); } else console.log("ok:", name); };

function open(path, protocols) {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(`ws://127.0.0.1:${port}${path}`, protocols);
    ws.binaryType = "arraybuffer";
    ws.queue = []; ws.waiters = [];
    const push = (item) => { if (ws.waiters.length) ws.waiters.shift()(item); else ws.queue.push(item); };
    ws.onmessage = (e) => push(e.data);
    ws.onclose = (e) => push({ closed: true, code: e.code, reason: e.reason });
    ws.onopen = () => resolve(ws);
    ws.onerror = () => reject(new Error("open failed"));
  });
}
function next(ws) {
  return new Promise((resolve) => {
    if (ws.queue.length) resolve(ws.queue.shift()); else ws.waiters.push(resolve);
  });
}

const ws = await open("/echo", ["chat.v2", "chat.v1"]);
ok("subprotocol chosen by the server's preference", ws.protocol === "chat.v1");
ws.send("hello from node");
ok("text echo", (await next(ws)) === "glow: hello from node");
const uni = "नमस्ते 🌍 ẞ";
ws.send(uni);
ok("unicode echo", (await next(ws)) === "glow: " + uni);
const bin = new Uint8Array(70000); for (let i = 0; i < bin.length; i++) bin[i] = (i * 7) & 255;
ws.send(bin);
const back = new Uint8Array(await next(ws));
ok("binary 70000 bytes (64-bit length)", back.length === 70000 && back.every((v, i) => v === ((i * 7) & 255)));
const mid = "m".repeat(300);
ws.send(mid);
ok("126..65535 length", (await next(ws)) === "glow: " + mid);
const big = "b".repeat(5 * 1024 * 1024);
ws.send(big);
ok("5 MiB text", (await next(ws)).length === big.length + 6);
for (let i = 0; i < 100; i++) ws.send("n" + i);
let all = true;
for (let i = 0; i < 100; i++) all = all && (await next(ws)) === "glow: n" + i;
ok("100 messages in order", all);
ws.send("close please");
const c = await next(ws);
ok("server closes with 4001 and a reason", c.closed && c.code === 4001 && c.reason === "bye from glow");

const ws2 = await open("/echo");
ok("no subprotocol when none offered", ws2.protocol === "");
ws2.close(1000, "client done");
const c2 = await next(ws2);
ok("client-initiated close echoed", c2.closed && c2.code === 1000);

// a request that is not an upgrade
const r = await fetch(`http://127.0.0.1:${port}/echo`);
ok("plain HTTP request refused with 400", r.status === 400);

console.log(failures === 0 ? "node: all passed" : `node: ${failures} failed`);
process.exit(failures ? 1 : 0);
