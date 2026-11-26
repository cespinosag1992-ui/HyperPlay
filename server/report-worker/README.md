# Error report server / Servidor de informes de errores (Cloudflare Worker)

**[English](#english) · [Español](#español)**

---

## English

The Exedra64 app always keeps errors on the phone (`files/diag/`). If the user agrees, it sends
them to this Worker, which creates a GitHub **Issue** labeled `auto-report` in the repository set
in `REPORTS_REPO`. That repository should be **private** (for example `HyperPlay-Reports`): reports
contain logs from users' phones. The GitHub token lives only in Cloudflare: the APK carries no
secret.

### Privacy

Personal data is removed twice: in the app before sending (`diag/ReportRedactor.kt`) and here
before creating the Issue (`src/redact.js`, same rules). Replaced with placeholders: IPv4/IPv6
and MAC addresses, emails, user folders (`/storage/emulated/<n>/…`, `/sdcard/…`, SD card volumes,
`C:\Users\<name>`, `/home/<name>`), device serials, Android ID, IMEI-like numbers, SteamIDs,
account names, passwords and tokens. Of storage paths only the last two parts (game folder and
file) are kept. Tests: `node --test test/redact.test.js`.

### Setup (once)

1. **Private repository** for the reports, e.g. `HyperPlay-Reports` (GitHub → New repository →
   *Private*). Create the labels `auto-report`, `kind:crash`, `kind:cierre`, `kind:juego` and
   `kind:manual` (a token with only Issues permission cannot create labels).
2. **GitHub token** (fine-grained, least privilege):
   GitHub → Settings → Developer settings → Fine-grained tokens → *Generate new token*.
   - Repository access: *Only select repositories* → the reports repository only.
   - Permissions → Repository → **Issues: Read and write** (nothing else).
3. Free **Cloudflare account**: <https://dash.cloudflare.com/sign-up>.
4. Set `REPORTS_REPO` in `wrangler.toml` to `owner/repository`, then in this folder:
   ```bash
   npx wrangler login
   npx wrangler secret put GITHUB_TOKEN      # paste the token from step 2
   npx wrangler deploy
   ```
   Wrangler prints the URL, for example `https://hyperplay-reports.<your-account>.workers.dev`.
5. Check that it answers: open the URL in a browser → `Exedra64 report worker OK`.
6. Tell the app where to send: GitHub → repo → Settings → Secrets and variables → Actions →
   **Variables** → `HP_REPORT_URL` = `https://hyperplay-reports.<your-account>.workers.dev/report`.
   The next published versions will carry that URL.
7. Per-IP limit (20 reports per hour) and de-duplication use a KV namespace. The official server
   has one (id in `wrangler.toml`); for your own copy create it with
   `npx wrangler kv namespace create REPORTS_KV` and replace the id.

`GITHUB_REPO` (old name) is still read when `REPORTS_REPO` is missing.

**Status:** the official server is deployed at
`https://hyperplay-reports.cespinosag1992.workers.dev` and `HP_REPORT_URL` is set in the repo.

### What is sent

Type (app `crash`, unexpected session end `cierre`, `juego` that exited with an error), app
version, phone model, Android version, the error details and the last lines of the game log, the
app log and the app's own logcat, after removing personal data as described above. No saves,
accounts or personal files.

---

## Español

La app Exedra64 guarda siempre los errores en el teléfono (`files/diag/`). Si el usuario lo
acepta, los envía a este Worker, que crea un **Issue** con la etiqueta `auto-report` en el
repositorio indicado en `REPORTS_REPO`. Ese repositorio debe ser **privado** (por ejemplo
`HyperPlay-Reports`): los informes contienen logs de teléfonos de usuarios. El token de GitHub vive
solo en Cloudflare: la APK no lleva ningún secreto.

### Privacidad

Los datos personales se quitan dos veces: en la app antes de enviar (`diag/ReportRedactor.kt`) y
aquí antes de crear el Issue (`src/redact.js`, mismas reglas). Se sustituyen por marcadores:
direcciones IPv4/IPv6 y MAC, correos, carpetas de usuario (`/storage/emulated/<n>/…`,
`/sdcard/…`, volúmenes de tarjeta SD, `C:\Users\<nombre>`, `/home/<nombre>`), números de serie,
Android ID, números tipo IMEI, SteamID, nombres de cuenta, contraseñas y tokens. De las rutas de
almacenamiento solo se conservan las dos últimas partes (carpeta del juego y archivo). Tests:
`node --test test/redact.test.js`.

### Puesta en marcha (una sola vez)

1. **Repositorio privado** para los informes, p. ej. `HyperPlay-Reports` (GitHub → New repository
   → *Private*). Crea las etiquetas `auto-report`, `kind:crash`, `kind:cierre`, `kind:juego` y
   `kind:manual` (un token que solo tiene permiso de Issues no puede crear etiquetas).
2. **Token de GitHub** (fine-grained, mínimo privilegio):
   GitHub → Settings → Developer settings → Fine-grained tokens → *Generate new token*.
   - Repository access: *Only select repositories* → solo el repositorio de informes.
   - Permissions → Repository → **Issues: Read and write** (nada más).
3. **Cuenta de Cloudflare** gratuita: <https://dash.cloudflare.com/sign-up>.
4. Pon en `wrangler.toml` `REPORTS_REPO` = `propietario/repositorio` y, en esta carpeta:
   ```bash
   npx wrangler login
   npx wrangler secret put GITHUB_TOKEN      # pega el token del paso 2
   npx wrangler deploy
   ```
   Wrangler imprime la URL, por ejemplo `https://hyperplay-reports.<tu-cuenta>.workers.dev`.
5. Comprueba que responde: abre la URL en el navegador → `Exedra64 report worker OK`.
6. Dile a la app dónde enviar: en GitHub → repo → Settings → Secrets and variables → Actions →
   **Variables** → `HP_REPORT_URL` = `https://hyperplay-reports.<tu-cuenta>.workers.dev/report`.
   Las siguientes versiones publicadas llevarán esa URL.
7. El límite por IP (20 informes por hora) y el deduplicado usan un espacio KV. El servidor
   oficial ya tiene uno (id en `wrangler.toml`); para tu propia copia créalo con
   `npx wrangler kv namespace create REPORTS_KV` y cambia el id.

`GITHUB_REPO` (nombre antiguo) se sigue leyendo si falta `REPORTS_REPO`.

**Estado:** el servidor oficial está publicado en
`https://hyperplay-reports.cespinosag1992.workers.dev` y `HP_REPORT_URL` está configurada en el repo.

### Qué se envía

Tipo (`crash` de la app, `cierre` inesperado de una sesión, `juego` que terminó con error),
versión de la app, modelo del teléfono, versión de Android, los detalles del error y las últimas
líneas del log del juego, del registro de la app y del logcat de la propia app, después de quitar
los datos personales como se explica arriba. No incluye partidas, cuentas ni archivos personales.
