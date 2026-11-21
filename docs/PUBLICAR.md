# Releasing a version / Publicar una versión

**[English](#english) · [Español](#español)**

---

## English

### Once: signing key and secrets

The release key is in `%USERPROFILE%\.hyperplay\hyperplay-release.jks` and its data in
`%USERPROFILE%\.hyperplay\keystore.properties`. There are copies in the local project, ignored by
git: `signing/` (key + data) and `keystore.properties` in the root (used by Gradle).

> **Back up that folder** (USB, private cloud). If the key is lost, new versions can no longer be
> installed over the old ones: every user would have to uninstall.

Repository secrets (Settings → Secrets and variables → Actions):

| Name | Type | Value |
|---|---|---|
| `HP_KEYSTORE_BASE64` | secret | the `.jks` in base64 |
| `HP_KEYSTORE_PASSWORD` | secret | `storePassword` |
| `HP_KEY_ALIAS` | secret | `hyperplay` |
| `HP_KEY_PASSWORD` | secret | `keyPassword` |
| `HP_REPORT_URL` | variable | report Worker URL + `/report` (optional) |

### Each version

1. Update `CHANGELOG.md` (English and Spanish, under `## X.Y.Z`).
2. Tag and push:
   ```bash
   git tag v1.2.3
   git push origin v1.2.3
   ```
3. The **Release** workflow builds the signed APK (`versionCode` = 10000·major + 100·minor +
   patch) and publishes `HyperPlay-v1.2.3-Snapdragon8Elite-arm64-v8a.apk` (the name always says
   the processor), its `.sha256` and the changelog section as notes.
4. On phones, HyperPlay detects it (on launch, at most every 6 h, or with
   **Settings → Check for updates**), downloads it, checks the SHA-256 and installs it on top.

### What an update keeps

It installs over the same package with the same signature: settings, folders, library art,
pending reports and everything on `/sdcard` (saves in `_HyperPlaySaves`) are kept. Each game's
translator cache (`_HyperPlaySaves/hypercache`) is tied to the build: a new version invalidates it
and it is rebuilt on first launch (code from two versions is never mixed). No update runs while a
game is paused in memory.

---

## Español

### Una vez: clave de firma y secretos

La clave de release está en `%USERPROFILE%\.hyperplay\hyperplay-release.jks` y sus datos en
`%USERPROFILE%\.hyperplay\keystore.properties`. Hay copias en el proyecto local, ignoradas por git:
`signing/` (clave + datos) y `keystore.properties` en la raíz (lo usa Gradle).

> **Haz una copia de seguridad de esa carpeta** (USB, nube privada). Si la clave se pierde, las
> nuevas versiones ya no se podrán instalar encima de las anteriores: cada usuario tendría que
> desinstalar.

Secretos del repositorio (Settings → Secrets and variables → Actions):

| Nombre | Tipo | Valor |
|---|---|---|
| `HP_KEYSTORE_BASE64` | secreto | el `.jks` en base64 |
| `HP_KEYSTORE_PASSWORD` | secreto | `storePassword` |
| `HP_KEY_ALIAS` | secreto | `hyperplay` |
| `HP_KEY_PASSWORD` | secreto | `keyPassword` |
| `HP_REPORT_URL` | variable | URL del Worker de informes + `/report` (opcional) |

### Cada versión

1. Actualiza `CHANGELOG.md` (inglés y español, bajo `## X.Y.Z`).
2. Etiqueta y sube:
   ```bash
   git tag v1.2.3
   git push origin v1.2.3
   ```
3. El workflow **Release** compila el APK firmado (`versionCode` = 10000·mayor + 100·menor +
   parche) y publica `HyperPlay-v1.2.3-Snapdragon8Elite-arm64-v8a.apk` (el nombre siempre indica el
   procesador), su `.sha256` y la sección del changelog como notas.
4. En los teléfonos, HyperPlay lo detecta (al abrirse, como mucho cada 6 h, o con
   **Ajustes → Buscar actualizaciones**), lo descarga, comprueba el SHA-256 e instala por encima.

### Qué conserva una actualización

Se instala sobre el mismo paquete con la misma firma: se mantienen ajustes, carpetas, arte de la
biblioteca, informes pendientes y todo lo de `/sdcard` (partidas en `_HyperPlaySaves`). La caché
del traductor de cada juego (`_HyperPlaySaves/hypercache`) va ligada al build: con una versión
nueva se invalida sola y se rehace en el primer arranque (no se mezcla código de dos versiones).
No se actualiza mientras hay un juego en pausa en memoria.
