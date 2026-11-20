// HyperPlay — receptor de informes de errores.
// La app envía POST /report con un JSON (ver app/.../diag/Diagnostics.kt) y este Worker crea un
// Issue en el repositorio con la etiqueta "auto-report". El token de GitHub vive solo aquí
// (secreto GITHUB_TOKEN), nunca dentro de la APK.
//
// Variables (wrangler.toml / secretos):
//   GITHUB_REPO   "propietario/repositorio"
//   GITHUB_TOKEN  token fine-grained con permiso Issues: Read and write SOLO en ese repo
//   REPORTS_KV    (opcional) espacio KV para limitar envíos por IP y evitar duplicados

const MAX_BODY = 256 * 1024;          // tamaño máximo del JSON recibido
const MAX_ISSUE = 60000;              // GitHub admite 65 536 caracteres por cuerpo
const PER_IP_PER_HOUR = 20;
const KINDS = new Set(["crash", "cierre", "juego", "manual"]);

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (request.method === "GET" && url.pathname === "/") {
      return text("HyperPlay report worker OK");
    }
    if (request.method !== "POST" || url.pathname !== "/report") return text("not found", 404);

    const length = Number(request.headers.get("content-length") || "0");
    if (length > MAX_BODY) return text("too large", 413);
    let report;
    try {
      const raw = await request.text();
      if (raw.length > MAX_BODY) return text("too large", 413);
      report = JSON.parse(raw);
    } catch {
      return text("bad json", 400);
    }
    if (!report || typeof report.id !== "string" || !KINDS.has(report.kind)) return text("bad report", 400);

    // Límite por IP y deduplicado por id (si hay KV configurado).
    if (env.REPORTS_KV) {
      const ip = request.headers.get("cf-connecting-ip") || "unknown";
      const hour = Math.floor(Date.now() / 3600000);
      const key = `ip:${ip}:${hour}`;
      const count = Number((await env.REPORTS_KV.get(key)) || "0");
      if (count >= PER_IP_PER_HOUR) return text("rate limited", 429);
      await env.REPORTS_KV.put(key, String(count + 1), { expirationTtl: 3700 });
      if (await env.REPORTS_KV.get(`id:${report.id}`)) return text("duplicate", 200);
      await env.REPORTS_KV.put(`id:${report.id}`, "1", { expirationTtl: 60 * 60 * 24 * 30 });
    }

    const title = `[${report.kind}] ${clean(report.title, 150)} (v${clean(report.appVersion, 20)})`;
    const body = issueBody(report);
    const labels = ["auto-report", `kind:${report.kind}`];

    const gh = await fetch(`https://api.github.com/repos/${env.GITHUB_REPO}/issues`, {
      method: "POST",
      headers: {
        "Authorization": `Bearer ${env.GITHUB_TOKEN}`,
        "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28",
        "User-Agent": "hyperplay-report-worker",
        "Content-Type": "application/json",
      },
      body: JSON.stringify({ title, body, labels }),
    });
    if (!gh.ok) {
      const detail = await gh.text();
      console.log(`GitHub ${gh.status}: ${detail.slice(0, 500)}`);
      return text("upstream error", 502);
    }
    const issue = await gh.json();
    return new Response(JSON.stringify({ ok: true, issue: issue.number }), {
      headers: { "Content-Type": "application/json" },
    });
  },
};

function text(s, status = 200) {
  return new Response(s, { status, headers: { "Content-Type": "text/plain; charset=utf-8" } });
}

function clean(v, max) {
  return String(v ?? "").replace(/[\r\n]+/g, " ").slice(0, max);
}

// Bloque de código colapsable; evita que el contenido cierre el bloque.
function block(label, content, budget) {
  const s = String(content ?? "").replace(/```/g, "ˋˋˋ");
  if (!s.trim()) return "";
  const cut = s.length > budget ? "…(recortado)\n" + s.slice(s.length - budget) : s;
  return `\n<details><summary>${label}</summary>\n\n\`\`\`\n${cut}\n\`\`\`\n</details>\n`;
}

function issueBody(r) {
  const head = [
    `**Tipo:** ${clean(r.kind, 20)}`,
    `**Versión:** ${clean(r.appVersion, 20)} (${clean(r.appVersionCode, 12)})`,
    `**Dispositivo:** ${clean(r.device, 120)}`,
    `**Sistema:** ${clean(r.android, 60)}`,
    `**Hora (teléfono):** ${clean(r.time, 40)}`,
    `**ID:** \`${clean(r.id, 40)}\``,
    "",
    "### Detalles",
    "```",
    String(r.details ?? "").replace(/```/g, "ˋˋˋ").slice(0, 8000),
    "```",
  ].join("\n");
  let budget = MAX_ISSUE - head.length - 600;
  const game = block("Log del juego", r.gameLog, Math.floor(budget * 0.5));
  budget -= game.length;
  const app = block("Registro de la app", r.appLog, Math.floor(budget * 0.4));
  budget -= app.length;
  const logcat = block("logcat", r.logcat, Math.max(0, budget - 200));
  return (head + game + app + logcat + "\n_Informe automático enviado por la app HyperPlay._").slice(0, MAX_ISSUE);
}
