# Publicar una versión

## Una vez: clave de firma y secretos

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

## Cada versión

1. Actualiza `CHANGELOG.md`.
2. Etiqueta y sube:
   ```bash
   git tag v1.2.3
   git push origin v1.2.3
   ```
3. El workflow **Release** compila el APK firmado (`versionCode` = 10000·mayor + 100·menor +
   parche), publica `HyperPlay-v1.2.3.apk` y su `.sha256` en la Release.
4. En los teléfonos, HyperPlay lo detecta (al abrirse, como mucho cada 6 h, o con
   **Ajustes → Buscar actualizaciones**), lo descarga, comprueba el SHA-256 e instala por encima.

## Qué conserva una actualización

Se instala sobre el mismo paquete con la misma firma: se mantienen ajustes, carpetas, arte de la
biblioteca, informes pendientes y todo lo de `/sdcard` (partidas en `_HyperPlaySaves`). La caché
del traductor de cada juego (`_HyperPlaySaves/hypercache`) va ligada al build: con una versión
nueva se invalida sola y se rehace en el primer arranque (no se mezcla código de dos versiones).
No se actualiza mientras hay un juego en pausa en memoria.
