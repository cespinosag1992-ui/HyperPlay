// Segunda capa de redacción (la primera está en la app: diag/ReportRedactor.kt).
// Los informes se convierten en Issues de GitHub; todo lo que llegue aquí debe poder ser público.
// Si cambias una regla, cambia también la de la app.

const HEX = "[0-9A-Fa-f]";
const VALUE = String.raw`(\s*[:=]\s*["']?)[^\s"',;&)}\]]+`;

function luhn(digits) {
  let sum = 0;
  for (let i = 0; i < digits.length; i++) {
    let d = digits.charCodeAt(digits.length - 1 - i) - 48;
    if (i % 2 === 1) { d *= 2; if (d > 9) d -= 9; }
    sum += d;
  }
  return sum % 10 === 0;
}

function keepTail(rest) {
  const parts = rest.split("/").filter(Boolean);
  if (parts.length === 0) return "";
  if (parts.length <= 2) return "/" + parts.join("/");
  return "/…/" + parts.slice(-2).join("/");
}

const re = (s) => new RegExp(s, "gi");

const RULES = [
  [re(String.raw`\b(?:gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,})`), "<token>"],
  [re(String.raw`\bxox[abprs]-[A-Za-z0-9-]{10,}`), "<token>"],
  [re(String.raw`\bAKIA[0-9A-Z]{16}\b`), "<token>"],
  [re(String.raw`\bAIza[0-9A-Za-z_-]{35}`), "<token>"],
  [re(String.raw`\beyJ[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]{8,}`), "<token>"],
  [re(String.raw`\b(Bearer|Basic)\s+[A-Za-z0-9._~+/=-]{8,}`), (_m, k) => `${k} <token>`],
  [re(String.raw`\b(access_?token|refresh_?token|auth_?token|token|password|passwd|pwd|secret|api_?key|access_?key|client_?secret|session_?id|cookie)` + VALUE),
    (_m, k, sep) => `${k}${sep}<token>`],
  [re(String.raw`\b(android_?id|ssaid|ro\.serialno|ro\.boot\.serialno|serial(?:_?no|_?number)?|imei|meid|device_?id|advertising_?id|gaid|adid)` + VALUE),
    (_m, k, sep) => `${k}${sep}<device-id>`],
  [re(String.raw`\b(account_?name|account|user_?name|username|login|steam_?user|persona_?name)` + VALUE),
    (_m, k, sep) => `${k}${sep}<account>`],
  [re(String.raw`\b[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}\b`), "<email>"],
  [re(String.raw`(?:/storage/emulated/\d+|/storage/self/primary|/storage/[0-9A-F]{4}-[0-9A-F]{4}|/mnt/user/\d+/[^/\s]+|/(?:mnt/)?sdcard)((?:/[^/\\\r\n"'<>|:;,()\[\]{}=]+)*)`),
    (_m, rest) => "<storage>" + keepTail(rest)],
  [re(String.raw`\b([A-Z]:\\(?:Users|Documents and Settings)\\)[^\\/\r\n"'<>|:]+`), (_m, p) => `${p}<user>`],
  [re(String.raw`(?<![\w.])(/(?:home|Users))/[^/\s"'<>|:]+`), (_m, p) => `${p}/<user>`],
  [re(String.raw`(?<![\w:-])(?:${HEX}{2}[:-]){5}${HEX}{2}(?![\w:-])`), "<mac>"],
  [re(String.raw`(?<![\w.])(?:25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)(?:\.(?:25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)){3}(?![\w.])`),
    (m) => (m === "0.0.0.0" || m.startsWith("127.") ? m : "<ip>")],
  [re(String.raw`(?<![\w:])(?:${HEX}{1,4}:){7}${HEX}{1,4}(?![\w:])`), "<ip>"],
  [re(String.raw`(?<![\w:])(?:${HEX}{1,4}(?::${HEX}{1,4}){0,6})?::(?:${HEX}{1,4}(?::${HEX}{1,4}){0,6})?(?![\w:])`),
    (m) => (m === "::1" || !/\d/.test(m) || m.length < 3 ? m : "<ip>")],
  [re(String.raw`(?<!\d)7656119\d{10}(?!\d)`), "<steam-id>"],
  [re(String.raw`(?<![\d.])\d{15}(?![\d.])`), (m) => (luhn(m) ? "<device-id>" : m)],
];

export function redact(text) {
  let out = String(text ?? "");
  if (!out) return out;
  for (const [regex, replacement] of RULES) out = out.replace(regex, replacement);
  return out;
}

// Campos sin texto libre que pasan tal cual.
const PASS_THROUGH = new Set(["id", "kind", "appVersion", "appVersionCode", "time"]);

export function redactReport(report) {
  const out = {};
  for (const [k, v] of Object.entries(report)) {
    out[k] = !PASS_THROUGH.has(k) && typeof v === "string" ? redact(v) : v;
  }
  return out;
}
