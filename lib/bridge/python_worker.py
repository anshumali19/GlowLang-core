"""GlowLang bridge worker for Python (started by lib/bridge.glow).

Reads one JSON request per line on stdin and writes one JSON reply per line
on stdout:

    {"id": 1, "op": "call", "target": "math.sqrt", "args": [16], "kwargs": {}}
    {"id": 1, "ok": 4.0}
    {"id": 2, "error": {"type": "ValueError", "message": "...", "traceback": "..."}}

Values JSON can carry (None, bools, numbers, strings, lists, dicts with text
keys) are sent as they are. Anything else stays in this process and is sent
as a reference, {"$ref": id, "type": "...", "repr": "..."}, which GlowLang
can pass back, call methods on, or release.

Anything the user's code prints goes to stderr, so it can never mix with
replies.
"""
import builtins
import importlib
import json
import math
import os
import sys
import traceback

PROTOCOL = 1

# Replies use a private copy of stdout; fd 1 and sys.stdout now go to stderr,
# so print() and C extensions writing to stdout cannot corrupt the protocol.
_out = os.fdopen(os.dup(sys.stdout.fileno()), "w", encoding="utf-8", newline="\n")
os.dup2(sys.stderr.fileno(), sys.stdout.fileno())
sys.stdout = sys.stderr

# User modules are found in the folder GlowLang runs in.
sys.path.insert(0, os.getcwd())

namespace = {"__name__": "__glowlang__", "__builtins__": builtins}
refs = {}
next_ref = [1]


class BridgeError(Exception):
    pass


def make_ref(value):
    rid = next_ref[0]
    next_ref[0] += 1
    refs[rid] = value
    t = type(value)
    try:
        text = repr(value)
    except Exception:
        text = "<%s>" % t.__name__
    if len(text) > 200:
        text = text[:197] + "..."
    return {"$ref": rid, "type": "%s.%s" % (t.__module__, t.__qualname__), "repr": text}


def encode(value, depth=0):
    """Turn a Python value into JSON data, keeping non-JSON values as refs."""
    if depth > 200:
        return make_ref(value)
    if value is None or isinstance(value, (bool, str)):
        return value
    if isinstance(value, int):
        return value
    if isinstance(value, float):
        return value if math.isfinite(value) else None
    # Only plain lists, tuples and dicts are copied: subclasses (Counter,
    # OrderedDict, namedtuple) keep their methods by staying references.
    if type(value) in (list, tuple):
        return [encode(v, depth + 1) for v in value]
    if type(value) is dict:
        if all(isinstance(k, (str, int, float, bool)) for k in value):
            return {str(k): encode(v, depth + 1) for k, v in value.items()}
        return make_ref(value)
    # NumPy scalars and similar: plain numbers.
    item = getattr(value, "item", None)
    if callable(item) and type(value).__module__ == "numpy" and getattr(value, "ndim", 1) == 0:
        return encode(item(), depth + 1)
    return make_ref(value)


def decode(value):
    """Turn JSON data from GlowLang into Python values, resolving refs."""
    if isinstance(value, list):
        return [decode(v) for v in value]
    if isinstance(value, dict):
        if "$ref" in value:
            rid = value["$ref"]
            if rid not in refs:
                raise BridgeError("reference %s was released or never existed" % rid)
            return refs[rid]
        return {k: decode(v) for k, v in value.items()}
    return value


def plain(value, depth=0):
    """Convert a value fully to JSON data (for ref.value()), or fail."""
    if depth > 200:
        raise BridgeError("value is nested too deeply to convert")
    if value is None or isinstance(value, (bool, int, str)):
        return value
    if isinstance(value, float):
        return value if math.isfinite(value) else None
    tolist = getattr(value, "tolist", None)
    if callable(tolist):
        return plain(tolist(), depth + 1)
    if isinstance(value, (bytes, bytearray)):
        return value.decode("utf-8", "replace")
    if isinstance(value, dict) or hasattr(value, "items"):
        return {str(k): plain(v, depth + 1) for k, v in value.items()}
    if isinstance(value, (list, tuple, set, frozenset, range)) or hasattr(value, "__iter__"):
        return [plain(v, depth + 1) for v in value]
    raise BridgeError("cannot convert %s to a GlowLang value" % type(value).__name__)


def resolve(path):
    """Find a dotted name: a name defined with exec, a builtin, or a module
    attribute, importing the longest module prefix ("os.path.join")."""
    if not path:
        raise BridgeError("empty name")
    parts = path.split(".")
    if parts[0] in namespace:
        obj = namespace[parts[0]]
        rest = parts[1:]
    elif hasattr(builtins, parts[0]) and len(parts) == 1:
        return getattr(builtins, parts[0])
    else:
        obj = None
        rest = None
        for i in range(len(parts), 0, -1):
            name = ".".join(parts[:i])
            try:
                obj = importlib.import_module(name)
            except ModuleNotFoundError as e:
                # Only "this name is not a module"; a missing dependency
                # inside the module is a real error.
                if e.name and (name == e.name or name.startswith(e.name + ".")):
                    continue
                raise
            rest = parts[i:]
            break
        if obj is None:
            if hasattr(builtins, parts[0]):
                obj = getattr(builtins, parts[0])
                rest = parts[1:]
            else:
                raise BridgeError("name not found: %s" % path)
    for attr in rest:
        obj = getattr(obj, attr)
    return obj


def target_of(req):
    ref = req.get("ref")
    name = req.get("target") or ""
    if ref is not None:
        obj = decode({"$ref": ref})
        for attr in name.split(".") if name else []:
            obj = getattr(obj, attr)
        return obj
    return resolve(name)


def handle(req):
    op = req.get("op")
    if op == "call":
        fn = target_of(req)
        args = decode(req.get("args") or [])
        kwargs = decode(req.get("kwargs") or {})
        return encode(fn(*args, **kwargs))
    if op == "get":
        return encode(target_of(req))
    if op == "set":
        name = req.get("target") or ""
        if "." not in name and req.get("ref") is None:
            namespace[name] = decode(req.get("value"))
            return None
        owner, _, attr = name.rpartition(".")
        obj = target_of({"ref": req.get("ref"), "target": owner})
        setattr(obj, attr, decode(req.get("value")))
        return None
    if op == "eval":
        return encode(eval(req["code"], namespace))
    if op == "exec":
        exec(req["code"], namespace)
        return None
    if op == "load":
        module = importlib.import_module(req["module"])
        namespace[req["module"].split(".")[0]] = importlib.import_module(req["module"].split(".")[0])
        return make_ref(module)
    if op == "value":
        return plain(decode({"$ref": req["ref"]}))
    if op == "release":
        for rid in req.get("refs") or []:
            refs.pop(rid, None)
        return None
    if op == "ping":
        return "pong"
    raise BridgeError("unknown operation: %s" % op)


def reply(obj):
    _out.write(json.dumps(obj, ensure_ascii=False, allow_nan=False) + "\n")
    _out.flush()


def main():
    reply({"ready": True, "language": "python", "protocol": PROTOCOL,
           "version": "%d.%d.%d" % sys.version_info[:3]})
    for raw in sys.stdin.buffer:
        line = raw.decode("utf-8", "replace").strip()
        if not line:
            continue
        rid = None
        try:
            req = json.loads(line)
            rid = req.get("id")
            result = handle(req)
            try:
                out = json.dumps({"id": rid, "ok": result}, ensure_ascii=False, allow_nan=False)
            except (TypeError, ValueError):
                out = json.dumps({"id": rid, "ok": make_ref(result)}, ensure_ascii=False)
            _out.write(out + "\n")
            _out.flush()
        except SystemExit:
            raise
        except BaseException as e:  # report everything, including KeyboardInterrupt
            reply({"id": rid, "error": {
                "type": type(e).__name__,
                "message": str(e),
                "traceback": traceback.format_exc(),
            }})


if __name__ == "__main__":
    main()
