# Error report server / Servidor de informes de errores (Cloudflare Worker)

**[English](#english) · [Español](#español)**

---

## English

The HyperPlay app always keeps errors on the phone (`files/diag/`). If the user agrees, it sends
them to this Worker, which creates a GitHub **Issue** labeled `auto-report`. The GitHub token
lives only in Cloudflare: the APK carries no secret.

### Setup (once)

1. **GitHub token** (fine-grained, least privilege):
   GitHub → Settings → Developer settings → Fine-grained tokens → *Generate new token*.
   - Repository access: *Only select repositories* → `HyperPlay`.
   - Permissions → Repository → **Issues: Read and write** (nothing else).
2. Free **Cloudflare account**: <https://dash.cloudflare.com/sign-up>.
3. In this folder:
   ```bash
   npx wrangler login
   npx wrangler secret put GITHUB_TOKEN      # paste the token from step 1
   npx wrangler deploy
   ```
   Wrangler prints the URL, for example `https://hyperplay-reports.<your-account>.workers.dev`.
4. Check that it answers: open the URL in a browser → `HyperPlay report worker OK`.
5. Tell the app where to send: GitHub → repo → Settings → Secrets and variables → Actions →
   **Variables** → `HP_REPORT_URL` = `https://hyperplay-reports.<your-account>.workers.dev/report`.
   The next published versions will carry that URL.
6. Per-IP limit (20 reports per hour) and de-duplication use a KV namespace. The official server
   has one (id in `wrangler.toml`); for your own copy create it with
   `npx wrangler kv namespace create REPORTS_KV` and replace the id.

**Status:** the official server is deployed at
`https://hyperplay-reports.cespinosag1992.workers.dev` and `HP_REPORT_URL` is set in the repo.

### What is sent

Type (app `crash`, unexpected session end `cierre`, `juego` that exited with an error), app
version, phone model, Android version, the error details and the last lines of the game log, the
app log and the app's own logcat. No saves, accounts or personal files.

---

## Español

La app HyperPlay guarda siempre los errores en el teléfono (`files/diag/`). Si el usuario lo
acepta, los envía a este Worker, que crea un **Issue** en GitHub con la etiqueta `auto-report`.
El token de GitHub vive solo en Cloudflare: la APK no lleva ningún secreto.

### Puesta en marcha (una sola vez)

1. **Token de GitHub** (fine-grained, mínimo privilegio):
   GitHub → Settings → Developer settings → Fine-grained tokens → *Generate new token*.
   - Repository access: *Only select repositories* → `HyperPlay`.
   - Permissions → Repository → **Issues: Read and write** (nada más).
2. **Cuenta de Cloudflare** gratuita: <https://dash.cloudflare.com/sign-up>.
3. En esta carpeta:
   ```bash
   npx wrangler login
   npx wrangler secret put GITHUB_TOKEN      # pega el token del paso 1
   npx wrangler deploy
   ```
   Wrangler imprime la URL, por ejemplo `https://hyperplay-reports.<tu-cuenta>.workers.dev`.
4. Comprueba que responde: abre la URL en el navegador → `HyperPlay report worker OK`.
5. Dile a la app dónde enviar: en GitHub → repo → Settings → Secrets and variables → Actions →
   **Variables** → `HP_REPORT_URL` = `https://hyperplay-reports.<tu-cuenta>.workers.dev/report`.
   Las siguientes versiones publicadas llevarán esa URL.
6. El límite por IP (20 informes por hora) y el deduplicado usan un espacio KV. El servidor
   oficial ya tiene uno (id en `wrangler.toml`); para tu propia copia créalo con
   `npx wrangler kv namespace create REPORTS_KV` y cambia el id.

**Estado:** el servidor oficial está publicado en
`https://hyperplay-reports.cespinosag1992.workers.dev` y `HP_REPORT_URL` está configurada en el repo.

### Qué se envía

Tipo (`crash` de la app, `cierre` inesperado de una sesión, `juego` que terminó con error),
versión de la app, modelo del teléfono, versión de Android, los detalles del error y las últimas
líneas del log del juego, del registro de la app y del logcat de la propia app. No incluye partidas,
cuentas ni archivos personales.
