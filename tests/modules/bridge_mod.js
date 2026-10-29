// Used by tests/bridge_test.glow.
exports.double = (x) => x * 2;
exports.later = async (x) => { await new Promise((r) => setTimeout(r, 5)); return x + 1; };
exports.Box = class Box { constructor(v) { this.v = v; } get() { return this.v; } };
