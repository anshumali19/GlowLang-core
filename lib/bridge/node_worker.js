// GlowLang bridge worker for Node.js (started by lib/bridge.glow).
//
// Reads one JSON request per line on stdin and writes one JSON reply per line
// on stdout, in the same protocol as python_worker.py:
//
//   {"id": 1, "op": "call", "target": "path.join", "args": ["a", "b"]}
//   {"id": 1, "ok": "a/b"}
//   {"id": 2, "error": {"type": "TypeError", "message": "...", "traceback": "..."}}
//
// Values JSON can carry are sent as they are; anything else (functions,
// class instances, Buffers, Maps, ...) stays here and is sent as a reference,
// {"$ref": id, "type": "...", "repr": "..."}. Promises are awaited, so async
// functions return their result. Requests run one at a time, in order.
"use strict";

const PROTOCOL = 1;
const path = require("path");
const util = require("util");
const vm = require("vm");
const { createRequire } = require("module");
const { pathToFileURL } = require("url");

// Replies use the real stdout; everything else written to stdout (console.log,
// libraries) goes to stderr so it cannot corrupt the protocol.
const protoWrite = process.stdout.write.bind(process.stdout);
process.stdout.write = process.stderr.write.bind(process.stderr);

// Packages and relative paths resolve from the folder GlowLang runs in.
const userRequire = createRequire(path.join(process.cwd(), "__glowlang__.js"));
const refs = new Map();
let nextRef = 1;

class BridgeError extends Error {
  constructor(message) { super(message); this.name = "BridgeError"; }
}

function typeName(v) {
  if (v === null) return "null";
  if (typeof v === "function") return v.name ? `function ${v.name}` : "function";
  if (typeof v === "object") return (v.constructor && v.constructor.name) || "Object";
  return typeof v;
}

function makeRef(value) {
  const id = nextRef++;
  refs.set(id, value);
  let text;
  try { text = util.inspect(value, { depth: 1, breakLength: Infinity }); } catch (e) { text = `<${typeName(value)}>`; }
  if (text.length > 200) text = text.slice(0, 197) + "...";
  return { $ref: id, type: typeName(value), repr: text };
}

function isPlainObject(v) {
  if (v === null || typeof v !== "object") return false;
  const proto = Object.getPrototypeOf(v);
  return proto === Object.prototype || proto === null;
}

// JS value -> JSON data, keeping non-JSON values as refs.
function encode(v, depth = 0) {
  if (depth > 200) return makeRef(v);
  if (v === undefined || v === null) return null;
  if (typeof v === "boolean" || typeof v === "string") return v;
  if (typeof v === "number") return Number.isFinite(v) ? v : null;
  if (typeof v === "bigint") return Number.isSafeInteger(Number(v)) ? Number(v) : v.toString();
  if (Array.isArray(v)) return v.map((x) => encode(x, depth + 1));
  if (isPlainObject(v)) {
    const out = {};
    for (const k of Object.keys(v)) out[k] = encode(v[k], depth + 1);
    return out;
  }
  return makeRef(v);
}

// JSON data from GlowLang -> JS values, resolving refs.
function decode(v) {
  if (Array.isArray(v)) return v.map(decode);
  if (v !== null && typeof v === "object") {
    if ("$ref" in v) {
      if (!refs.has(v.$ref)) throw new BridgeError(`reference ${v.$ref} was released or never existed`);
      return refs.get(v.$ref);
    }
    const out = {};
    for (const k of Object.keys(v)) out[k] = decode(v[k]);
    return out;
  }
  return v;
}

// Fully convert a value to JSON data (for ref.value()), or fail.
function plain(v, depth = 0) {
  if (depth > 200) throw new BridgeError("value is nested too deeply to convert");
  if (v === undefined || v === null) return null;
  if (typeof v === "boolean" || typeof v === "string") return v;
  if (typeof v === "number") return Number.isFinite(v) ? v : null;
  if (typeof v === "bigint") return Number.isSafeInteger(Number(v)) ? Number(v) : v.toString();
  if (v instanceof Date) return v.toISOString();
  if (Buffer.isBuffer(v)) return v.toString("utf8");
  if (v instanceof Map) {
    const out = {};
    for (const [k, x] of v) out[String(k)] = plain(x, depth + 1);
    return out;
  }
  if (Array.isArray(v) || ArrayBuffer.isView(v) || v instanceof Set) return Array.from(v, (x) => plain(x, depth + 1));
  if (typeof v === "function") throw new BridgeError(`cannot convert ${typeName(v)} to a GlowLang value`);
  if (typeof v === "object") {
    if (typeof v.toJSON === "function") return plain(v.toJSON(), depth + 1);
    const out = {};
    for (const k of Object.keys(v)) out[k] = plain(v[k], depth + 1);
    return out;
  }
  throw new BridgeError(`cannot convert ${typeName(v)} to a GlowLang value`);
}

// Load a package or file: require first, import() for ES modules. A bare
// name that is not a package is also looked for as a file in the folder
// GlowLang runs in ("mymod" finds ./mymod.js), as Python finds mymod.py.
async function loadModule(name) {
  const spec = name.startsWith(".") ? path.resolve(name) : name;
  try {
    return await requireOrImport(spec);
  } catch (e) {
    const local = path.resolve(name);
    if (e && e.code === "MODULE_NOT_FOUND" && spec === name && !path.isAbsolute(name) && String(e.message).includes(`'${name}'`)) {
      try { return await requireOrImport(local); } catch (e2) { if (!e2 || e2.code !== "MODULE_NOT_FOUND") throw e2; }
    }
    throw e;
  }
}

async function requireOrImport(spec) {
  try {
    return userRequire(spec);
  } catch (e) {
    if (e && (e.code === "ERR_REQUIRE_ESM" || e.code === "ERR_REQUIRE_ASYNC_MODULE")) {
      const url = path.isAbsolute(spec) ? pathToFileURL(spec).href : spec;
      return await import(url);
    }
    throw e;
  }
}

// Find a dotted name: a global (including names defined with exec), or an
// attribute of a package ("path.join", "fs.promises.readFile"). Returns the
// value and the object it belongs to (the `this` for calls).
async function resolve(name) {
  if (!name) throw new BridgeError("empty name");
  const parts = name.split(".");
  let obj;
  if (parts[0] in globalThis) obj = globalThis[parts[0]];
  else obj = await loadModule(parts[0]);
  let owner;
  for (const attr of parts.slice(1)) {
    if (obj === null || obj === undefined) throw new BridgeError(`name not found: ${name}`);
    owner = obj;
    obj = obj[attr];
  }
  return { value: obj, owner };
}

async function targetOf(req) {
  const name = req.target || "";
  if (req.ref !== null && req.ref !== undefined) {
    let obj = decode({ $ref: req.ref });
    let owner;
    for (const attr of name ? name.split(".") : []) {
      owner = obj;
      obj = obj[attr];
    }
    return { value: obj, owner };
  }
  return resolve(name);
}

async function handle(req) {
  switch (req.op) {
    case "call": {
      if (req.kwargs && Object.keys(req.kwargs).length) throw new BridgeError("JavaScript functions take no keyword arguments; pass a dict as an argument instead");
      const { value: fn, owner } = await targetOf(req);
      if (typeof fn !== "function") throw new BridgeError(`${req.target || "value"} is not a function`);
      const args = decode(req.args || []);
      let result;
      // Classes need `new`.
      if (/^class[\s{]/.test(Function.prototype.toString.call(fn))) result = new fn(...args);
      else result = fn.apply(owner, args);
      return encode(await result);
    }
    case "get": {
      const { value } = await targetOf(req);
      return encode(await value);
    }
    case "set": {
      const name = req.target || "";
      const value = decode(req.value);
      if (!name.includes(".") && (req.ref === null || req.ref === undefined)) { globalThis[name] = value; return null; }
      const cut = name.lastIndexOf(".");
      const { value: obj } = await targetOf({ ref: req.ref, target: cut < 0 ? "" : name.slice(0, cut) });
      obj[cut < 0 ? name : name.slice(cut + 1)] = value;
      return null;
    }
    case "eval":
      return encode(await vm.runInThisContext(req.code));
    case "exec":
      vm.runInThisContext(req.code);
      return null;
    case "load":
      return makeRef(await loadModule(req.module));
    case "value":
      return plain(decode({ $ref: req.ref }));
    case "release":
      for (const id of req.refs || []) refs.delete(id);
      return null;
    case "ping":
      return "pong";
    default:
      throw new BridgeError(`unknown operation: ${req.op}`);
  }
}

function reply(obj) {
  let text;
  try { text = JSON.stringify(obj); } catch (e) { text = JSON.stringify({ id: obj.id, ok: makeRef(obj.ok) }); }
  protoWrite(text + "\n");
}

// Global functions and vars made by exec (`function f() {}`) become globals.
globalThis.require = userRequire;

let queue = Promise.resolve();
const rl = require("readline").createInterface({ input: process.stdin, crlfDelay: Infinity });
rl.on("line", (line) => {
  if (!line.trim()) return;
  queue = queue.then(async () => {
    let id = null;
    try {
      const req = JSON.parse(line);
      id = req.id;
      reply({ id, ok: await handle(req) });
    } catch (e) {
      const err = e instanceof Error ? e : new Error(String(e));
      reply({ id, error: { type: err.name || "Error", message: err.message, traceback: err.stack || "" } });
    }
  });
});
rl.on("close", () => { queue.then(() => process.exit(0)); });
process.on("unhandledRejection", (e) => { process.stderr.write(`unhandled rejection in bridge code: ${e && e.stack || e}\n`); });

reply({ ready: true, language: "javascript", protocol: PROTOCOL, version: process.versions.node });
