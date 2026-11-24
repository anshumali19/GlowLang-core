// The benchmark API with Node's built-in http module.
const http = require("http");
const NOTE = /^\/notes\/(\d+)$/;

http.createServer((req, res) => {
  const m = NOTE.exec(req.url);
  if (!m) { res.writeHead(404); res.end(); return; }
  const body = JSON.stringify({ id: Number(m[1]), text: "buy milk", done: false });
  res.writeHead(200, { "Content-Type": "application/json", "Content-Length": Buffer.byteLength(body) });
  res.end(body);
}).listen(Number(process.env.PORT || 18083), "127.0.0.1");
