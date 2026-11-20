# Servidor de informes de errores (Cloudflare Worker)

La app HyperPlay guarda siempre los errores en el teléfono (`files/diag/`). Si el usuario lo
acepta, los envía a este Worker, que crea un **Issue** en GitHub con la etiqueta `auto-report`.
El token de GitHub vive solo en Cloudflare: la APK no lleva ningún secreto.

## Puesta en marcha (una sola vez)

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
6. (Opcional) límite por IP y deduplicado: crea el espacio KV como indica `wrangler.toml`.

## Qué se envía

Tipo (`crash` de la app, `cierre` inesperado de una sesión, `juego` que terminó con error),
versión de la app, modelo del teléfono, versión de Android, los detalles del error y las últimas
líneas del log del juego, del registro de la app y del logcat de la propia app. No incluye partidas,
cuentas ni archivos personales.
