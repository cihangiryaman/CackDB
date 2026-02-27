/*
 * HTTP-to-RESP bridge for the inMemDb test page.
 * Accepts POST /cmd with { "args": ["SET","key","val"] }
 * and forwards as RESP to inMemDb on localhost:6399.
 *
 * Also serves the static HTML file.
 *
 * Usage: node proxy.js [--db-port 6399] [--http-port 8080]
 */
const net = require("net");
const http = require("http");
const fs = require("fs");
const path = require("path");

const DB_PORT = parseInt(process.argv.find((_, i, a) => a[i - 1] === "--db-port") || "6399");
const HTTP_PORT = parseInt(process.argv.find((_, i, a) => a[i - 1] === "--http-port") || "8080");

/* Build a RESP array from an array of strings */
function buildRESP(args) {
  let out = `*${args.length}\r\n`;
  for (const a of args) {
    const s = String(a);
    out += `$${Buffer.byteLength(s)}\r\n${s}\r\n`;
  }
  return out;
}

/* Minimal RESP parser – returns { value, consumed } or null */
function parseRESP(buf, off = 0) {
  if (off >= buf.length) return null;
  const crlf = buf.indexOf("\r\n", off);
  if (crlf === -1) return null;
  const line = buf.slice(off, crlf);
  const after = crlf + 2;

  switch (line[0]) {
    case "+": return { value: line.slice(1), consumed: after };
    case "-": return { value: `(error) ${line.slice(1)}`, consumed: after };
    case ":": return { value: `(integer) ${line.slice(1)}`, consumed: after };
    case "$": {
      const len = parseInt(line.slice(1));
      if (len === -1) return { value: "(nil)", consumed: after };
      const end = after + len + 2;
      if (end > buf.length) return null;
      return { value: `"${buf.slice(after, after + len)}"`, consumed: end };
    }
    case "*": {
      const count = parseInt(line.slice(1));
      if (count === -1) return { value: "(nil)", consumed: after };
      let pos = after;
      const items = [];
      for (let i = 0; i < count; i++) {
        const r = parseRESP(buf, pos);
        if (!r) return null;
        items.push(`${i + 1}) ${r.value}`);
        pos = r.consumed;
      }
      return { value: items.join("\n"), consumed: pos };
    }
    default: return { value: line, consumed: after };
  }
}

/* Send a RESP command and collect the response */
function query(args) {
  return new Promise((resolve, reject) => {
    const sock = net.createConnection({ host: "127.0.0.1", port: DB_PORT }, () => {
      sock.write(buildRESP(args));
    });
    let data = "";
    sock.on("data", (chunk) => {
      data += chunk.toString();
      const r = parseRESP(data);
      if (r) { sock.destroy(); resolve(r.value); }
    });
    sock.on("error", (e) => reject(e));
    sock.on("end", () => {
      if (data) { const r = parseRESP(data); resolve(r ? r.value : data); }
      else reject(new Error("connection closed"));
    });
    setTimeout(() => { sock.destroy(); resolve(data || "(timeout)"); }, 3000);
  });
}

const server = http.createServer(async (req, res) => {
  /* CORS */
  res.setHeader("Access-Control-Allow-Origin", "*");
  res.setHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
  res.setHeader("Access-Control-Allow-Headers", "Content-Type");
  if (req.method === "OPTIONS") { res.writeHead(204); res.end(); return; }

  /* Serve HTML */
  if (req.method === "GET" && (req.url === "/" || req.url === "/index.html")) {
    const html = fs.readFileSync(path.join(__dirname, "index.html"), "utf-8");
    res.writeHead(200, { "Content-Type": "text/html" });
    res.end(html);
    return;
  }

  /* Command endpoint */
  if (req.method === "POST" && req.url === "/cmd") {
    let body = "";
    req.on("data", (c) => (body += c));
    req.on("end", async () => {
      try {
        const { args } = JSON.parse(body);
        const result = await query(args);
        res.writeHead(200, { "Content-Type": "application/json" });
        res.end(JSON.stringify({ ok: true, result }));
      } catch (e) {
        res.writeHead(500, { "Content-Type": "application/json" });
        res.end(JSON.stringify({ ok: false, result: e.message }));
      }
    });
    return;
  }

  res.writeHead(404);
  res.end("Not found");
});

server.listen(HTTP_PORT, () => {
  console.log(`Bridge listening on http://localhost:${HTTP_PORT}`);
  console.log(`Forwarding to inMemDb on port ${DB_PORT}`);
});
