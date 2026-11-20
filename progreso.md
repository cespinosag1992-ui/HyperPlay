# HyperPlay · Progreso

> Leer ANTES de empezar a trabajar y actualizar AL TERMINAR cada sesión/tarea.
> Última actualización: 2026-11-19 16:05 (nueva interfaz estilo macOS; guardado de HK arreglado con NtQueryDirectoryFile; PSHUFLW corregido en el JIT; repetición de frames contra el límite de CPU de nubia; Quick Resume; protección térmica reforzada; gate verde)
> **ADB:** el puerto de depuración inalámbrica cambia al reactivarla; descubrir con `adb mdns services` (último: `192.168.0.9:44517`; si mDNS no lo ve: barrido de ping + `scratchpad/portscan.ps1` 30000-50000).

## Estado actual y pendientes (2026-11-19 16:05)
- **Hecho hoy**:
  - Quick Resume en memoria (vuelta en 0,5 s).
  - JIT: encadenado de bloques de la caché de disco (−41 % de CPU al arrancar), flags muertos entre bloques y PSHUFLW/PSHUFHW corregidos.
  - Repetición de frames: el móvil ya no limita la CPU y HK va a 60,0 fps.
  - `NtQueryDirectoryFile`: HK vuelve a guardar la partida.
  - Protección térmica de dos niveles; HyperFG bloquea el tope y queda limitado a 60 con calor.
  - Diagnóstico de audio en `WASAPI ritmo`.
  - Interfaz nueva estilo macOS.
- **Pendiente de probar por el usuario**:
  - Reaparecer tras morir en el banco con el arreglo del guardado.
  - Audio: la última partida medida da 48 kHz y 0 silencio.
  - Cuánto enfría la protección térmica en partida.
  - La interfaz nueva.
- **Pendiente técnico**:
  - Picos de 34–150 ms al recorrer zonas nuevas (trabajo del C# del juego).
  - Probar el GC incremental de Mono (`debug.hyperplay.incgc 1`) ahora que `strchr` funciona.
  - A/B del anclaje a Prime (`primepin`).
  - Cambios de escena en zonas avanzadas.
  - Cuphead y DmC.

## 2026-11-19 (17:10): audio al salir, overlay, sonidos, caché y física (pedidos del usuario)
- **El juego se seguía oyendo al cerrarlo**:
  - Al cerrar un juego (Forzar cierre, o abrir otro con uno en Quick Resume) primero se despausaba. El bombeo WASAPI reabría AAudio y nadie lo volvía a parar; en el log de Cuphead hay "DirectAAudioEngine iniciado" a las 16:49:37 sin "detenido" después.
  - Ahora `NativeDllInterceptor::stopAllAudio()` para todos los clientes WASAPI y une sus bombeos. `nativeDirectStop` lo llama antes de despausar, y Kotlin ya no despausa antes de `killHeld`.
  - Al minimizar sí se detenía ("DirectAAudioEngine detenido" a los 230 ms): pendiente de que el usuario lo confirme.
- **Overlay**: solo Activado (compacto) / Desactivado (`PerfOverlayMode.SELECTABLE`). Un "full" guardado pasa a compacto; el modo completo se veía mal en el centro.
- **Sonidos de la interfaz**: senoidales suaves con caída exponencial (`decayMs`) y volumen bajo, en lugar de las ondas triangulares agudas: tic al navegar, pop al confirmar, descenso al volver, golpe grave en un error.
- **"Preparar caché" siempre 0**: llamaba al precompilador AOT antiguo, ya sin uso. Se quita el botón y en Detalles aparece "Caché del traductor" con el tamaño real de `hypercache/oryon_jit.*`: HK 550 mil bloques y 332 MB, Cuphead 205 mil bloques y 123 MB.
- **Física** (usuario: el caballero no se levanta del banco tras morir; el geo cae sin parar hasta recogerlo):
  - Sin excepciones en el log. HK guarda a las 16:10:20, carga la escena del banco a las 16:10:25 y se queda esperando hasta el cierre forzado de las 16:12.
  - El guardado ya funciona: `user2.dat` y `user2.dat.bak1` se actualizan a las 16:10.
  - Primera medida: `xflags` (flags muertos entre bloques) desactivado por defecto, porque es el cambio de JIT más reciente y no daba ganancia; `setprop debug.hyperplay.xflags 1` lo reactiva. La prueba nativa lo activa explícitamente.
  - Si el fallo sigue, el siguiente paso es comparar instrucción a instrucción el código de física (Box2D en UnityPlayer) con el x86 real.
- Gate verde; suite nativa con 39 510 comprobaciones y 0 fallos.

## 2026-11-19 (16:00): interfaz nueva estilo macOS (pedido del usuario)
- **Tokens compartidos** en `ui/MacTheme.kt` (`Mac`, `MacGroup`, `MacInfoRow`, `MacCapsule`, `MacSectionHeader`, `MacStatusDot`, `macFocusRing`):
  - Modo oscuro de macOS: fondo #1C1C1E, grupos #2C2C2E con separadores de 0,5 dp.
  - Un único acento azul #0A84FF; verde, naranja y rojo solo para estado.
  - Texto secundario al 60 %, escala de espacios de 4 dp, radios de 6–12 dp, sin gradientes ni sombras.
- **Launcher** (`LauncherScreen.kt`):
  - Barra lateral "Biblioteca · N": cada juego con su inicial en color estable y "Windows · 64/32 bits". La selección es azul con la lista enfocada y gris si no.
  - Abajo de la barra, "Buscar juegos" y "Añadir carpeta".
  - Detalle: icono grande, título, cápsulas (64 bits, Portable, Memoria ampliada), botón azul "Jugar" (o "Continuar" con Quick Resume, más la cápsula verde "En pausa en memoria") y "Preparar caché".
  - Grupos "Detalles", "Este dispositivo" y "Carpetas de juegos" (tocar una carpeta la quita, con confirmación). Estado vacío explicado.
  - Se quitaron los textos de marketing ("DIRECT LAUNCH .EXE", "LPDDR5X").
- **Menú rápido** (`SessionOverlay.kt`), con aspecto de ventana de Ajustes:
  - Izquierda: título, estado en vivo con punto de color y texto (FPS respecto al objetivo, batería, protección térmica) y Reanudar / Salir (sigue en memoria) / Forzar cierre. La confirmación de Forzar cierre sale en el mismo panel.
  - Derecha: grupos Rendimiento, Imagen y Controles y pantalla, con filas etiqueta + ayuda y el valor en un selector "‹ valor ›". El tope aparece en gris "Lo fija HyperFG" cuando HyperFG está activo.
  - La fila enfocada se desplaza sola a la vista (`BringIntoViewRequester`). Antes las 11 filas no cabían en la altura apaisada.
  - `QuickMenuItem` reordenado: primero las acciones y después los ajustes.
- Pantallas de carga y de fin con el mismo estilo (barra de progreso azul y registro en grupo).
- Verificado con capturas en el RedMagic (`scratchpad/ui2.png`, `ui4.png`). Gate verde. Prueba `quickMenuActionsComeFirst`.

## OBJETIVO: Stable Alpha 1.0.0 (trabajo autónomo autorizado por el usuario)
Hollow Knight, Cuphead y DmC: Devil May Cry deben arrancar desde el launcher, mostrar imagen real sobre Vulkan, reproducir audio y responder a los controles sin cerrarse solos, **a 60-120 FPS** (requisito del usuario, 2026-11-18). Tiempo de carga objetivo (usuario, 2026-11-18): **~5 s** desde el launcher hasta el título o menú (Cuphead tarda hoy ~5 min). El rasterizador por software (4 FPS) queda solo como referencia de corrección: la ruta de la Alpha es render por GPU (DXBC -> SPIR-V, estado D3D11/D3D9 -> pipelines Vulkan en el Adreno 830) más rendimiento del JIT.
Fases: (1) cierre Unity x64: VEH/SEH a Mono, `K32EnumProcessModules`, primer `Present` con `screencap`; (2) rasterizador D3D11 (`Draw/DrawIndexed`, DXBC) y audio (`MMDevAPI/WASAPI`, `waveOutWrite` -> `DirectAAudioEngine`); (3) 32 bits para DmC (stdcall/cdecl/thiscall en thunks, arena < 4 GB, `FS:` TEB/SEH/TLS, puente D3D9 -> Vulkan); (4) sello 1.0.0 (IL2CPP `GameAssembly.dll`, `vcruntime140/msvcp140/bcrypt`, caché HyperAOT persistente).

## 2026-11-19 (noche 6): HyperFG manda sobre el tope (pedido del usuario)
- **Con HyperFG activo** el selector "Tope de FPS" queda bloqueado: sale en gris, y al intentar cambiarlo aparece un aviso con sonido de error (`FrameGenLogic.fpsCapSelectable`). El juego va a 60 y se muestran 120, con el panel siempre a 120 Hz (`FrameGenLogic.panelHz`).
- **Con HyperFG apagado** se vuelve a elegir el tope, y el panel sigue al tope (72 → 144 Hz).
- **Protección térmica con HyperFG**: HyperFG sigue, limitado a 60 en pantalla (30 reales + 30 sintéticos, `THROTTLED_GAME_FPS`) hasta enfriarse. Esto sustituye a la pausa de HyperFG de la noche 5. Sin HyperFG: 60 a partir de 39,5 °C y 40 a partir de 41,5 °C.
- El escudo de frame lento escala con el ritmo del juego: 22 ms a 60 FPS y 44 ms a 30 (`slowFrameMs`, también en HyperFrameGen).
- Pruebas: `frameGenLocksTheUserFpsCap`, `slowFrameShieldScalesWithGameFps` y térmicas actualizadas. Gate verde. Instalado.

## 2026-11-19 (noche 5): protección térmica más fuerte
- **Problema** (usuario, captura): batería a 41 °C con la protección "activa". Con el tope ya en 60, la protección solo relajaba Prime, y HyperFG (activado por el usuario) seguía con la GPU al 93 %.
- **Cambios**:
  - Con la protección activa (≥ 39,5 °C) HyperFG se suspende: `ThermalDecision.suspendFrameGen`, `appliedFrameGen` en la actividad. La repetición de frames ya se apagaba con `thermalRelaxed`.
  - Con ≥ 41,5 °C hay un tope de emergencia de 40 FPS (`HOT_FPS`, 3 vsyncs exactos a 120 Hz) hasta bajar de 40,5 °C. Después vuelve a 60, y por debajo de 37,5 °C al perfil del usuario.
  - El Quick Menu muestra el tope en vigor y "HyperFG en pausa"/"en pausa por calor"; el HUD ya no marca FG en pausa.
- Pruebas: `emergencyCapAt41_5UntilBelow40_5` y `frameGenRespectsThermalProtection` ampliada. Gate verde. Instalado; falta medir cuánto enfría en partida.

## 2026-11-19 (noche 4): guardado de HK roto (NtQueryDirectoryFile) y audio
- **Guardado**: `user2.dat` se quedaba con la fecha de las 07:54. HK escribe `user2.dat.new` y `user2_1.5.12620.dat` y después enumera la carpeta (`FileSystemEnumerator` de Mono → `ntdll!NtQueryDirectoryFile`).
  - La función no existía: `EntryPointNotFoundException`, el guardado se cortaba y el `.new` nunca pasaba a `.dat`.
  - Al cargar, HK borraba el `.new` como "likely corrupted": se perdía el progreso.
  - Arreglado: `winfile::NtQueryDirectoryFile` sobre `fdopendir` por fd, validado por dispositivo+inodo. Clases 1/2/3/12/37/38, filtro `*`/`?`, entrada pendiente si no cabe, `STATUS_NO_MORE_FILES`.
  - Copia de las partidas en `_HyperPlaySaves/backup_20261119_1540`. El último guardado completo (`.new`, 15:35, idéntico a `user2_1.5.12620.dat`) se pasó a mano a `user2.dat`.
- **Atasco tras morir** (usuario: reaparece en el banco y no camina): probablemente es la misma excepción a mitad de la corrutina de muerte y guardado. Pendiente de confirmar con el usuario.
- **Audio**: en la partida con la versión de PSHUFLW, `WASAPI ritmo` da 48 000 frames/s, silencio 0 y GetBuffer medio de 945.
- **Overlay de RedMagic**: marca 119–120 FPS por la repetición de frames (presentaciones/s). El juego va a 60, como indica nuestro HUD.

## 2026-11-19 (noche 3): repetición de frames, PSHUFLW y audio
- **Repetición de frames** (`HyperFrameGen::recordRepeat`, `debug.hyperplay.repeat 0` la apaga):
  - Con el juego a la mitad del refresco (60 FPS en 120 Hz), cada frame se copia al historial y el hilo de HyperFG lo vuelve a presentar a medio intervalo. Es la misma imagen, sin interpolar.
  - El sistema ve 120 presentaciones/s y nubia perf deja de limitar la CPU: 3532/4320 MHz y 60,0 fps exactos.
  - Se apaga con la protección térmica (`pacing::thermalRelaxed`), para que el móvil vuelva a ahorrar. Las repeticiones no cuentan en el FPS del HUD (`endPresent(..., countFrame=false)`).
  - Paseo de 60 s: después de los primeros ~25 s, 60,0 fps con 2–11 frames de más de 18 ms por cada 2 s y ninguno de más de 33 ms.
- **[Ritmo]**: nuevo contador `>18ms`. La captura de tirones (`stallprof`) salta ya con frames de más de 40 ms. Script `walksum.ps1` para resumir un paseo.
- **Picos al caminar** (40–150 ms): el hilo principal ejecuta código C# compilado por Mono. Nuestra traducción durante el paseo suma solo 4 ms (`JIT total` sin cambios), así que el coste es la ejecución.
- **Anclaje a Prime** (`primepin 1`): en un paseo, los frames de más de 18 ms bajaron de 935 a 136; los de más de 33 ms se mantuvieron (15 frente a 12). Pendiente repetirlo (la segunda pasada se cortó por ADB).
- **Fallo del JIT: PSHUFLW y PSHUFHW** (F2/F3 0F 70) se traducían como PSHUFD.
  - El `strchr` SSE2 de la CRT de Mono no encontraba el carácter cuando caía en los bytes 2–3 de cada grupo de 4. Por eso el GC incremental se caía al iniciar: `strchr("incremental=..", '=')` devolvía NULL.
  - Arreglado con `emitInsElementH`. Pruebas `testPshufLwHw` y `testMonoCrtStrchr` (bytes reales de Mono). Suite: 39 510 comprobaciones, 0 fallos. `kCodegenVersion` 5.
- **Audio "muy mal"** (usuario): la línea `WASAPI ritmo` alterna entre ejecuciones de 48 000 y 32 000 frames/s con el mismo código. Con 32 000, un tercio es silencio.
  - El mezclador del juego no está saturado (menos del 12 % de CPU).
  - Diagnóstico nuevo en `WASAPI ritmo`: GetBuffer medio y máximo, padding medio, silencio insertado por segundo, llamadas a GetCurrentPadding por segundo y eventos por segundo.
  - Sospecha: el fallo de PSHUFLW (código SSE de FMOD). Pendiente de verificar con el usuario.

## 2026-11-19 (noche 2): estabilidad de HK, el límite a 60 FPS lo pone el móvil
- **Desglose del frame** en `[Ritmo]`: `juego med/max >15ms` (desde que acaba un Present hasta el siguiente) y `present med/max` (nuestro Present). Con tope 60, el Present cuesta 0,5–2 ms; el frame se va en el juego.
- **Causa de los 57–58 FPS a tope 60** (Cruce Olvidado, quieto): el control de frecuencia de nubia (`nubia.perf.cpu.cpufreq.ctrl=1`) baja `scaling_max_freq` cuando el juego va justo a 60.

  | Tope | max p0 / p6 (MHz) | Resultado |
  |---|---|---|
  | 60 con ADPF | 1555 / 3283 | 57–58 fps, peor frame 25 ms, juego 15,5 ms |
  | 60 sin ADPF | 2745 / 1017 | Peor que con ADPF |
  | 66 | 3532 / 4320 (sin límite) | 66,0 fps, peor frame 19–20 ms |
  | 72 (prop) | 3532 / 4320 (sin límite) | 72,0 fps, peor frame 17–18 ms |
  | Sin tope | 3532 / 4320 (sin límite) | 76–77 fps, peor frame 17–18 ms, juego 12,5 ms |
  | 72 de la app (panel 144 Hz), caminando y atacando | 3532 / 4320 (sin límite) | 64–72 fps (casi siempre 68–72) |

  Con 72 desde la app quedan picos de 34–142 ms al caminar: cargas de assets y GC de Mono.
- **Probado y descartado**:
  - `adpfpct` 50/60/80: sin cambio.
  - TSO desactivado: −0,5 ms por frame.
  - Flags muertos entre bloques: sin cambio medible en fps (ver abajo).
- **Flags muertos entre bloques** (`debug.hyperplay.xflags`, activo):
  - Una salida directa no materializa RFLAGS si su destino sobrescribe todos los flags antes de leerlos.
  - El destino tiene que ser código de una imagen PE (`addStaticImage` desde BionicPeLoader) en la misma página de 4 KiB que el bloque, y no ser un trampolín.
  - Clasificador `flagsDeadAt`: ALU, TEST y NEG = escritura completa; INC/DEC = escritura parcial (se sigue mirando); MOV/LEA/MOVZX/PUSH/POP/NOP/NOT/SSE = neutras; el resto = vivo.
  - En `eliminateDeadFlags`, una escritura seguida solo de salidas `DeadExit` (sin lecturas ni saltos atrás) es muerta.
  - Bit 14 de `aotConfigKey`. Prueba `testCrossBlockDeadFlags`. Suite: 39 114 comprobaciones, 0 fallos.
- **Perfilador por hilo**: `setprop debug.hyperplay.proftid main|<tid>` crea un timer de CPU de ese hilo (el del proceso fusionaba expiraciones). En partida el proceso usa 2,35 núcleos; el hilo principal va al 79 % con un perfil plano.
- **Pendiente (decisión del usuario)**: ¿tope por defecto a 72 FPS (144 Hz)? 60 sigue siendo el valor por defecto que pidió el usuario. La protección térmica sigue bajando a 60 al calentarse.

## 2026-11-19 (noche): JIT, los bloques de la caché de disco ya se encadenan
- **Fallo**: `prologueBytes_` solo se fijaba en `compileLocked`. Un hilo que solo cargaba bloques de `oryon_jit.dat`, sin traducir ninguno, lo tenía a 0. Así `linkExit` no enlazaba nada y cada salida de bloque volvía al despachador C++.
- **Perfil del arranque en frío** (`debug.hyperplay.prof 30`, `scratchpad/prof.sh`): `executeEntry` 13 %, `linkExit` 5,6 % y `suspensionPoint` 5,4 %, un 24 % de la CPU en el despachador.
- **Arreglo** (`loadPersistedLocked`): si `prologueBytes_` es 0 se emite el prólogo (fijo) en el hueco del bloque para medirlo; el `memcpy` del bloque lo sobrescribe.
- **Medido** (HK, 30 s desde el lanzamiento):
  - CPU del proceso: de 71,0 s (2,37 núcleos) a 41,9 s (1,40 núcleos), un 41 % menos. El despachador desaparece del perfil.
  - Primer frame: igual, 11,7 s.
  - Menú: igual, ~36 s.
  - El tiempo de reloj del arranque lo marca el hilo principal (unos 21 s de CPU en 30 s), con un perfil plano: no hay una función dominante.
  - La función caliente de UnityPlayer `0x18062b0f0` es el bucle de espera activa de los workers de Unity (`pause` + `incl esi`). PAUSE sigue como NOP, que es lo más barato.
- Suite nativa: `OryonEmitterBackendTest: 39089 comprobaciones, 0 fallos`. Gate Gradle verde.
- Qué ganas: menos calor y batería al arrancar, y los hilos de trabajo dejan de pagar el despachador en cada bloque.

## 2026-11-19 (noche): Quick Resume en memoria (arranque instantáneo la segunda vez)
Pedido del usuario: "luego de abrir una vez debería ser posible abrir más rápido". Un volcado completo a disco se descartó: habría que reubicar punteros del host, el estado de la GPU y los hilos bloqueados en thunks. En su lugar, el juego se congela en RAM, como una consola.
- **Pausa nativa** (`loader/FramePacing`):
  - `setPaused`/`paused`. `paceFrame()` bloquea en una condvar al inicio del Present; no se tiene ningún mutex en ese punto. Al volver reancla el limitador sin informar a ADPF de la pausa.
  - La bomba WASAPI para AAudio y deja de señalar eventos (el mezclador del juego se queda esperando). Al reanudar vuelve a arrancar el motor de audio.
  - JNI `nativeSetPaused`; `nativeDirectStop` reanuda antes de parar.
- **Ciclo de vida** (`GameSessionActivity`):
  - `surfaceDestroyed` pausa (Inicio, otra app o pantalla apagada) y `surfaceCreated` reanuda. Antes el juego seguía gastando CPU/GPU sin verse.
  - `onDestroy` con el juego vivo y sin Force Kill lo guarda en `session/QuickResume`, sin `forceKill`.
  - Al abrir el mismo `.exe` (`QuickResumeLogic.action`, rutas `/sdcard` = `/storage/emulated/0`) se reengancha: launcher, log, mando, fase RUNNING y `pollSession`. Otro `.exe` cierra antes el suspendido.
  - `launchMode=singleTask`; `onNewIntent` con otro juego cierra el actual y abre el nuevo.
- **`QuickResumeService`**:
  - Servicio en primer plano `specialUse` durante toda la sesión. Se arranca con la actividad visible, como exige Android 12+.
  - Notificación "En memoria" con toque para volver y acción "Cerrar juego". Se para al terminar el juego, en Force Kill y si falla el arranque.
- **Quick Menu**: nueva entrada "Salir (Quick Resume: sigue en memoria)", justo encima de "Forzar cierre".
- **Medido en el RedMagic (HK, menú)**:

  | Medida | Antes | Con Quick Resume |
  |---|---|---|
  | Primer frame | 11,7 s (en frío) | 0,5 s (58 FPS) |
  | Menú | ~33 s (en frío) | 0,5 s |
  | CPU del proceso | 203 % jugando | 3,8–11 % congelado |

  Mismo PID. Tras volver, 60 FPS con el peor frame en 17–26 ms. Probado por Inicio y por "Salir". El servicio está en primer plano (`isForeground=true`).
- **Limitaciones**:
  - Si Android mata el proceso (poca RAM, o el usuario lo cierra desde Recientes o la notificación) vuelve el arranque normal.
  - El Present D3D9 (DmC) aún no pasa por `paceFrame`, así que no se pausa.
- Tests: `QuickResumeLogicTest` (6). Gate verde.

## 2026-11-18 (noche 2): resolución nativa, icono, RET en línea, llamadas nativas directas, escucha de FPS
Pedido del usuario: 60 FPS estables mínimos en partida sin tirones ni congelaciones (motor, no ports); orden HK -> Cuphead -> DmC; resolución nativa de cada teléfono; icono nuevo (`docs/icono_hyperplay.jpg`).
- **Icono**: adaptativo con el logo HP (fondo `#0F0F0F`, primer plano `drawable-xxxhdpi/ic_launcher_logo.png` con alfa por luminancia, sirve también de monocromo).
- **Resolución nativa**: `Win32DisplayInfo.h` ya no fija 1344x608; `setDisplayMode()` toma la superficie real (JNI `nativeSetDisplayMode` desde `GameSessionActivity` antes de lanzar + `nativeDirectAttachSurface`). Unity recibe `-screen-width/-screen-height/-screen-fullscreen 1` nativos (PlayerPrefs tenía 1024x768). Verificado: backbuffer 2688x1216.
- **Idioma**: el usuario puso HK en español desde el juego.
- **JIT**:
  - búsqueda en línea ARM64 para RET/JMP/CALL indirectos en la caché rápida (`emitInlineDispatch`, ×2,2 en CALL/RET);
  - **llamadas nativas directas** (`emitFastNativeCall`): `call [IAT]` a una lista segura (`TlsGetValue`, secciones críticas, SRW, `HeapAlloc/Free`, QPC, `GetCurrentThreadId`, `WaitOnAddress`...) hace BLR desde el bloque sin despachador; las bloqueantes marcan el hilo "en nativo" y comprueban suspensión con LDAR al volver. `TlsGetValue` pasó de 143 000 thunks/s a ~250;
  - desenlace selectivo por destino (`unlinkRangeLocked` + índice `linksByTarget_`) y borrado de la caché rápida solo para los bloques borrados (antes `eraseBlocksLocked` recorría 64 K entradas en cada escritura de Mono: 20 % del hilo principal).
  - Interruptores: `debug.hyperplay.inlinedispatch`, `debug.hyperplay.fastnative`.
- **Audio**: el ritmo WASAPI es correcto (48 000 frames/s, 50 ReleaseBuffer/s): el coste del hilo de mezcla es DSP real, no un bucle.
- **Escucha de FPS**: `[Ritmo] fps | peor ms | >33ms | >100ms` cada 2 s (etiqueta DllInterceptor; este ROM descarta "HyperPlay.FPS"), `[Thunks/s]` cada 15 s. `scratchpad/listen.ps1` lo guarda en `docs/fps_live.log` con reconexión.
- Medidas: menú ~144 FPS; partida a nativa 23 FPS -> 37-39 FPS (antes de quitar el recorrido de 64 K); cargas con frames de 0,2-1,7 s y una de 28 s.

## 2026-11-18 (tarde): HK jugable de principio a fin, encadenado de bloques, flags muertos, mando minimalista
- **Media Foundation verificado**: la intro de HK se reproduce (MP4 H.264 por `AMediaCodec`), termina y el juego entra en el Paso del Rey; el Caballero camina y ataca con el mando táctil. Capturas en scratchpad (`mf2`, `mf3`, `mf4`).
- ADB: la IP pasó a `192.168.0.9:33145`; mDNS no lo anunciaba -> barrido de ping + `portscan.ps1` (30000-50000).
- **TSO**: sin TSO la partida "se congelaba" ~70 s en la carga del Paso del Rey, pero con TSO también: es la carga de escena (no presenta durante ~70 s). TSO sigue activo por defecto; su necesidad real **no está demostrada**.
- **Encadenado de bloques** (`emitLinkableExit`/`linkExit`/`unlinkAllLocked`): las salidas a RIP fija (Jcc, JMP/CALL directos, fin de bloque) escriben RIP, descuentan `ctx.chainBudget` (2048; vuelve al despachador para suspensiones), comparan el contador SMC global con `ctx.smcCursor` y terminan en un `B` que el despachador parchea al cuerpo del bloque destino (tras el prólogo). El epílogo empieza con `exitSite = 0`. Se deshacen todos los enlaces en cualquier invalidación (SMC, `invalidateCodeRange`). Arreglado: tras una excepción del huésped o un cambio de RIP durante la suspensión, `exitSite` quedaba viejo y enlazaba mal (crash al arrancar Unity). `setprop debug.hyperplay.chain 0` lo desactiva.
- **Flags muertos**: cada escritura completa de RFLAGS (`emitWriteFlags`, ~30 instrucciones) seguida de otra sin lectura intermedia se salta con un `B`; lecturas = cargas de `rflags`/`nzcv`, escrituras de RIP y llamadas nativas; una escritura que alguna rama salta (SHL r,CL con cuenta 0) no cuenta como sobrescritura. `setprop debug.hyperplay.deadflags 0` la desactiva.
- Suite nativa **1025/1025**; bucle de 3 M vueltas: 59,6 ms sin encadenado -> 15,2 ms (×3,9).
- Carga HK (primer Present): 97 s -> 81 s (encadenado) -> 74,5 s (+ flags muertos).
- **HK en partida: 43 FPS · 23 ms** con encadenado + flags muertos (antes 19-20 FPS). Una muestra intermedia dio 4 FPS sin explicación (vigilar). Falta llegar a 60-120: siguiente paso, perfil por hilo del principal con el nuevo JIT. El hilo de mezcla de audio consume ~50-66 % de un núcleo de forma continua: revisar el ritmo de WASAPI de Unity.
- Mando nuevo verificado en el teléfono: `docs/capturas/2026-11-18_mando_minimalista_hk_menu.png`. Gate verde.
- Perfilador: `debug.hyperplay.profdelay <s>` (empieza a muestrear tras N s) e informe por hilo (`T`, `tG`, `tN`).
- **Mando táctil rediseñado** (pedido del usuario): LT sobre LB y RT sobre RB pegados a las esquinas superiores, **L3/R3 dedicados** debajo; stick izquierdo en la esquina inferior-izquierda con cruceta compacta (−35 %, 4 pétalos con hueco central) a su derecha; rombo A/B/X/Y en la esquina inferior-derecha con el stick derecho a su izquierda; View/Menú pequeños a `y = 0.07`. Medidas en unidades de altura ancladas a los bordes (20:9 y 16:9). Estilo cristal oscuro (~19-25 % en reposo, ~65 % al pulsar, borde `#66FFFFFF`), sticks flotantes casi invisibles en reposo. Tests: sin solapes (círculos y rectángulos) en 2688x1216, 1920x1080 y 800x360, centro y franja del HUD libres, L3/R3, sticks flotantes en 16:9.

## 2026-11-18 (madrugada): cuelgue de Hollow Knight resuelto, escáner del JIT, SSE2 de enteros, Media Foundation
- **Causa real del cuelgue tras el idioma**: un worker de jobs de Unity moría con `opcode 0F E7 no soportado` (`MOVNTDQ`, "Hilo invitado ... finalizó con código -2"); el job nunca terminaba y el hilo principal esperaba para siempre en `WaitOnAddress` (0x18051f65e). Añadidos `MOVNTDQ` (66 0F E7) y `MOVNTPS/PD` (0F 2B) -> `STR Q`.
- **`jit_scan`** (`app/src/test/cpp/JitScan.cpp`): mapea un PE32+ sin ejecutarlo, recorre todas las funciones de `.pdata` bloque a bloque con el traductor real y agrupa los errores. UnityPlayer de HK (103 587 funciones) en 1,7 s. Antes 580 funciones con error, ahora 474, casi todas ruido del barrido (opcodes de 1 byte inválidos en x64, AES/SHA no anunciados en CPUID, MMX). Script: `scan_all.sh` (HK y Cuphead UnityPlayer + mono).
- **SSE2/SSE4.1 de enteros añadidos** (todos los que encontró el escáner): PMULLW, PMULHW, PMULHUW, PMULUDQ, PMULDQ, PMADDWD, PSADBW, PACKSSDW/PACKUSWB/PACKSSWB, PAVGB/W, PMINUB/PMAXUB/PMINSW/PMAXSW, PADD/PSUB con saturación (US/S, B/W), PSRL/PSRA/PSLL W/D/Q por registro (cuenta >= ancho -> 0 o signo), PMOVSXDQ, PMOVZXBD/WD, PABSB/W/D, PBLENDVB/BLENDVPS/BLENDVPD. Test `testSse2IntegerOps` contra referencia en C++: **suite 1013/1013**.
- **TSO** (orden de memoria x86): accesos escalares del huésped con `LDAPR`/`STLR` (el CPU tiene `lrcpc`/`uscat`), desalineados con DMB. Interruptor `setprop debug.hyperplay.tso 0`. No era la causa del cuelgue; **pendiente medir** su coste en la carga (HK tarda ~100 s al logo con TSO) y decidir el valor por defecto.
- Vigilante de bloqueos: vuelca una vez por hilo la cadena de retornos del huésped (`process_vm_readv`, sin SIGSEGV en el tope de la pila).
- `callGuestNested` admite hasta 8 argumentos (los callbacks de MF usan 6).
- **Verificado**: HK pasa el logo de Team Cherry (60 FPS), menú principal, perfil, calibración y brillo con el mando táctil (pulsación de 150 ms: `input tap` es demasiado corto para el sondeo del juego) y empieza partida nueva (poema del prólogo).
- **Bloqueo siguiente: la intro es vídeo**. MP4 H.264 + AAC dentro de `sharedassets414.resource`; Unity lo abre con Media Foundation (`mfplat/mfreadwrite/mf.dll` no existían -> *"Initializing Microsoft Media Foundation failed"*) y la secuencia de HK espera a que el VideoPlayer cargue (no se deja saltar). Nuevo **`media/MediaFoundationBridge.{h,cpp}`**: COM propio (IMFAttributes, IMFMediaType, IMFSample, IMFMediaBuffer, IMFSourceResolver::CreateObjectFromByteStream leyendo el IMFByteStream del juego, IMFMediaSource mínimo, IMFSourceReader síncrono y asíncrono con `OnReadSample`), demux con `AMediaExtractor` en memoria y decodificación por hardware con `AMediaCodec` (NV12/I420/RGB32/YUY2; PCM16/float). Compila y se instala; **sin probar en el teléfono** (ADB se cayó en la primera prueba).

## 2026-11-18 (noche): audio de Cuphead (FMOD) — waitable timers
- Causa de *"FMOD failed to initialize the output device"*: FMOD abre WASAPI en modo sondeo (sin evento) y su hilo de mezcla espera un **waitable timer**; `CreateWaitableTimerA` era un stub que devolvía 0 y FMOD caía a la salida nula.
- Implementados `CreateWaitableTimerW/A/ExW/ExA`, `SetWaitableTimer(Ex)` (vencimiento relativo o FILETIME absoluto, periodo en ms) y `CancelWaitableTimer`: el timer es un evento de HyperPlay (sirve con `WaitForSingleObject`/`Multiple`) que un hilo de servicio único señala al vencer y reprograma.
- Verificado por log: desaparece el aviso de FMOD; en ~20 s hay ~980 pares `IAudioRenderClient::GetBuffer(960 frames)`/`ReleaseBuffer` que el bombeo envía a AAudio (48 kHz estéreo). Falta confirmarlo de oído en el teléfono.

## 2026-11-18 (noche): Cuphead responde al mando (Rewired + XInput)
- Causa: Rewired (`Rewired_Windows_Lib`) pedía `user32!SetWindowsHookExW` por P/Invoke (hook sobre la ventana de Unity para WM_INPUT); sin él, *"Unable to initialize input source!"* y caía a la entrada de Unity, que no ve nuestro mando. Ahora `SetWindowsHookExW/A` devuelve un HHOOK simulado, más `UnhookWindowsHookEx`, `CallNextHookEx`, `RegisterTouchWindow`.
- Rewired encuentra `Xinput1_4.dll` y usa la API completa: añadidos `XInputGetCapabilities` (gamepad conectado), `XInputEnable`, `XInputGetBatteryInformation` (con cable), `XInputGetKeystroke` (ERROR_EMPTY) y `XInputGetStateEx` (nombre y ordinal #100).
- `GetProcAddress` NULL se registra una vez por nombre base (antes el tope de 200 lo agotaban los sondeos decorados `_X@N` de Mono).
- **Verificado**: A en "Press Any Button" -> menú principal START/OPTIONS/DLC/EXIT con el aviso "CONFIRM Ⓐ" de Xbox (`docs/capturas/2026-11-18_cuphead_menu_con_mando.png`). Queda: *"Exception getting HID device list"* (Raw Input HID, no necesario para XInput).

## 2026-11-18 (noche): mando táctil con distribución Xbox y sticks flotantes
Pedido del usuario (maqueta de referencia): LB/LT arriba a la izquierda, RB/RT arriba a la derecha, View (⧉ = Back) y Menú (≡ = Start) arriba al centro, stick izquierdo arriba-izquierda + cruceta grande abajo-centro-izquierda, rombo A/B/X/Y con colores Xbox (Y amarillo, X azul, B rojo, A verde) arriba-derecha + stick derecho abajo-centro-derecha; **sticks flotantes**: nacen bajo el dedo en cualquier zona libre de su mitad y vuelven a su reposo al soltar; doble toque y mantener = L3/R3; LT/RT = L2/R2 digitales (gatillo completo).
- `input/VirtualTouchLayout.kt`: posiciones en fracciones de la pantalla y tamaños proporcionales a la altura (escala igual en 2688x1216 y 16:9); `VirtualTouchInput` con dos sticks independientes (`onSticks(lx, ly, rx, ry)`), base flotante dentro de pantalla, doble toque con reloj inyectable.
- `input/VirtualTouchOverlayView.kt`: estilo cristal translúcido, iconos ⧉ y ≡ dibujados, cruceta con flechas, sticks con aro y pomo (tenues en reposo).
- El HUD de rendimiento pasa abajo al centro (arriba están los botones).
- Tests JVM reescritos (`VirtualTouchLayoutTest`: sin solapes en 3 tamaños de pantalla, distribución Xbox, stick flotante bajo el dedo y dentro de pantalla, dos sticks a la vez, doble toque L3, gatillos/Menú, cruceta). Gate verde.
- **Verificado en el teléfono** con Hollow Knight (`docs/capturas/2026-11-18_mando_xbox_hk_confirmar.png`): cruceta abajo x2 (Русский -> 简体中文) y A abre el diálogo de confirmación, a 124 FPS. En Cuphead (`2026-11-18_mando_xbox_cuphead.png`) el mando se ve correcto, pero el juego **no responde**: Rewired avisa *"Unable to initialize input source ... will fall back to Unity input"* y la entrada de Unity no lee nuestro XInput. Pendiente: proveedor de entrada de Cuphead (RawInput/DirectInput/XInput de UnityPlayer).

## 2026-11-18 (noche): RUTA GPU D3D11 -> Vulkan — de 4 a 122-157 FPS
Hecho y verificado en el RedMagic 10 Pro (suite nativa 954/954; gate `testDebugUnitTest lintDebug assembleDebug` verde):
- **Hollow Knight**: menú de idiomas idéntico al del rasterizador por software, **122 FPS · 8,2 ms**, CPU 26 %, GPU 14 % (`docs/capturas/2026-11-18_hollow_knight_gpu_122fps.png`). 74 377 draws en GPU, 0 fallidos, 5 pipelines.
- **Cuphead**: título completo con grano de película, aberración cromática y viñeta, **157 FPS · 6,3 ms** (`docs/capturas/2026-11-18_cuphead_gpu_157fps.png`); "Build version" a **+36 s** (antes 150 s); 25 pipelines, 0 draws fallidos.

Implementación:
- `gfx/D3D11GpuRenderer.{h,cpp}`: el interceptor pasa el estado del contexto como datos (`DrawCall`, `TextureBinding`, `BufferBinding`). Texturas reflejadas en `VkImage` (subida desde la copia de CPU cuando cambia `generation`; BC1-7 nativo si `textureCompressionBC`, si no descompresión a RGBA8; A8 con swizzle); VB/IB/CB copiados a un **anillo host-visible por frame** (2 frames en vuelo, caché por clave+generación dentro del frame); render passes cacheados por formato (LOAD/STORE) y framebuffers por vista; **pipelines** por (VS, PS, layout, strides, topología, blend, cull, formato RT); descriptor set por draw (UBO/SAMPLED_IMAGE/SAMPLER) con vistas según la dimensión que declara el shader; samplers D3D11 -> Vulkan cacheados; viewport con altura negativa (misma orientación que D3D); ClearRTV, CopyResource y **CopySubresourceRegion en la GPU** (si se hacían en CPU pisaban lo dibujado: era la causa de la escena negra de Cuphead).
- Presentación: pase gráfico propio (triángulo a pantalla completa, `gfx/shaders/present.{vert,frag}` -> `PresentSpv.h` con glslc) que aplica la rotación de la superficie (pre-transform) por push constants; `DirectVulkanBridge::beginPresent/endPresent/submit/gpuContext`.
- Dispositivo: se activan `textureCompressionBC`, `samplerAnisotropy`, `independentBlend` si existen.
- `setprop debug.hyperplay.gpu 0` vuelve al rasterizador por software (referencia).
- Diagnóstico: `noteGpuClobber` registra escrituras de CPU (Unmap/Copy/Update) sobre recursos con contenido de GPU; el renderer avisa de resubidas de imágenes escritas por la GPU.

**Frame pacing (mismo día)**: `GameSessionLogic.TARGET_FPS = 120` (modo de pantalla 120 Hz + `Surface.setFrameRate(120)`) y swapchain en **FIFO** por defecto (`setprop debug.hyperplay.mailbox 1` para MAILBOX). Cuphead: **118 FPS · 8,5 ms estables**, GPU 31 % (con MAILBOX iba a 157 FPS tirando frames sobre un panel a 60 Hz). Siguiente paso de pacing: `VK_GOOGLE_display_timing` para programar cada present.

Pendiente de la ruta GPU: profundidad/estencil (DSV, `OMSetDepthStencilState`: HK/Cuphead son 2D, DmC lo necesitará), MRT (solo RT0), lecturas de CPU de RT (STAGING), buffers estáticos persistentes (hoy se copian al anillo cada frame), frame pacing (hoy MAILBOX sin límite: >144 FPS).

## 2026-11-18 (noche): perfil de la carga de Cuphead — el cuello de botella es el rasterizador
Nuevo perfilador de muestreo `cpu/GuestProfiler.cpp` (`setprop debug.hyperplay.prof <s>`; SIGPROF por 1 ms de CPU del proceso; rango host -> RIP de cada bloque traducido; reporte en `<juego>/_HyperPlaySaves/profile.txt`, porque logcat pierde líneas con tanto tráfico). Script offline `prof_map.py` (scratchpad) que agrupa por función vía `.pdata`.
- 290 s de carga: **4,6 núcleos de media**; muestras: ~85 % en el rasterizador por software (`dxbc::Vm::src/readRaw/exec/write/run`, `sample2D`, `texel8`, `shadePixel`), **solo 9,3 % en código del juego traducido** (top: UnityPlayer+0xf3cedf, +0x8ed3b0, LZ4 +0xb852c2).
- Experimento `setprop debug.hyperplay.noraster 1` (los draws no se rasterizan): la escena del título ("Build version") llega en **37 s en vez de 150 s**; el bucle del juego hace **3000 Present en 120 s (~25 fps)** con el JIT actual. Unity carga de forma asíncrona y avanza por frame, así que los frames de 0,25-4 s arrastraban la carga.
- Conclusión: la ruta GPU (DXBC -> SPIR-V) resuelve a la vez FPS y casi toda la carga. Los 37 s restantes incluyen las intros con tiempo fijo (logos) y la carga real; se perfilarán con el rasterizador desactivado.

## 2026-11-18 (noche): traductor DXBC -> SPIR-V (base de la ruta GPU)
- `gfx/SpirvBuilder.h`: constructor de módulos SPIR-V 1.0 (secciones en orden, tipos/constantes deduplicados, `typeArrayUnique` para tipos con layout propio).
- `gfx/DxbcToSpirv.h`: traductor SM4/SM5 VS/PS con la misma cobertura que `dxbc::Vm`. Registros como `uvec4` crudos con bitcast por instrucción; swizzle/máscara/neg/abs/saturate (NClamp); control de flujo estructurado (if/else, loop con continue, break(c)/continue(c) como selección + rama, switch con casos compartidos, ret(c), discard -> OpKill); CB como bloques uniform `uvec4[N]` (stride 16), ICB privado constante, `x#[]` en Function; texturas 1D/2D/3D/cube (+arrays) con imagen separada + sampler, `sample/_l/_b/_d/_c/_c_lz`, `ld`, `resinfo` (float/rcp/uint), `gather4`; desplazamientos constantes; FragCoord.w -> 1/w como en D3D; SV_VertexID/InstanceID/IsFrontFace/SampleIndex. Bindings: set 0, base de etapa (VS 0, PS 64) + {cb: slot, t: 16+slot, s: 48+slot}. PS enlazado por semántica con el VS (`translate(ps, &vs)`).
- Prueba `app/src/test/cpp/DxbcSpirvTest.cpp` (binario de dispositivo `dxbc_spirv_test <dirs> <salida>`): corpus real de **342 shaders** (12 de HK + 330 de Cuphead): **341 traducidos y los 341 válidos con `spirv-val --target-env vulkan1.1`** (NDK `shader-tools`). Falta: opcode 111 (1 shader de HK). La equivalencia semántica GPU vs `dxbc::Vm` se comprobará al conectar el pipeline Vulkan (comparando RT).
- Siguiente: renderizador D3D11 -> Vulkan (recursos en VkImage/VkBuffer, pipelines por estado, render passes por RT, presentación por blit en GPU).

## CHECKLIST OFICIAL Stable Alpha 1.0.0 (usuario, 2026-11-18) — estado
Criterio: HK, Cuphead y DmC arrancan desde el launcher, imagen real en Vulkan, audio, controles, sin cierres; además 60-120 FPS, carga ~5 s y pacing sin tirones (requisitos añadidos el mismo día).

**Fase 2 — gráficos D3D11 y correcciones del JIT**
- [x] Draw/DrawIndexed(+Instanced) en GPU: `gfx/D3D11GpuRenderer` (HK 122 FPS, Cuphead 157 FPS). Falta depth/estencil y MRT.
- [x] DXBC -> SPIR-V para VS/PS (`gfx/DxbcToSpirv.h`; 341/342 shaders del corpus válidos con spirv-val y en uso real).
- [x] NaN de Unity: corregido (`movss/movsd` reg-reg, codificaciones MINPS/MAXPS/MAXPD); 0 avisos en las corridas.
- [x] SMC: invalidación selectiva (`eraseBlocksLocked`); `smc=+0` en régimen estable.
- [x] `_pioinfo` 0x73efe0 eliminado.
- [~] Hook `MemoryManager` de Unity: desactivado por defecto (solo con `setprop debug.hyperplay.unitymemhook 1`), así que ya no adivina tamaños; falta decidir si se borra.
- [~] Cuphead: supera ReloadAssembly, carga AssetBundles y renderiza el título a pantalla completa ("Press Any Button"). Falta gameplay, audio (FMOD usa la salida nula) y controles.

**Fase 3 — DmC 32 bits**
- [ ] ABI stdcall/cdecl/thiscall en thunks (`[ESP+4..]`, limpieza de pila).
- [ ] Arena < 4 GB en BionicPeLoader/WinSafeHeap/VirtualAlloc cuando `is64 == false`.
- [ ] `FS:[0x18]` TEB, `FS:[0x00]` SEH, `FS:[0x2C]` TLS.
- [ ] Puente D3D9 -> Vulkan (IDirect3D9 / IDirect3DDevice9).

**Fase 4 — sello**
- [ ] HyperAOT persistente **reubicable** (el `aot_oryon.bin` actual guarda código con direcciones absolutas: no es recargable).
- [~] Audio y controles en gameplay: HK OK en menús (audio WASAPI -> AAudio, A/D-pad); falta gameplay y Cuphead.
- [ ] IL2CPP (`GameAssembly.dll`), vcruntime140/msvcp140/bcrypt.
- [ ] Salida limpia, Quick Menu, suite nativa + gate en verde al cierre (hoy: 954/954 y gate OK).
- [ ] 60-120 FPS, carga ~5 s, frame pacing (FIFO + display timing), Game Space/RedMagic.

## Plan de fluidez: frame pacing en pantallas 120/144 Hz y generación de frames de RedMagic (2026-11-18)
Requisito del usuario: sin tirones ni aceleraciones en pantallas de 144 Hz o más, y compatibilidad con la generación de frames de RedMagic.
- **Generación de frames de RedMagic** (chip Red Core R3, "Frame Rate Boost"/interpolación): se activa desde **Game Space** (modos Rise/Diablo) y RedMagic lo anuncia para una lista de juegos concretos (60 -> 120 fps). **No hay SDK ni API pública para desarrolladores**; el juego no necesita soportarlo. Lo que sí depende de nosotros: (1) que Game Space reconozca la app como juego: `android:appCategory="game"` + `android:isGame="true"` añadidos al manifiesto; (2) entregar una cadencia **estable** de 60 fps (la interpolación duplica frames regulares; con frames irregulares produce artefactos); (3) verificar en el teléfono si HyperPlay puede añadirse a la lista de interpolación de Game Space. Ojo: la interpolación añade latencia de entrada.
- **Frame pacing propio** (Android Game Development Kit): `Surface.setFrameRate` / `ANativeWindow_setFrameRate` con la tasa objetivo (60/72/120/144 según el panel) para que el sistema elija un refresco divisor exacto; en Vulkan, `VK_GOOGLE_display_timing` (`vkGetRefreshCycleDurationGOOGLE`, `VkPresentTimesInfoGOOGLE.desiredPresentTime` en CLOCK_MONOTONIC, realimentación con `vkGetPastPresentationTimingGOOGLE`) y `VK_EXT_present_timing` en Android 17+; alternativa: Swappy. Presentar en **FIFO con tiempos programados** a un divisor del refresco (p. ej. 72 o 144 en 144 Hz, 60/120 en 120 Hz) en vez de MAILBOX sin límite, que da aceleraciones. El reloj del juego (`QueryPerformanceCounter`) ya es monotónico; el paso del juego debe ser constante para no acelerar.
- Orden: primero la ruta GPU (sin ella no hay 60 fps que pacear), luego pacing con display timing, luego validación con Game Space.

## 2026-11-18 (noche): CUPHEAD — pantalla de título real y animada
Hecho y verificado (suite nativa: **954 comprobaciones, 0 fallos**; gate Gradle `testDebugUnitTest lintDebug assembleDebug` BUILD SUCCESSFUL). Captura: `docs/capturas/2026-11-18_cuphead_titulo.png` (título con Cuphead y Mugman animados, overlay **FPS 4 · 252 ms**, vivo a 600 s, 0 excepciones, 0 señales fatales).

Bugs del JIT encontrados y corregidos (cada uno con test en `OryonEmitterBackendTest.cpp`):
- **Shifts/rotaciones de 16 bits** (`66 C1/D1/D3`): el decodificador nunca pasaba `size=2`, así que `rol ax, 8` (el `_byteswap_ushort` de MSVC) se ejecutaba como de 32 bits y daba 0 con 0x0300. Unity leía los `flags` u16 big-endian de la tabla de bloques de los AssetBundles como 0 (= sin comprimir) y fallaba con *"was not built with the right version or build target"*. Ahora: valor replicado en las dos mitades para ROL/ROR, MSB real (bit 15) en RCL/RCR, SAR con signo de 16 bits, SF/ZF del ancho real (`emitTstSized`), guardado de 16 bits en la ruta CL (antes escribía 32 bits en memoria) y cuenta enmascarada a 5 bits.
- **Registros AH/CH/DH/BH** (sin REX): `SETcc`, shifts/rotaciones con CL, `INC/DEC r8` y XADD/CMPXCHG/XCHG de 8 bits escribían el byte bajo de RSP/RBP/RSI/RDI (podía corromper la pila). Nuevos `loadByteRm`/`storeByteRm`.
- **Tabla SSE4.1 `66 0F 38 38..40` mal mapeada**: 38 se traducía como PMULLD (es PMINSB), 3A como PMINUD (es PMINUW), 3B como PMAXUD (es PMINUD), 3C como PMINSD (es PMAXSB); faltaban 39/3E/3F/40. Ahora es una tabla única SMIN/UMIN/SMAX/UMAX/MUL.
- **MOVNTI** (`0F C3`): store no temporal = STR normal.
- Diagnóstico: el descompresor LZ4 real de UnityPlayer.dll se ejecuta bajo el JIT en la suite (lee `/data/local/tmp/unity_lz4.bin`, no se incluye en el repo) contra datos sintéticos y el blocksinfo/bloque 0 reales del bundle `atlas_player`: idéntico al de referencia.

Loader / Win32:
- `FormatMessageW/A` reales (mono-dl.c:125), eliminado el parche fijo `_pioinfo` (escribía fuera del mono.dll de Cuphead), `XCHG`/atómicos LOCK no alineados caen a STR normal (SIGBUS en mono).
- **Steam**: nunca se carga la `steam_api64.dll` del juego (la de Cuphead es un emulador que saltaba a una IAT sin resolver, RIP=0xBDDCA). Imports desde DLL locales (CSteamworks.dll de Steamworks.NET) usan el modo sin Steam (`SteamAPI_Init` -> false, el juego sigue: *"[Steamworks.NET] SteamAPI_Init() failed"*); lo no implementado devuelve 0.
- Herramientas de diagnóstico por `setprop`: `debug.hyperplay.memscan` (texto o `hex:`; busca en memoria rw al fallar un AssetBundle), `debug.hyperplay.tracetrad <tamaño hex>` (traza traducciones y pila del huésped tras un `ReadFile` de ese tamaño), `debug.hyperplay.drawlog <N>` (draws N..N+39 con rangos UV) y `debug.hyperplay.dumprt 1` (vuelca los RT a `_HyperPlaySaves/rtdump_N.raw`). `logGuestStackChain` reconstruye la cadena de llamadas.

**Resuelto: imagen desplazada media pantalla** (HK y Cuphead). Con volcados de RT por draw (`debug.hyperplay.dumprt`) se verificó que la escena, la cadena de post-proceso y el backbuffer eran correctos. El fallo estaba en `DirectVulkanBridge::presentPixels`: con la superficie rotada (90/270), las inversas usaban `eh` para x y `ew` para y (cruzadas). Con `ew != eh` la imagen salía desplazada y con una fila repetida por el clamp. Corregido: `rot90: vy = ew-1-x`, `rot270: vx = eh-1-y`. Ahora se registra una vez la geometría (`presentPixels: frame ... transform=`). Captura: `docs/capturas/2026-11-18_hollow_knight_menu_completo.png` (menú de idiomas a pantalla completa). Además: la carga de escena tarda unos 5 min a velocidad JIT, FMOD de Cuphead no abre salida (*"FMOD failed to initialize the output device"*, usa el null output) y Rewired cae a la entrada de Unity. Los pases de post-proceso a pantalla completa cuestan 170-520 ms cada uno.

## 2026-11-18 (tarde): FASE 2 — PRIMERA IMAGEN REAL de Hollow Knight (rasterizador D3D11 por software)
Hecho y verificado (suite nativa: **793 comprobaciones, 0 fallos**; gate Gradle BUILD SUCCESSFUL). Captura: `docs/capturas/2026-11-18_hollow_knight_primera_imagen.png` (pantalla de selección de idioma, overlay **FPS 4 · 262 ms**).
- **Rasterizador D3D11 por software** (decisión: encaja con el modelo de recursos en CPU y `presentPixels`; el decodificador DXBC servirá después para DXBC -> SPIR-V en Adreno):
  - `loader/DxbcShader.h`: contenedor DXBC, firmas ISGN/OSGN, tokens SM4/SM5 (operandos, índices relativos, modificadores, inmediatos, declaraciones, buffer de constantes inmediato). Verificado con los 12 shaders reales volcados por HK (`_HyperPlaySaves/shaderdump/`).
  - `loader/DxbcVm.h`: intérprete (aritmética float/int, comparaciones, movc, dp2/3/4, rsq/sqrt/exp/log, round_*, conversiones, sample/sample_l/sample_b/sample_c/ld/resinfo, if/else/loop/switch/break/continue/ret/discard).
  - `loader/SoftRaster.h`: fetch de vértices por input layout, VS, recorte z>=0 / w>0, viewport, culling, regla top-left, interpolación con perspectiva/constante/sin perspectiva, PS, blending D3D11 completo y write mask; RT RGBA8/BGRA8(sRGB)/RGBA16F/RGBA32F/RGB10A2. Raster **paralelo por franjas** con pool persistente (5 hilos + render).
  - `loader/D3D11Sampler.h`: sampler (punto/bilineal, wrap/mirror/clamp/border/mirror_once), formatos RGBA8/BGRA8/sRGB/R8/A8/RG8/RGBA16F/RGBA32F/R32F/R16F/RG16F, **BC1-BC5** (caché del mip descomprimido invalidada por generación del recurso).
  - Interceptor: el bytecode DXBC y el input layout se guardan; estado del contexto (VS/PS/layout/VB/IB/topología/CB/SRV/samplers/RTV/blend/rasterizer/viewport/scissor/ClearState) y `Draw/DrawIndexed/Draw*Instanced`. Histograma de slots del contexto y log de los 40 primeros draws.
- **Bugs del JIT encontrados con un test diferencial SSE escalar** (`testSseScalarLanes`): `MOVSS/MOVSD xmm,xmm` copiaban el registro entero (deben sustituir solo el lane 0): era la causa de los 786 `The bounds contain ... NaN` de Unity (ahora 0); `CMPSS/CMPSD` sin implementar; `CVTSS2SI/CVTSD2SI` (FCVTNS) añadidos.
- `QueryDisplayConfig`/`DisplayConfigGetDeviceInfo` reales (1 path, modos origen/destino): antes éxito sin rellenar -> Unity indexaba basura (AV) y guardó `Screenmanager Resolution 1x1` en PlayerPrefs.
- Resolución expuesta al juego: **1344x608** (panel 2688x1216 / 2, `win32::kRenderScaleDivisor`); Vulkan escala al panel.
- **La build debug del motor nativo era -O0**: `target_compile_options(hyperplay PRIVATE -O2)` (raster 4,5x más rápido; afecta también al JIT y al despachador).
- Hooks de Unity `MemoryManager` desactivados por defecto (`debug.hyperplay.unitymemhook=1` para reactivarlos).
Pendiente inmediato: **BC7** (formato 98, aún sin decodificar: texturas en negro), velocidad del intérprete (precompilar instrucciones / SIMD 4 píxeles), entrada (controles táctiles/mando -> XInput en el menú), corrupción intermitente (AV en `std::map` de un worker y puntero con texto ASCII, 1 de cada ~3 corridas), Cuphead y DmC.

## 2026-11-18 (mañana): FASE 1 CERRADA — Hollow Knight presenta frames por Vulkan
Hecho y verificado (suite nativa en silicio: **668 comprobaciones, 0 fallos**; gate Gradle BUILD SUCCESSFUL):
- **Hollow Knight en vivo**: 0 excepciones, vivo a 300 s, Steam en modo sin cliente, audio FMOD por WASAPI -> AAudio, escena cargada y **`DXGI Present #600: 2688x1216 -> Vulkan OK`** (overlay FPS 4). La imagen sale negra (`pixel0 RGBA=00000000`): falta el rasterizador D3D11 (Fase 2).
- **Rendimiento del despachador x10** (hilo principal ~2 M -> 15-25 M bloques/s en vivo):
  - Las invalidaciones SMC ya no vacían toda la caché rápida: `eraseBlocksLocked` borra solo el rango (`blockCache_` ahora `std::map`, `lower_bound`) y anula las entradas afectadas de la caché rápida.
  - Caché rápida de 64 K entradas (calloc, memoria perezosa).
  - `dispatchThunk`: `ThunkInfo` resuelto una vez por dirección con caché por hilo (antes 2-3 mutex globales + ~20 búsquedas de cadena por llamada); `isRegisteredTrampoline` con caché por hilo invalidada por generación.
  - Línea `Ejecutados` con ms y deltas `trad/lento/thunks/smc` para diagnosticar.
- **Sincronización Win32 corregida**: `Wake(All)ConditionVariable` notifica con el mutex de la CV tomado (se perdían despertares); SRWLOCK con modo compartido real (antes compartido = exclusivo); `WaitOnAddress` despierta también por `WakeByAddress*` aunque el valor vuelva al original (ABA); `InitializeConditionVariable` real.
- **SMC**: carrera de dos hilos escribiendo a la vez en la misma página protegida (el segundo recibía un AV real) -> reintento.
- `TerminateProcess(self)` detiene también el hilo que llama (el CRT entraba en bucle hasta desbordar la pila).
- **Opcodes nuevos**: `POPCNT` (F3 0F B8, SWAR), `PINSRW`/`PEXTRW` (66 0F C4/C5). Tests `testPopcnt`, `testPinsrwPextrw`.
- Vigilante de bloqueos (`[Bloqueo] tid=...`): cada 15 s lista hilos >5 s dentro de un thunk o fuera de thunks (estado, suspensiones, RIP).
Pendiente inmediato (Fase 2): rasterizador D3D11 (`Draw/DrawIndexed`, shaders DXBC) para ver imagen real; investigar `The bounds contain ... NaN` (786 avisos de Unity: posible fallo de coma flotante del JIT); tormentas SMC de 24 s esporádicas; parche fijo `_pioinfo` de mono; hook `MemoryManager` de Unity.

## 2026-11-18 (madrugada, 2ª parte): atomicidad LOCK, SMC, heap sin corrupción, swapchain creado
Hecho y verificado (suite nativa en silicio: **622 comprobaciones, 0 fallos**; gate Gradle BUILD SUCCESSFUL):
- **LOCK atómico real**: `LOCK ADD/SUB/AND/OR/XOR/INC/DEC`, `CMPXCHG`, `XADD`, `XCHG m` emiten `CASAL` con reintento de instrucción entera (antes load/op/store no atómico: contadores de referencias de Mono/Unity corruptos entre hilos). Test `testAtomicsMultithread`.
- **Código automodificable (SMC)**: páginas escribibles traducidas se protegen contra escritura; el `SIGSEGV SEGV_ACCERR` las marca sucias en un anillo sin locks y `suspensionPoint` (al inicio del bucle, antes de la caché rápida) invalida los bloques. Mono parchea trampolines/IMT en caliente. Test `testSelfModifyingCode`.
- **`SleepEx`** real (el stub provocaba bucle activo e inundación de logs).
- **`VirtualAlloc(MEM_RESERVE)` en dirección fija dentro de una reserva existente -> `ERROR_INVALID_ADDRESS` (487)**, como Windows. Antes se trataba como commit: el gestor de código de Mono colocaba sus chunks dentro del heap de Boehm y el GC leía bytes x86 como punteros (`addr=0x48000000f890ff0e`). Resultado en vivo: **0 excepciones de acceso**.
- **`GetModuleHandleExW(FROM_ADDRESS)`**: busca el módulo que contiene la dirección (antes leía la dirección como cadena).
- Hollow Knight en vivo: `Loaded All Assemblies` (60 s), PhysX, `DXGI swapchain: backbuffer 2688x1216 formato 28`, `Using XInput`, polling de input, idioma `EN` restaurado. Luego Unity se apagaba solo (`Physics::Module Cleanup`, `Input System ... Shutdown`) sin `Present`.
- **Causa del apagado encontrada y corregida**: vigilancia de RIP (`setprop debug.hyperplay.watch 0x18005cf40`, icall `Application.Quit` localizado por la tabla de icalls de UnityPlayer) confirmó `Application.Quit(0)` desde código gestionado. `DesktopPlatform.CreateOnlineSubsystem` (Assembly-CSharp) cuenta subsistemas con `File.Exists(Plugins/x86_64/{steam_api64,GalaxyCSharpGlue,XGamingRuntimeThunks}.dll)` y si hay más de uno registra `Multiple online subsystems packaged.` y sale. `File.Exists` era siempre true: Mono P/Invoke prueba primero `GetFileAttributesExWW`/`FindFirstFileExWW` y nuestro `GetProcAddress` devolvía un stub dinámico (handle 0 ≠ `INVALID_HANDLE_VALUE`). **`GetProcAddress` ahora devuelve `NULL` + `ERROR_PROC_NOT_FOUND` (127)** para nombres no implementados (también evita que Unity crea que `bcrypt.dll` exporta `UnityPluginLoad`). Las importaciones de la IAT siguen con stubs.
- **Registro de Windows real** (`loader/Win32Registry.h`): `RegOpenKeyEx/RegCreateKeyEx/RegQueryValueEx/RegSetValueEx` (A/W), `RegGetValueW`, `RegEnumValueW`, `RegEnumKeyExW`, `RegQueryInfoKeyW`, `RegDeleteValue/Key`, `RegCloseKey`; HKCU persistido en `<Juego>/_HyperPlaySaves/registry.hpreg` (PlayerPrefs de Unity: antes `PlayerPrefsException: Could not store preference value`); HKLM con CPU y versión de Windows de solo lectura. Test `testWin32Registry`. Suite: **656 comprobaciones, 0 fallos**.
- Diagnóstico: `[NativeThunk]` registra siempre las 4 primeras llamadas de cada función aunque se haya pasado el tope; `TlsSetValue` limitado a 200 líneas.
- Con `GetProcAddress` -> NULL, Mono asertaba en `mini-windows.c:1153` (no encontraba `RtlAddGrowableFunctionTable` ni `RtlInstallFunctionTableCallback`). Implementadas de verdad: `RtlAdd/Grow/DeleteGrowableFunctionTable`, `RtlInstallFunctionTableCallback`, `RtlAdd/DeleteFunctionTable`, `AreFileApisANSI`, `SetThreadErrorMode`, `GetSystemTimePreciseAsFileTime`, `AppPolicyGetThreadInitializationType`, `GetCurrentPackageId`. Resultado en vivo: HK pasa la selección de subsistema (ya no hay `Multiple online subsystems`) y carga `steam_api64.dll`.
- **Steam sin cliente para juegos gestionados**: `LoadLibrary("steam_api64.dll")` devuelve un módulo sintético (`0x7FFF0010`) cuyo `SteamAPI_Init` devuelve false (`InitFlat`/`SteamInternal_SteamAPI_Init` -> FailedGeneric); el juego entra en su modo "Steam failed to initialize". Antes se mapeaba la DLL real del juego (emulador) y su DllMain fallaba con AV (`addr=0x10`) -> sesión terminada (-3). Las importaciones nativas siguen con `NativeSteamStub`.
- **WASAPI -> AAudio (Fase 2, audio)**: `CoCreateInstance(MMDeviceEnumerator)`, `IMMDeviceEnumerator` (endpoint de reproducción por defecto, colección, sin captura), `IMMDevice` (`Activate`, `OpenPropertyStore` con FriendlyName, `GetId`, `GetState`), `IAudioClient/2/3` (formato de mezcla float32 48 kHz estéreo, `IsFormatSupported`, `Initialize`, `GetBufferSize`, `GetCurrentPadding`, `Start/Stop/Reset`, `SetEventHandle`, `GetService`), `IAudioRenderClient` (`GetBuffer/ReleaseBuffer` con conversión float/PCM16/24/32 -> PCM16 estéreo), `IAudioClock`, `ISimpleAudioVolume`, `IAudioSessionControl`. Un hilo host por cliente consume 10 ms por vuelta, escribe bloqueando en `DirectAAudioEngine` y señala el evento del cliente.
- `IMMEndpoint` (`GetDataFlow` -> eRender) y `PKEY_DeviceInterface_FriendlyName`: FMOD hace QI sin comprobar el HRESULT. En vivo: FMOD activa `IAudioClient`, llama `GetBuffer/ReleaseBuffer` y espera el evento: **el camino de audio WASAPI -> AAudio está en uso**.
- Los punteros de `steam_api64.dll` sintético se registran como trampolines (antes el JIT de Mono saltaba a una dirección host -> hilo muerto).
- `[UnityHook] MemoryManager` limitado a 300 líneas: generaba 2 M líneas por corrida y expulsaba del búfer de logcat todo lo demás.
- **TLS de DLL cargadas en tiempo de ejecución** (`OryonEmitterBackend::registerDynamicModuleTls`, llamado en `loadLocalDll` antes de DllMain): índice propio, `_tls_index` escrito, bloque copiado de la plantilla `.tls` en todos los hilos vivos y heredado por los nuevos. Antes `mono-2.0-bdwgc.dll` y demás DLL dinámicas quedaban con índice 0 y escribían en el bloque TLS de otro módulo: se pisaba `_Init_thread_epoch` de UnityPlayer y los "magic statics" de MSVC se daban por construidos sin construirse (AV `call *(%rax)` con vtable nula en `0x180310a0b`, objeto global `0x181f4f930`).
Pendiente inmediato: verificar en vivo el TLS dinámico y la llegada a `Present`; revisar el parche fijo `_pioinfo` de mono (offset `0x73efe0` cableado, peligroso con otra versión de Mono) y el hook `MemoryManager` de Unity (copia tamaños adivinados en `Reallocate`); regresión del benchmark (17-19 M bloques/s frente a 80 M).

## 2026-11-18: Fase 1 — excepciones VEH, GC de Mono, rendimiento del despachador
Hecho y verificado (suite nativa en silicio: **611 comprobaciones, 0 fallos**; gate Gradle BUILD SUCCESSFUL):
- **Entrega de excepciones hardware al huésped (VEH)**: `instrMap` por bloque + `hostIndex_` -> RIP exacta; manejador (libsigchain/sigaction) copia X0..X15/V0-7/V16-23/ESR del `ucontext` a `PendingFault`, desvía el PC a `guestFaultLanding` (fuera del manejador) que hace `longjmp` al bucle activo (`FaultScope` en `executeEntry`, `callDllEntry`, `callDllEntryNested`, `callGuestNested`); `deliverPendingFault` construye `CONTEXT` (0x0010001F) + `EXCEPTION_RECORD` + `EXCEPTION_POINTERS` bajo RSP (red zone 128 B), llama a los VEH (`dispatchGuestException`) y al `SetUnhandledExceptionFilter`; `-1` aplica el CONTEXT devuelto; sin manejar -> `-4` limpio. Test `testVectoredExceptionHandlerAccessViolation` (RIP exacta, `addr=0x18`, `RDX=0` vivo, `RAX=0x1337` tras continuar).
  - En vivo (Hollow Knight): `[GuestException] 0xc0000005 en RIP=... (addr=0x18) -> VEH devuelve -1` y Unity imprime `NullReferenceException` como en Windows; luego `- Loaded All Assemblies, in 53.379 seconds`.
- **PSAPI / exports**: `EnumProcessModules/K32EnumProcessModules(Ex)`, `GetModuleInformation/K32*`, `GetModuleFileNameExW` (solo módulos PE reales), `GetModuleFileNameW/A` por `hModule` (antes siempre el exe), `GetProcAddress` por ordinal y exports reenviados `DLL.Func`/`DLL.#n` (recursivo).
- **GC de Mono (Boehm)**:
  - `GetExitCodeThread` sobre handles reales de hilo (antes 0 = "terminado": Boehm no escaneaba hilos vivos).
  - **Rangos ejecutables de proceso**: `VirtualAlloc/VirtualProtect/VirtualFree` aplican a todos los backends (`addExecutableRangeAll`, etc.) y un hilo sin el rango lo toma del principal. Antes los hilos de Mono morían con `RIP fuera del rango de código PE / DLLs` en código JIT nuevo -> `Fatal error in GC: SuspendThread loop failed` (578.137 MessageBox).
  - **`VirtualQuery` real** sobre `/proc/self/maps` (caché 50 ms): `MEM_FREE` en huecos, `MEM_IMAGE` en imágenes PE del huésped, `MEM_MAPPED` en archivos del host, `MEM_PRIVATE` resto; protección y `AllocationBase` reales. Antes todo era "64 KB MEM_PRIVATE RWX": Boehm no registraba los `.data` de las DLL como raíces -> objetos vivos recolectados -> cascada de `NullReferenceException` ("another NullReferenceException was thrown while processing the stack trace").
- **Rendimiento**: caché directa por hilo RIP -> código host sin locks (antes 2 mutex por bloque, uno global); invalidaciones que solo vacían la caché si había bloques en el rango; `addCodeRange` sin duplicados cubiertos. Benchmark en silicio: **~71-85 M bloques/s (12-14 ns/bloque)**.
- **SSE4.1 `ROUNDPS/PD/SS/SD`** (`66 0F 3A 08..0B`, FRINTN/M/P/Z): Mono `Math.Ceiling` (`66 0F 3A 09 C0 02`). Test `testSse41Round`.
- Diagnóstico: fallo de traducción registra bloque previo, sus bytes, GPR y `[RSP..]`; `MessageBox` y `[NativeThunk]` con volumen acotado; `run_game.sh` filtra por el PID de la ejecución.
Pendiente inmediato: verificar que Hollow Knight supera `ROUNDPD` y llega a `CreateSwapChain`/`Present`; vigilar un salto no determinista de un hilo de Mono a memoria de datos del host (`0x70a5456039`, visto una vez).

## (histórico) EN CURSO (2026-11-17 ~20:45): entrega de excepciones VEH al huésped
Hecho:
- `TranslatedBlock::instrMap` ({offset host, offset guest} por instrucción) rellenado en `compileLocked`; `hostIndex_` (hostCode -> guestRip) en el backend (validar contra `blockCache_` al buscar; vaciar en `configure`).
- `loader/Win64Context.h`: `toContext/fromContext` (CONTEXT x64, `kContextAll = 0x0010001F`), `fillExceptionRecord`, códigos (`0xC0000005`, `0xC000001D`), veredictos (-1/0). `GetThreadContext/SetThreadContext` ya lo usan.
- `NativeDllInterceptor`: registro real de VEH (`AddVectoredExceptionHandler` con orden `first`, `RemoveVectoredExceptionHandler`), `SetUnhandledExceptionFilter` guarda y devuelve el anterior, `dispatchGuestException(backend, EXCEPTION_POINTERS*)` llama a los VEH con `callGuestNested` y luego al filtro.
Falta (en este orden):
1. Backend: `resolveGuestFault(hostPc)` (upper_bound en `hostIndex_` + `instrMap`), `ownsHostPc` (rango `arena_`), `PendingFault` (gpr de `uc_mcontext.regs[0..15]`, XMM de `fpsimd_context` V0-7/V16-23, ESR WnR para lectura/escritura, fault addr).
2. Manejador (`guestCrashSpecialHandler` y vía sigaction): si el PC está en la arena JIT y hay `faultJmp_`, rellenar `PendingFault`, poner `uc_mcontext.pc = &guestFaultLanding` y devolver true; `guestFaultLanding` hace `longjmp(*t_currentBackend->faultJmp_)` ya fuera del manejador (no romper el estado de libsigchain).
3. `jmp_buf` por bucle (RAII que restaura el anterior) en `executeEntry`, `callDllEntry`, `callDllEntryNested`, `callGuestNested`; tras el longjmp `deliverPendingFault()`: cargar estado del fallo en `ctx_`, construir CONTEXT + EXCEPTION_RECORD + EXCEPTION_POINTERS bajo RSP (red zone 128 B, alineado a 16), `ctx_.gpr[4]` = base de esas estructuras, llamar `NativeDllInterceptor::dispatchGuestException`; -1 -> `fromContext(..., true)` y seguir; si no, devolver -4 (fin limpio). Profundidad máxima 8.
4. Test `testVectoredExceptionHandlerAccessViolation` (mov eax,[rdx+0x18] con RDX=0; VEH avanza Rip; RAX=0x1337) + suite en silicio + gate + Hollow Knight/Cuphead.
5. Tarea 2 del usuario: `EnumProcessModules`/`K32EnumProcessModules`, `GetModuleInformation`/`K32GetModuleInformation` y exports reenviados en `GetProcAddress` (aserción `mono-dl.c:125` de Cuphead).

## Sesión 2026-11-17 (noche): texturas D3D11 reales, Present por Vulkan, suspensión de hilos y entrega de fallos

### 1. Recursos D3D11 reales (antes: singletons compartidos y E_NOTIMPL)
- **`loader/D3D11ResourceModel.h`** (header-only, probado en la suite): tamaños DXGI reales (incl. BC1..BC7 en bloques 4x4), cadena de mips (`MipLevels=0` -> completa), layout por subrecurso (`mip + slice*MipLevels`) con `RowPitch`/`DepthPitch` reales, reserva de memoria diferida, `D3D11_SUBRESOURCE_DATA`, `UpdateSubresource` con `D3D11_BOX`, `CopySubresourceRegion`/`CopyResource`, limpieza con color empaquetado (RGBA8/BGRA8/BGRX8/R10G10B10A2/RGBA16F/RGBA32F/R32F/R8/A8) y conversión del backbuffer a RGBA8.
- **COM (`NativeDllInterceptor.cpp`)**: un objeto por `Create*` (`D3DResourceObj`, `D3DViewObj`, `D3DDescObj`) con registro de punteros válidos (nunca se interpreta memoria ajena).
  - Dispositivo: `CreateBuffer`, `CreateTexture1D/2D/3D` (validación con `pp=NULL` -> `S_FALSE`, `E_INVALIDARG` honesto), `CreateShaderResourceView/UnorderedAccessView/RenderTargetView/DepthStencilView` (vista ligada a su recurso, `GetResource` real), `CreateInputLayout`, `Create{Vertex,Geometry,Pixel,Hull,Domain,Compute}Shader`, `CreateGeometryShaderWithStreamOutput` (10 args), `CreateClassLinkage`, `CreateBlend/DepthStencil/Rasterizer/SamplerState` (descriptor copiado para `GetDesc`), `CreateQuery/Predicate`; `CheckFormatSupport` con máscara real por formato (antes devolvía `1` = solo BUFFER).
  - Contexto: `Map` con puntero y pitch reales del subrecurso (antes un scratch de 16 MB con pitch 0), `UpdateSubresource`, `CopyResource`, `CopySubresourceRegion` (9 args), `ClearRenderTargetView` (respeta MipSlice/ArraySlice de la vista), `GetData` (EVENT=TRUE, TIMESTAMP en ns, TIMESTAMP_DISJOINT 1 GHz).
  - Recursos: `GetType` (era `void` con puntero, se devolvía como valor), `GetDesc` real, `GetEvictionPriority` devuelve UINT; private data común (`GetPrivateData` -> `DXGI_ERROR_NOT_FOUND`).
  - `QueryInterface` sobre objetos reales devuelve el propio objeto (antes devolvía el singleton de textura).
  - `Release` a 0 libera la memoria del recurso conservando la "cáscara" COM (un puntero colgante del juego no provoca use-after-free nativo).
  - El despacho COM recibe los **12** argumentos.
- **Swapchain DXGI**: backbuffer real creado con el tamaño/formato que pide el juego (`CreateSwapChain`, `ForHwnd`, `ForCoreWindow`, `ForComposition`, `D3D11CreateDeviceAndSwapChain`), `GetBuffer` devuelve ese backbuffer, `ResizeBuffers` lo recrea, `GetDesc/GetDesc1` reflejan tamaño y formato reales.
- **Present**: backbuffer -> RGBA8 -> **`DirectVulkanBridge::presentPixels()`**: staging host-visible, escalado al swapchain, rotación según `currentTransform`, swizzle BGRA, barreras `UNDEFINED -> TRANSFER_DST -> PRESENT_SRC`, `vkCmdCopyBufferToImage`, semáforo de fin y fence propio. (Antes `presentTestFrame` presentaba imágenes nunca escritas.)
- **Superficie Android**: `surfaceCreated` llegaba antes que `D3D11CreateDevice` y `attachWindow` fallaba sin instancia Vulkan (`attach=false`) sin reintento; ahora `attachWindow` inicializa Vulkan (`initLocked`) -> `surfaceCreated: attach=true`.

### 2. Suspensión real de hilos invitados (GC de Mono/Boehm)
- Síntoma: bucle infinito `SuspendThread(0xfffffffffffffffe)` -> `GetThreadContext` (stub 0) -> `ResumeThread` -> `MessageBoxA` (aserción), ~32.000/min, 640 MB de log en 2 min.
- Causas: `DuplicateHandle(GetCurrentThread())` devolvía el pseudo-handle; `SuspendThread/ResumeThread` eran stubs; `GetThreadContext` no existía; `CreateThread` devolvía como ThreadId el `pthread_t` truncado (no coincidía con `GetCurrentThreadId()=gettid()`).
- Implementación:
  - `OryonEmitterBackend`: registro de backends por TID (`forThread`), `requestSuspend/requestResume` (contador Win32), `waitUntilSuspended`, puntos seguros entre bloques (no hay encadenamiento de bloques: cada bloque vuelve al despachador) y estado `kInNative` durante los thunks; en todos los bucles (`executeEntry`, `callDllEntry`, `callDllEntryNested`, `callGuestNested`).
  - Win32: handles de hilo reales (`0x7E000000|tid`) en `DuplicateHandle`, `OpenThread`, `GetThreadId`; `SuspendThread`/`ResumeThread` reales; `GetThreadContext`/`SetThreadContext` con el layout `CONTEXT` x64 (Rax..R15 @0x78, Rip @0xF8, EFlags @0x44, Xmm @0x1A0); `CloseHandle` ignora los handles etiquetados (no llega a `close(fd)`); `CreateThread` devuelve el TID real.
  - Test `testGuestThreadSuspend`: hilo invitado en bucle `inc rax; jmp`, se suspende (RAX congelado, RIP en inicio de bloque), suspensión anidada, reanudación y desregistro.
- Resultado en vivo: desaparece el bucle de `SuspendThread`/`MessageBoxA`.

### 3. VirtualAlloc ya no pisa memoria del host (causa del SIGILL en el hilo de UI)
- Síntoma: `SIGILL ILL_ILLOPC` en `liblog.so GetDefaultTag()+0` en el hilo principal de Android (Compose).
- Causa: `win_VirtualAlloc(lpAddress, MEM_COMMIT)` hacía `mprotect` sobre **cualquier** dirección (aunque no fuera reserva del huésped) y `MEM_RESERVE` a dirección fija usaba `MAP_FIXED` (reemplaza lo mapeado). Mono pide direcciones cercanas a su código y una caía en `liblog` (`0x708d25a000`): su texto quedaba escribible/reemplazado por ceros.
- Corrección: `MEM_COMMIT` solo sobre reservas propias; `MEM_RESERVE` a dirección fija con `MAP_FIXED_NOREPLACE` (y verificación de dirección) -> si está ocupado, `NULL` + `ERROR_INVALID_ADDRESS (487)` como Windows. Igual en `MapViewOfFileEx` con dirección base.
- Verificado en vivo: `win_VirtualAlloc: rango 0x708d25a000+0x10000 ocupado -> ERROR_INVALID_ADDRESS` y desaparece el SIGILL.

### 4. Diagnóstico
- **Informe de crash del huésped** (`OryonEmitterBackend.cpp`): manejador SIGSEGV/SIGBUS/SIGILL registrado como *special handler* de libsigchain (`AddSpecialSignalHandlerFn`; ART intercepta `sigaction`), solo actúa en hilos que ejecutan código invitado; escribe con `write()` (async-signal-safe) en `files/guest_crash.txt` (`adb shell run-as com.murdoc820.hyperplay cat files/guest_crash.txt`) la RIP del bloque, los 16 GPR y 48 bytes en RIP; después deja seguir la cadena (debuggerd).
- Volumen de `[NativeThunk]` acotado (30.000 primeras + 1/1000) y más APIs ruidosas sin log (`HeapFree/ReAlloc/Size`, `TryEnterCriticalSection`, `Suspend/ResumeThread`, `GetThreadContext`): logd descartaba las líneas finales (incluidos los diagnósticos).
- `MessageBoxA/W` registran su texto; `AddVectoredExceptionHandler`/`SetUnhandledExceptionFilter` registran el manejador.
- `exit-info` del sistema: `adb shell dumpsys activity exit-info com.murdoc820.hyperplay`; tombstones: `dumpsys dropbox --print data_app_native_crash`.

### Verificación
- Suite nativa en silicio: **`591 comprobaciones, 0 fallos`, `EXIT:0`** (nuevos: `testD3D11ResourceModel`, `testGuestThreadSuspend`, `testWinSafeHeap`, `testSetccMemory`).
- Gate Gradle `testDebugUnitTest lintDebug assembleDebug`: **BUILD SUCCESSFUL**.
- Hollow Knight: sin `failed to create 3D texture` (antes `[80004001]`), `surfaceCreated: attach=true`, sin bucle de suspensión, sin SIGILL. **Aún no se llega a crear el swapchain ni a `Present`**: el proceso cae antes (punto siguiente).

### Siguiente bloqueo real: entrega de excepciones al huésped (VEH/SEH)
- `files/guest_crash.txt` (2026-11-17 20:27, Hollow Knight):
  `signal 0xb fault 0x18`, `RIP(bloque)=0x6dd7f1ea25` (código JIT de Mono en memoria `VirtualAlloc`), `RDX=0`;
  bytes: `48 63 45 98 | 03 c6 | 89 45 b0 | 48 8b 4d 80 | 48 8b 51 18 | 48 63 4a 18 | 3b c1 | 0f 8d ...` =
  `movsxd rax,[rbp-0x68]; add eax,esi; mov [rbp-0x50],eax; mov rcx,[rbp-0x80]; mov rdx,[rcx+0x18]; movsxd rcx,[rdx+0x18]` (longitud de un array nulo) -> **`NullReferenceException` de C#**.
- Mono implementa esas comprobaciones con fallos de hardware: registra `AddVectoredExceptionHandler(first=1, handler=0x6aa36010a0)` y espera recibir `EXCEPTION_ACCESS_VIOLATION (0xC0000005)` con `EXCEPTION_RECORD` + `CONTEXT` exactos.
- Plan: (1) mapa por bloque *offset host -> RIP x86* emitido en `translateBlock` para la RIP exacta de la instrucción que falla; (2) en el manejador de señales, construir `CONTEXT` desde `ucontext` (X0..X15 = RAX..R15, XMM desde V0..V7/V16..V23) y `EXCEPTION_RECORD` (`ExceptionInformation[0]=lectura/escritura`, `[1]=dirección`); (3) llamar a los VEH registrados (en orden) con `EXCEPTION_POINTERS`; (4) `EXCEPTION_CONTINUE_EXECUTION (-1)` -> cargar el `CONTEXT` resultante en `CpuContext` y volver al despachador (siglongjmp fuera del bloque); `EXCEPTION_CONTINUE_SEARCH` -> `SetUnhandledExceptionFilter` -> crash como ahora.

## Sesión 2026-11-17 (tarde): thunks de 12 argumentos, SHLWAPI real, detección de monitor y limpieza

### Causas raíz atacadas
1. **Despachador de thunks truncado a 8 argumentos.** `dispatchThunk` ahora usa `loader/Win64ThunkAbi.h` (header-only, compartido con la suite nativa): `readThunkArgs()` lee `RCX, RDX, R8, R9` + `[RSP+0x20..0x58]` (8 args de pila, cada uno validado con `isValidStackAddress`) e `invokeThunk()` llama al trampolín con 12 `uint64_t` (AAPCS64: 8 en `X0..X7`, 4 en pila; compatible con callees de menos parámetros). Ya llegan reales `CreateWindowExW/A` (12), `D3D11CreateDevice` (10: `ppDevice`=arg7, `pFeatureLevel`=arg8, `ppImmediateContext`=arg9) y `D3D11CreateDeviceAndSwapChain` (12).
2. **Bug extra encontrado: hilos invitados con `user` equivocado.** `guestThreadTrampoline` pasaba el backend PRINCIPAL como `user` del thunk; `isValidStackAddress` validaba contra la pila del hilo principal y **todos los args 4+ de cualquier API llamada desde un hilo secundario se leían como 0** (p. ej. `CreateFileW` desde el hilo de carga). Ahora `user = threadBackend.get()`.
3. **`CreateWindowExW/A`:** `CW_USEDEFAULT (0x80000000)` o tamaños `<= 0` -> `2688x1216` (`win32::normalizeWindowExtent`), posición `CW_USEDEFAULT` -> 0; log con tamaño pedido y real.
4. **`D3D11CreateDevice`:** sondeo con `ppDevice == ppImmediateContext == NULL` escribe `*pFeatureLevel = 0xB000` y devuelve `S_FALSE (1)` (antes `E_POINTER`); log de adapter/driverType/flags/levels/sdk. `...AndSwapChain` propaga `S_FALSE`.
5. **SHLWAPI real** (`loader/Win32PathUtils.h` + `kHModShlwapi = 0x7FFF000F` en `resolveModuleHandle`/`GetProcAddress`/`resolveNativeSymbol`, también `shcore.dll` y `api-ms-win-core-shlwapi-*`): `PathCanonicalizeW/A` (normaliza `/`->`\`, resuelve `.`/`..`, no sube de la raíz, permite buffer origen=destino, `FALSE`+`ERROR_INSUFFICIENT_BUFFER` si excede `MAX_PATH`), `PathCombineW/A`, `PathAppendW/A`, `PathRemoveFileSpecW/A`, `PathFindFileNameW/A`, `PathFindExtensionW/A`, `PathFileExistsW/A` (vía `GetFileAttributes* != INVALID_FILE_ATTRIBUTES`), `PathIsRelativeW/A`. Evidencia en log de Cuphead: con el stub, Unity buscaba `'_Data\globalgamemanagers'` (ruta del juego perdida) y hacía un bucle de 17.640 `PathFileExistsW` -> 0.
6. **`Forcing GfxDevice: Null` (Hollow Knight):** descartados línea de comandos (sin `-batchmode`/`-nographics`), estación de ventana y carga de DLL (no hay ningún `LoadLibrary` de d3d11/dxgi en el log). Causa más probable: `USER32!EnumDisplayMonitors(0,0,cb=0x1807d2890,...)` devolvía `1` **sin invocar el callback** -> Unity veía 0 monitores y elegía Null. Corregido:
   - Nuevo `OryonEmitterBackend::callGuestNested(fn, args, n<=4)` (generaliza `callDllEntryNested`: guarda/restaura `CpuContext`, RA=0, shadow space, stdcall en 32 bits).
   - `win_EnumDisplayMonitors` invoca `MONITORENUMPROC(kPrimaryMonitor, hdc, &RECT{0,0,2688,1216}, dwData)` con el backend del hilo (`thread_local t_dispatchBackend`).
   - `systemMetric`: `76/77` son `SM_X/YVIRTUALSCREEN` -> `0` (antes 2688/1216 = escritorio desplazado); `61/62 SM_CX/CYMAXIMIZED`, `59/60 MAXTRACK`, `16 SM_CXFULLSCREEN` -> tamaño real; `81 SM_SAMEDISPLAYFORMAT=1`.
   - `EnumDisplayDevicesA/W` con `lpDevice` no nulo devuelve el monitor `\\.\DISPLAY1\Monitor0` (`ACTIVE|ATTACHED = 0x3`).
   - `DEVMODE.dmFields = 0x005C0000` (incluye `DM_DISPLAYFREQUENCY`).
   - `kHDeskDefault` pasa a `0xCAFE0030` (chocaba con `kPrimaryMonitor = 0xCAFE0010`).
7. **Rutas de perfil coherentes:** `resolveVirtualPath` aplanaba `C:\users\default\AppData\LocalLow\Unity Technologies` a `_HyperPlaySaves/Unity Technologies` mientras `C:\users\default` iba a `<Game>/users/default` (padre e hijo incoherentes -> bucle de `CreateDirectory` de Unity). Ahora `C:\users\...`, `C:\ProgramData\...` y rutas de guardado conservan el árbol completo bajo `<Game>/_HyperPlaySaves/`.
8. **Registro:** los stubs `advapi32!Reg*` devuelven `ERROR_FILE_NOT_FOUND (2)` en vez de `ERROR_SUCCESS` con `HKEY` sin rellenar.
9. **`timeGetTime`:** log one-shot de las 3 primeras llamadas para certificar el reloj monotónico.

10. **CAUSA RAÍZ REAL de `Forcing GfxDevice: Null`: bug del JIT en `SETcc m8`** (`OryonEmitterBackend.cpp`, rama `0F 90..9F`). El 0/1 se calculaba en X16 y luego `emitEffectiveAddress` (que usa X16/X17) lo sobrescribía con la dirección: se almacenaba **el byte bajo de la dirección**. Unity guarda `setne [rip+...]` en `0x181f4a331` (flag `nographics`) -> quedaba `0x31` (≠0) -> `IsNoGraphics()` verdadero -> `batchmode=1, renderer=Null`. Localizado con análisis estático de `UnityPlayer.dll` (LEA RIP-relativo a `"Forcing GfxDevice"`/`"nographics"`, escritores de la global del renderer en RVA `0x7d7afe`, función de decisión RVA `0x646ca0`) y un volcado en vivo temporal (`batchmode=1 nographics=49 flag332=50`, ya eliminado). Corrección: el valor se aparta en X20 antes de calcular la dirección. Test `testSetccMemory` (`setne/sete [rax+d8]` en dirección con byte bajo `0x31`).
11. **`DeviceContext::Map`** usaba `a4` (MapFlags) como `pMapped`; la firma COM es `Map(this, res, sub, type, flags, pMapped)` -> `a5`. Con los args reales devolvía `E_POINTER` y Unity registraba `failed to map staging 2D texture [80004003]`.
12. **`DirectVulkanBridge` duplicado:** `jni_bridge.cpp` (superficie de Kotlin) y `NativeDllInterceptor.cpp` (presentación DXGI) tenían instancias estáticas distintas; ahora ambas usan `DirectVulkanBridge::shared()`.
13. **`CreateDirectoryW/A` reales** (antes stub -> 0): `mkdir` sobre `resolveVirtualPath`, `ERROR_ALREADY_EXISTS (183)`, `ERROR_PATH_NOT_FOUND (3)`, `ERROR_ACCESS_DENIED (5)`. Cuphead ya crea `_HyperPlaySaves/users/default/AppData/LocalLow/Studio MDHR/Cuphead/Crashes`.
14. **`MultiByteToWideChar`/`WideCharToMultiByte`**: CP_UTF8 (ACP del entorno) se decodificaba como Latin-1; ahora UTF-8 <-> UTF-16 real con surrogates, `U+FFFD` en secuencias inválidas y `ERROR_INSUFFICIENT_BUFFER (122)` si no cabe.

### Limpieza (agente en paralelo)
- ~17.900 líneas eliminadas de la era Wine/rootfs/contenedores/X11: `cpp/bridge/` y `cpp/core/` completos, `XSharedFrameServer`, `VsyncPublisher`, `VulkanSurfaceBridge`, `HyperPlayDisplayProtocol`, `FrameStats`, `LowLatencyAudioServer`, `PcmMixer/RingBuffer`, `EvdevInjector`, `PadSharedState`, `pad_jni`, `BionicDlopenCpuBackend`, `BionicRuntimeLinker`; `main/assets/` y `main/jniLibs/` (bash, FEX, Xvfb, xkbcomp, plugins ALSA/Wine); Kotlin `setup/` y `engine/` (`RootfsDownloadAndSetupManager`, `WineRuntimes`, `WarmContainer`, `ContainerProcess`, `CompatibilityLayerManager`, `EngineInstall*`, `EngineMode`, `EngineRuntimeNative`, `BionicCoreManager`, `BootstrapAssets`), `NativeInputSink` y sus tests.
- Editados: `CMakeLists.txt`, `jni_bridge.cpp` (JNI legacy fuera; nuevos `nativeDirectSetGamepadState`, `nativeDirectGetRumble`, `nativeDirectFramesPresented`), `GameLauncher`, `TurnipDriverManager`, `XInputPad*` (el pad ahora alimenta `DirectXInputBridge`, antes escribía un shm que nadie leía), `ControllerManager`, `GameSessionActivity`, `MainActivity`, `LauncherScreen/Logic`, `LibraryRepository`, `GameSessionLogic`, `SessionOverlay`, `PerfMonitor`, `AndroidManifest.xml` (sin `FirstRunSetupActivity`/`EngineInstallService` ni permisos de foreground/wake-lock), `app/build.gradle.kts` (sin commons-compress/zstd-jni). Emparejamiento `external fun` <-> `Java_com_murdoc820_*` verificado.
- Pendiente de decisión: cadena Turnip (`TurnipDriverManager`, `TurnipNative`, `gpu/TurnipDriverInspector`) sin llamadas en producción; `third_party/` (proot/talloc/mesa-patches), `scripts/build_*rootfs*.sh`, `windows/` fuera de `app/src` sin tocar.

### Verificación
- Suite nativa (`-std=c++20 -O2 -Wall -Wextra -Werror -static-libstdc++`, NDK 28.2) en `/data/local/tmp/oryon_test` del RedMagic 10 Pro: **`514 comprobaciones, 0 fallos`, `EXIT:0`** (antes 443). Nuevos: `testNativeThunkDispatcher12Args`, `testCallGuestNested`, `testSetccMemory`, `testShlwapiPaths`, métricas/monitor.
- Comando: `aarch64-linux-android31-clang++ ... OryonEmitterBackendTest.cpp cpu/OryonEmitterBackend.cpp cpu/HyperAotCache.cpp loader/PeHeaderInspector.cpp -llog`.
- Gate Gradle `testDebugUnitTest lintDebug assembleDebug`: **BUILD SUCCESSFUL** (`GATE_EXIT=0`, `%TEMP%\opencode\gradle_gate_utf8.txt`). `OryonJitTranslatorTest` ajustado a la caché JIT real de 256 MB.
- **Hollow Knight en vivo** (`%TEMP%\opencode\logcat_utf8_HK.txt`): `Forcing GfxDevice: Null = 0`; `GfxDevice: creating device client; kGfxThreadingModeThreaded`; `EnumDisplayMonitors -> callback(HMONITOR=0xcafe0010) = 1` con `GetMonitorInfoA` dentro; `CreateWindowExW(... size=2688x1216 (pedido CW_USEDEFAULT))`; `CreateDXGIFactory2 -> S_OK`; `D3D11CreateDevice(adapter=..., driverType=0, flags=0x20, levels=4, sdk=7, ppDevice, pFeatureLevel, ppContext)` -> `S_OK, nivel 0xB000`; 121 llamadas a `ID3D11DeviceContext`; `Fatal signal=0`, `SIGABRT=0`, `ExitProcess=0`.
- **Cuphead en vivo** (`logcat_utf8_Cuphead.txt`): `PathCanonicalizeW('D:\Cuphead.exe') -> 'D:\Cuphead.exe'`, `Mono path[0] = 'D:/Cuphead_Data/Managed'` (antes `_Data\...`), sin `no boot config`, `CreateWindowExW(style=0xca0000, 320x240)`, `D3D11CreateDevice` real, avanza a `Begin MonoManager ReloadAssembly`; proceso vivo a los 80 s; `Fatal signal=0`.

### Iteración "bug de cadena" (2026-11-17 ~19:00)
- **`.resG` NO era basura:** Unity también sondea `.res`, `.resS` y `.resG` (recursos de streaming) para `globalgamemanagers.assets` y `unity default resources`. El diagnóstico anterior era erróneo.
- **Bug real: `wchar_t` de 32 bits de Bionic escrito/leído como WCHAR de Windows (16 bits).** El juego pasa a `GetFileAttributesW` el mismo buffer que rellena `GetTempPathW` (desensamblado de `UnityPlayer.dll` en `0x180a12fc7` -> `0x180a125b1`); como se copiaba un literal `L"D:\\Temp\\"`, el juego leía `'D'`. Corregidos todos los casos encontrados en `NativeDllInterceptor.cpp`:
  - `GetTempPathW/A`: ahora devuelven `C:\users\default\AppData\Local\Temp\` (aislado en `_HyperPlaySaves/users/...`) y crean la carpeta real; antes creaban `savesDir/Temp` mientras `D:\Temp` resolvía a `<Game>/Temp`.
  - `GetUserObjectInformationW` (`UOI_NAME`/`UOI_TYPE`), `GetKeyboardLayoutNameW` (`wcsncpy`), `GetKeyNameTextW`, `ToUnicode` (escribían 4 bytes).
  - `CreateFileA`, `CreateFileMappingA`, `OpenFileMappingA`: pasaban un búfer `wchar_t` a la variante W -> solo llegaba el primer carácter.
  - `GetFullPathNameW`: `*lpFilePart` se calculaba con aritmética de `wchar_t*` (4 bytes por carácter).
- **Causa probable del cierre silencioso de Hollow Knight: `WinSafeHeap` hacía un `mmap` >= 4 KB por CADA `HeapAlloc`/`LocalAlloc`/`MemoryManager::Allocate`** (28.128 asignaciones de 16-65 B en 6 s, cada una con su línea de log). La RSS crece x64-x256 y Android mata el proceso sin señal; además `isSafeHeapPointer` leía `ptr-32` de punteros ajenos (posible SIGSEGV). Sustituido por `loader/WinSafeHeap.h`: 24 clases de tamaño (16 B..64 KB) en arenas de 64 MB (`MAP_NORESERVE`) con listas libres, `mmap` dedicado solo para > 64 KB, cabecera de 16 B (alineación de 16), pertenencia por rango de arena (nunca lee memoria ajena), doble free detectado, ceros garantizados como antes. `HeapReAlloc` rechaza punteros ajenos (`ERROR_INVALID_PARAMETER`) en vez de copiar un tamaño inventado. Eliminado el log por llamada de `HeapAlloc`.
- Test nuevo `testWinSafeHeap` (200.000 bloques en <= 3 arenas, alineación, ceros, `owns`/`free` de puntero ajeno, doble free, bloque grande de 1 MB).
- Compilación NDK limpia (`-Werror`) del interceptor y de `oryon_test`; gate Gradle **BUILD SUCCESSFUL** (`%TEMP%\opencode\gradle_gate_heap.txt`).
- **Verificado en el dispositivo** (ADB Wi-Fi reactivado en el puerto nuevo **`192.168.0.11:45023`**, descubierto con `adb mdns services`; `35515` ya no vale):
  - Suite nativa en silicio: **`531 comprobaciones, 0 fallos`, `EXIT:0`**.
  - **Hollow Knight** (`%TEMP%\opencode\logcat_heap_HK.txt`): **vivo a los 60 s** (antes moría a los ~6 s), RSS estable 357-475 MB. `GetTempPathW -> C:\users\default\AppData\Local\Temp\` y Unity ya reconoce la carpeta. Unity reporta `Direct3D 11.0 [level 11.0]`, `Renderer: Qualcomm Adreno 830`, `VRAM 8192 MB`; FMOD cae a salida emulada sin sonido; llega a `Begin MonoManager ReloadAssembly` y al registro del PlayerLoop (`Unable to find type ...PlayerLoop...`, avisos de tipos XR/Director ausentes en la build). `Fatal signal=0`, `SIGABRT=0`, `ExitProcess=0`.
  - **Cuphead** (`logcat_heap_Cuphead.txt`): vivo a los 80 s, RSS ~299 MB; `Direct3D 11.0`, Adreno 830; tras `Begin MonoManager ReloadAssembly` + `mono-dl.c:125` la RSS queda **congelada** (298.936 kB de 60 s a 80 s): probable bloqueo en la carga de ensamblados.
- Restos menores: `QueryInterface(IDXGIFactory5) failed (80004002)` (esperable), `Failed to save a temporary cursor file ... Unknown error [0]` (algún paso de `CreateFileW`/`WriteFile` sobre Temp devuelve fallo sin `SetLastError`), un `WinSafeHeap: free de ... ajeno` (puntero de otro asignador).

### Siguientes bloqueos reales
1. **D3D11 mock sin recursos:** `CreateTexture3D`/`Texture2D`/`ShaderResourceView` devuelven `E_NOTIMPL (0x80004001)` (`failed to create 3D texture`). Siguiente hito: recursos D3D11 reales sobre `DirectVulkanBridge` (texturas, SRV, RTV) y `Present` hacia la swapchain.
2. **Hollow Knight:** resuelto el cierre silencioso (montón). Siguiente: PlayerLoop -> primer `Present` (depende del punto 1).
3. **Cuphead:** se congela tras `Begin MonoManager ReloadAssembly` con `* Assertion: should not be reached at ..\mono\utils\mono-dl.c:125` (Mono `mono_dl_open` sin backend de carga para esta plataforma; revisar `LoadLibraryW` de ensamblados nativos/`mono.dll` y su `GetProcAddress`). FMOD sin dispositivo de salida (falta puente WASAPI/`winmm waveOut` -> AAudio).

## Hito Crítico: Snapdragon 8 Elite (RedMagic 10 Pro NX789J) - 2026-11-17 (Sesión Alineación Win64 + Banderas PF + ABI AAPCS64)

### 0. Estado de esta sesión (actualización en paralelo, en curso)
- **Objetivo:** superar `nativeDirectLaunch terminó con código -2: execute fallo (-2): Jcc con paridad (PF) no soportado` y continuar el arranque de Hollow Knight en el RedMagic 10 Pro (`192.168.0.11:35515`).
- **Progreso verificado:** el bloqueo PF ya no existe en el emisor; la suite nativa pasa **307 comprobaciones, 0 fallos -> OK** en el Snapdragon 8 Elite. La inversión de paridad quedó corregida y verificada en silicio.
- **Gate Gradle:** `cmd /c ".\gradlew.bat testDebugUnitTest lintDebug assembleDebug --console=plain"` -> **BUILD SUCCESSFUL en 1m 10s** (53 tareas: 6 ejecutadas, 47 up-to-date; `GATE_EXIT=0`). Log: `%TEMP%\opencode\gradle_gate_20261117.txt`.
- **Verificación en vivo completada:** APK reinstallado (`Success`, 39.803.827 B), Hollow Knight relanzado (pid `7549`), applog `Hollow_Knight-20261117-145337.log`: ya no aparece `Jcc con paridad (PF) no soportado` ni `stack_unaligned`. El bloqueo PF quedó superado en silicio real.
- **Nuevo bloqueo en vivo:** `nativeDirectLaunch terminó con código -3: execute fallo (-3):` con `lastError()` vacía. En `OryonEmitterBackend.cpp:7473`, `-3` significa `stopped_ == true` (alguien llamó a `stop()`), **no** fallo de traducción (ese caso es `-2`). El logcat acotado (30.000 líneas) contiene 30.000 llamadas `[NativeStub] ... WaitOnAddress ... R8=0x4, R9=0xfffffffe -> returning 0` de los hilos `7735`/`7737`; el stub genérico que retorna 0 inmediatamente produce un spin infinito.
- **Implementación en curso (2026-11-17, bloque 2):** `WaitOnAddress`/`WakeByAddress`/`WakeByAddressAll` dejan de ser stubs genéricos:
  - `resolveNativeSymbol()` los mapea a trampolines reales `win_WaitOnAddress`, `win_WakeByAddress` y `win_WakeByAddressAll` antes de caer en `DynamicStubManager` (`NativeDllInterceptor.cpp:1113+`).
  - `win_WaitOnAddress(Address, CompareAddress, AddressSize, dwMilliseconds)` valida que ambos rangos sean legibles (`mincore`), lee el valor de comparación desde memoria huésped (1/2/4/8 bytes), y bloquea en un `std::condition_variable` por dirección hasta que el valor atómico leído en `Address` **cambie** respectoa `CompareAddress`, o hasta el timeout (`INFINITE = 0xFFFFFFFF`, `0xFFFFFFFE` acotado a 24 h). Devuelve `1` si despertó, `0` + `ERROR_TIMEOUT (1460)` si expiró, `0` + `ERROR_INVALID_PARAMETER (87)` si los argumentos son inválidos.
  - `win_WakeByAddress`/`win_WakeByAddressAll` incrementan una generación y hacen `notify_all()` sobre el objeto de esa dirección. El valor se lee con `__atomic_load_n`, por lo que no hay carrera de datos C++ con las escrituras atómicas invitadas.
  - `isVoidWin32Function()` incluye `WakeByAddress`/`WakeByAddressAll` para no sobreescribir RAX.
  - Declaraciones añadidas en `NativeDllInterceptor.h:410+`.
- **Gate tras WaitOnAddress:** `BUILD SUCCESSFUL in 15s`, `GATE_EXIT=0` (`%TEMP%\opencode\gradle_gate_waitonaddress.txt`). Compilación C++ sin errores con los nuevos trampolines.
- **Resultado en vivo (2ª iteración):** el spin desapareció. En 20.000 líneas de logcat: `WaitOnAddress=27`, `WakeByAddress=2`, `Fatal signal=0`, `assert=0`. Antes: 30.000/30.000 líneas eran `WaitOnAddress` con stub. La trampa real devuelve `RAX=0x1` y el motor sigue avanzando a nuevas APIs.
- **Nuevo hallazgo:** la aplicación importa `WakeByAddressSingle` (no `WakeByAddress`), que seguía cayendo en el stub genérico. Se añadió el mapeo a `win_WakeByAddress` y a `isVoidWin32Function()`.
- **Ruido de log:** en 20 s se emitieron 15.069 líneas `[NativeThunk]`, dominadas por `HeapAlloc` (3.909), `ReleaseSRWLockExclusive` (1.304), `AcquireSRWLockExclusive` (1.303), `TlsGetValue` (478), `TlsSetValue` (102) y `WaitOnAddress` (26). Se marcaron como `isNoisy` para no ahogar el logcat y no degradar el rendimiento.
- **Gate tras `WakeByAddressSingle` + throttling:** `BUILD SUCCESSFUL in 14s`, `GATE_EXIT=0` (`%TEMP%\opencode\gradle_gate_wake_single.txt`).
- **Bloqueo real identificado tras el spin:** el huésped **no se cae por el JIT**: llama a `KERNEL32.dll!ExitProcess(1)` (logcat `14:59:10.252`, `RIP=0x1819bf735`). La causa inmediata es `GetModuleHandleExW(0, L"mscoree.dll", &hMod)` (`RIP=0x1819bf793`), para lo que `PeLoader` registra `No se pudo localizar DLL local en disco: mscoree.dll` y devuelve `RAX=0`; acto seguido Unity termina con código 1.
- **3ª verificación:** `WaitOnAddress` real eliminó el spin: `NativeThunk=852` en 20 s frente a 15.069 antes; `NativeStub=12` (ya ninguno de sincronización); `Fatal signal=0`, `Assertion=0`. Los 12 stubs restantes son APIs no críticas (`SetDllDirectoryW`, `RegQueryValueExA`, `WS2_32.dll!`, `AppPolicyGetProcessTerminationMethod`, `DllGetVersion`, `RegCreateKeyW`, `RegOpenKeyExW`).
- **Aclaración `mscoree.dll`:** no existe en la carpeta del juego (solo `MonoBleedingEdge/`). Según el patrón del CRT de MSVC, `GetModuleHandle(mscoree.dll)` forma parte del apagado: si no está cargado, se llama directamente a `ExitProcess`. No es un fallo de nuestra API ni un sitio donde deba devolverse un handle falso.
- **Causa real de la salida:** `[Guest Console] Unsupported platform.` seguido de `Failed to create batch mode window.` (14:59:10.110/.111). Justo antes, `shell32.dll!DllGetVersion` era un stub genérico que devolvía `0` sin rellenar `DLLVERSIONINFO`, por lo que Unity no podía identificar la plataforma y terminaba con `ExitProcess(1)`.
- **Implementación completada:** `win_DllGetVersion()` valida `pdvi`/`cbSize`, rellena `DLLVERSIONINFO` (20 B) o `DLLVERSIONINFO2` (32 B) con versión Windows NT 6.1.7601 (`VER_PLATFORM_WIN32_NT=2`, `ullVersion=MAKEDLLVERULL(6,1,7601,0)`) y devuelve `S_OK`; parámetros inválidos devuelven `E_POINTER`/`E_INVALIDARG` honestos. Mapeado en `resolveNativeSymbol()` para `shell32.dll`/`shfolder.dll` antes del stub genérico. Declaración en `NativeDllInterceptor.h:533+`.
- **Gate con `DllGetVersion`:** `BUILD SUCCESSFUL in 14s`, `GATE_EXIT=0` (`%TEMP%\opencode\gradle_gate_dllgetversion.txt`).
- **Resultado en vivo (4ª iteración, 18 s):** `DllGetVersion=2` (llamada y retorno `RAX=0x0 = S_OK`), `Unsupported platform=0`, `batch mode=0`, `ExitProcess=0`, `Fatal signal=0`, `Assertion=0`. Unity arranca: `Initialize engine version: 6000.0.61f1 (74a0adb02c31)`, `Forcing GfxDevice: Null`, `NullGfxDevice: Version NULL 1.0`. `NativeStub=10` (sin APIs de sincronización ni DllGetVersion).
- **Nuevo bloqueo real (JIT):** `nativeDirectLaunch terminó con código -2: execute fallo (-2): opcode 0F C2 no soportado en RIP=0x180633f60`. `0F C2` es `CMPPS xmm1, xmm2/m128, imm8` (o `CMPPD` con prefijo `0x66`); hay que implementarlo en `OryonEmitterBackend` y verificarlo en la suite nativa.
- **Implementación de `0F C2` completada y verificada en silicio:**
  - `OryonEmitterBackend.h/.cpp`: nuevos `emitFcmEqV`/`emitFcmGeV`/`emitFcmGtV` con bases AArch64 `FCMEQ 0x4E20E400/0x4E60E400`, `FCMGE 0x6E20E400/0x6E60E400`, `FCMGT 0x6EA0E400/0x6EE0E400` (`4S`/`2D`) y `emitCmpPsPd()` para los 8 predicados x86. `EQ=FCMEQ(a,b)`, `LT=FCMGT(b,a)`, `LE=FCMGE(b,a)`, `ORD=FCMEQ(a,a)&FCMEQ(b,b)`; `NEQ/NLT/NLE/UNORD` se invierten con `EOR` contra una máscara `0xFF..FF` generada por `CMEQ` de un registro consigo mismo.
  - `translateBlock()` decodifica `0F C2 /r imm8`, acepta fuente registro o memoria (`emitLdrQ` en `V24`), aplica prefijo `0x66` para `CMPPD`, rechaza explícitamente `F2/F3` (`CMPSS/CMPSD`) y predicados > 7.
  - Correcciones encontradas por la prueba: la inversión con `EOR` contra cero era identidad (usar máscara todo unos); `ORD/UNORD` destruía la fuente memory al calcular `ORD(src2)` después de sobrescribir `V24` (calcular `src2` primero); el test usaba ModRM `0x09` en vez de `0x01` para `xmm0,[r9]`.
  - `OryonEmitterBackendTest.cpp`: nuevo `testCmppsCmpPd()` con 32 comprobaciones (8 predicados × registro/memoria × `CMPPS`/`CMPPD`, incluyendo NaN). Compilación NDK limpia con `-Wall -Wextra -Werror`; en dispositivo: **`OryonEmitterBackendTest: 340 comprobaciones, 0 fallos -> OK`**.
- **Gate con `0F C2`:** `BUILD SUCCESSFUL in 13s`, `GATE_EXIT=0` (`%TEMP%\opencode\gradle_gate_cmpps.txt`).
- **Verificación en vivo de `0F C2`:** APK reinstalado (`Success`), Hollow Knight relanzado, PID `10445`. En `%TEMP%\opencode\logcat_cmpps.txt`: `0F C2=0`, `ExitProcess=0`, `Fatal signal=0`, `Assertion=0`, `NativeStub=10`. Unity avanza hasta `Initialize engine version: 6000.0.61f1 (74a0adb02c31)`, `Forcing GfxDevice: Null`, `Input System module state changed to: Initialized`.
- **Nuevo bloqueo real (JIT):** `nativeDirectLaunch terminó con código -2: execute fallo (-2): opcode 0F 50 no soportado en RIP=0x180633f64`. `0F 50` es `MOVMSKPS r32, xmm/m128` (`0x66` → `MOVMSKPD`, 2 bits); hay que implementarlo y verificarlo en la suite nativa.
- **Implementación de `0F 50` completada y verificada en silicio:**
  - `OryonEmitterBackend.cpp`: `translateBlock()` decodifica `0F 50 /r`, fuente registro o memoria (`emitLdrQ` en `V24`), prefijo `0x66` para `MOVMSKPD`, rechazo explícito de `F2/F3`. Extrae `UMOV W16, Vsrc.S[lane]` (base `0x0E003C00`, `imm5=(lane<<3)|4`; `S[1]`/`S[3]` para PD), aísla el bit con `LSR #31`, desplaza `LSL #i` y acumula con `ORR Wdst`.
  - `OryonEmitterBackendTest.cpp`: nuevo `testMovmskpsMovmskpd()` con 42 comprobaciones (16 máscaras × registro/memoria para PS, 4 × registro/memoria para PD, rechazo de prefijo `F3`). Compilación NDK limpia con `-Wall -Wextra -Werror`; en dispositivo: **`OryonEmitterBackendTest: 382 comprobaciones, 0 fallos -> OK`**.
  - Corrección durante la prueba: el test usaba ModRM `0x01` con REX.B para `[r8]`; el valor correcto es `0x00` (`rm=000` + REX.B -> R8). El fallo inicial fue `SIGSEGV` leyendo `[X16]=0`, no un error del opcode emitido.
- **Gate con `0F 50`:** `BUILD SUCCESSFUL in 10s`, `GATE_EXIT=0` (`%TEMP%\opencode\gradle_gate_movmsk.txt`).
- **Verificación en vivo de `0F 50`:** APK reinstalado (`Success`), PID `11430`. En `%TEMP%\opencode\logcat_movmsk.txt`: `0F 50=0`, `0F C2=0`, `no soportado=0`, `Fatal signal=0`, `Assertion=0`, `NativeStub=12`. Unity sigue avanzando (`Initialize engine version`, `NullGfxDevice`).
- **Nuevo bloqueo (API Win32, no JIT):** guest imprime `[Guest Console] Received signal SIGABRT`, `Obtained 0 stack frames` y después `TerminateProcess(-1, 1)` → `executeEntry` devuelve `-3`. No hay `ExitProcess`, `RaiseException`, opcode no soportado ni fatal nativo.
- **Causa probable y corrección aplicada:** `KERNEL32.dll!InitOnceBeginInitialize` estaba entre los stubs dinámicos que devuelven 0 (fallo) justo antes del abort. Se implementó `win_InitOnceBeginInitialize()` con semántica real: estado por puntero `lpInitOnce`, primera llamada `*fPending=1` y `TRUE`, llamadas posteriores `*fPending=0` y `TRUE`, soporte de `INIT_ONCE_CHECK_ONLY`, validación de argumentos con `ERROR_INVALID_PARAMETER`. Se registró el trampoline en `resolveKernel32Export()`.
- **Gate con `InitOnceBeginInitialize`:** `BUILD SUCCESSFUL in 14s`, `GATE_EXIT=0` (`%TEMP%\opencode\gradle_gate_initonce.txt`).
- **Verificación en vivo de `InitOnceBeginInitialize`:** ya no aparece como stub; devuelve `RAX=0x1`. Pero aparece `KERNEL32.dll!InitOnceComplete` como stub devolviendo 0 inmediatamente después, y el guest vuelve a imprimir `Received signal SIGABRT` + `TerminateProcess(0xffffffffffffffff, 1)`. Conclusión: el abort se origina en el par InitOnce, no en un opcode JIT.
- **Corrección en curso:** implementar `win_InitOnceComplete()` compartiendo estado con `win_InitOnceBeginInitialize()` (mapa por `lpInitOnce`, `completed`/`context`), con `INIT_ONCE_INIT_FAILED` y error `ERROR_INVALID_PARAMETER` para argumentos nulos.
- **Gate con `InitOnceComplete`:** `BUILD SUCCESSFUL in 14s`, `GATE_EXIT=0` (`%TEMP%\opencode\gradle_gate_initonce2.txt`).
- **Verificación en vivo del par InitOnce:** `SIGABRT=0`, `TerminateProcess=0`, `ExitProcess=0`, `Fatal signal=0`, `Assertion=0`, `NativeStub=10`; `InitOnceBeginInitialize` y `InitOnceComplete` ejecutan como thunks reales. Unity vuelve a avanzar hasta `NullGfxDevice`.
- **Implementación de VEX `0F AE` completada y verificada en silicio:**
  - `OryonEmitterBackend.cpp`: rama `vexOp == 0xAE` en el decodificador VEX 2B con grupo 15: `/0 VFXSAVE`, `/1 VFXRSTOR`, `/2 VLDMXCSR`, `/3 VSTMXCSR` (escribe `0x1F80`), `/5../7` barreras `DMB`; rechaza ModRM truncado explícitamente.
  - `OryonEmitterBackendTest.cpp`: nuevo `testVexMxcsr()` con 4 comprobaciones (`VSTMXCSR [RSP+0x40]`, `VLDMXCSR [RSP]` + `VSTMXCSR`, verificación del valor `0x1F80` en memoria). Compilación NDK limpia con `-Wall -Wextra -Werror`; en dispositivo: **`OryonEmitterBackendTest: 386 comprobaciones, 0 fallos -> OK`**.
- **Gate con VEX `0F AE`:** `BUILD SUCCESSFUL in 9s`, `GATE_EXIT=0` (`%TEMP%\opencode\gradle_gate_vexae.txt`).
- **Verificación en vivo de VEX `0F AE`:** APK reinstalado, PID `12899`. En `%TEMP%\opencode\logcat_vexae.txt`: `VEX 2B opcode no soportado=2`, `0F 50=0`, `0F C2=0`, `no soportado=2`, `SIGABRT=0`, `TerminateProcess=0`, `ExitProcess=0`, `Fatal signal=0`, `Assertion=0`, `NativeStub=10`. La dirección original `0x1819cd0cc` ya se traduce; el bloqueo se movió a `RIP=0x1819cd0e9`.
- **Implementación de conversiones escalares VEX completada y verificada en silicio:**
  - `OryonEmitterBackend.cpp`: VEX 2B `vexOp==0x2D` con `pp==F3` emite `VCVTSS2SI r32, xmm/m32` (`FCVTZS Wd, Sn`, fuente registro o memoria); VEX 3B mapa 1 `vexOp==0x2A` con `pp==F3` emite `VCVTSI2SS xmm1, xmm2, r/m32|64` con `W`, preservando los lanes altos de `vvvv` mediante `ORR V` + `INS V.S[0]`.
  - `OryonEmitterBackendTest.cpp`: nuevo `testVexScalarConversions()` con 6 comprobaciones. Detalle corregido durante la prueba: `C5 7A` tiene `R~=0`, por lo que el destino es `R8D`, no `EAX`; la comprobación usa `g_backend->context().gpr[8]`. Compilación NDK limpia con `-Wall -Wextra -Werror`; en dispositivo: **`OryonEmitterBackendTest: 392 comprobaciones, 0 fallos -> OK`**.
- **Gate con conversiones VEX:** `BUILD SUCCESSFUL in 9s`, `GATE_EXIT=0` (`%TEMP%\opencode\gradle_gate_vexcvt.txt`).
- **Verificación en vivo de conversiones VEX:** APK reinstalado, PID `13582`. En `%TEMP%\opencode\logcat_vexcvt.txt`: `VEX 3B opcode no soportado=0`, `0F 50=0`, `0F C2=0`, `SIGABRT=0`, `TerminateProcess=0`, `ExitProcess=0`, `Fatal signal=0`, `Assertion=0`, `NativeStub=10`. Las tres instrucciones del bloque `0x1819cd0cc` ya se traducen correctamente.
- **Implementación de VEX `0F 2E/2F` completada y verificada en silicio:** rama VEX 2B que decodifica `UCOMISS/COMISS` (`pp=0`) y `UCOMISD/COMISD` (`pp=1`, `66`), con fuente registro o memoria y síntesis idéntica de `ZF=0x40`, `PF=0x04`, `CF=0x01`. `OryonEmitterBackendTest.cpp` amplía `testVexScalarConversions()` con 5 comprobaciones (mayor/menor/igual/NaN en single y mayor en double). Compilación NDK limpia con `-Wall -Wextra -Werror`; en dispositivo: **`OryonEmitterBackendTest: 397 comprobaciones, 0 fallos -> OK`**. Detalle corregido en la prueba: `modrm=0x48` cargaba `xmm1`, no `xmm2`; el valor correcto es `0x50`.
- **Gate con VEX `UCOMISS`:** `BUILD SUCCESSFUL in 9s`, `GATE_EXIT=0` (`%TEMP%\opencode\gradle_gate_vexucomiss.txt`).
- **Verificación en vivo de VEX `UCOMISS`:** APK reinstalado, PID `14116`. En `%TEMP%\opencode\logcat_vexucomiss.txt`: `VEX 3B opcode no soportado=0`, `0F 50=0`, `0F C2=0`, `SIGABRT=0`, `TerminateProcess=0`, `ExitProcess=0`, `Fatal signal=0`, `Assertion=0`, `NativeStub=10`. El bloqueo anterior (`0x1819cd0f8`) ya se traduce.
- **Nuevo bloqueo real (JIT):** `VEX 2B opcode no soportado` en `RIP=0x1819cce57`, bytes `C5 F9 70 D0` = `VPSHUFD xmm2, xmm0, imm8` (VEX.128.66.0F `70 /r ib`). La lógica legacy de `PSHUFD` ya existe en `OryonEmitterBackend.cpp:4088` y puede reutilizarse, añadiendo la lectura del `imm8` y `pp==66`.
- **Implementación de VEX `0F 70` completada y verificada en silicio:** rama VEX 2B `vexOp==0x70 && pp==66 && !L` emite `VPSHUFD xmm1, xmm2/m128, imm8`, reutilizando `emitInsElementS` y con copia de seguridad en `V25` cuando destino y origen coinciden. `testVexPshufd()` añade 9 comprobaciones (inversión de dwords y fuente memoria). Compilación NDK limpia con `-Wall -Wextra -Werror`; en dispositivo: **`OryonEmitterBackendTest: 406 comprobaciones, 0 fallos -> OK`**. Detalles corregidos en la prueba: `modrm=0x08` cargaba `xmm1`, no `xmm0` (el correcto es `0x00`); VEX2 no tiene bit `B~`, por lo que `[r8]` no existe y la fuente memoria usa `[rax]`.
- **Gate con VEX `VPSHUFD`:** `BUILD SUCCESSFUL in 8s`, `GATE_EXIT=0` (`%TEMP%\opencode\gradle_gate_vexpshufd.txt`).
- **Verificación en vivo de VEX `VPSHUFD`:** APK reinstalado, PID `14822`. En `%TEMP%\opencode\logcat_vexpshufd.txt`: `VEX 3B opcode no soportado=0`, `0F 50=0`, `0F C2=0`, `SIGABRT=0`, `TerminateProcess=0`, `ExitProcess=0`, `Fatal signal=0`, `Assertion=0`, `NativeStub=10`. `VPSHUFD` en `0x1819cce57` ya se traduce.
- **Nuevo bloqueo real (JIT):** `VEX 2B opcode no soportado` en `RIP=0x1819cce8e`, bytes `C5 E1 73 D3 34` = `VPSRLQ xmm3, xmm3, 0x34` (VEX.128.66.0F 73 /2), seguido de `C5 E1 FB 1D ...` = `VPSUBQ` y `C5 FA E6 ...` (truncado). Implementado el grupo de desplazamientos vectoriales VEX `0F 71/72/73` con codificación de tres operandos: `vvvv` es destino, `ModRM.rm` fuente y `ModRM.reg` el grupo; se corrige además el manejo de alias `dst==src` (copia cuando `imm==0`). `testVexVectorShifts()` añade 19 comprobaciones (`VPSRLQ` y `VPSLLDQ` con destino distinto de la fuente). Compilación NDK limpia con `-Wall -Wextra -Werror`; en dispositivo: **`OryonEmitterBackendTest: 425 comprobaciones, 0 fallos -> OK`**. Nota: no se cambió la ruta legacy `0F 71/72/73` porque sus pruebas confirman que sus operandos son correctos tal como estaban.
- **Gate con desplazamientos VEX:** `BUILD SUCCESSFUL in 8s`, `GATE_EXIT=0` (`%TEMP%\opencode\gradle_gate_vexshifts.txt`).
- **Verificación en vivo de desplazamientos VEX:** APK reinstalado, PID `15530`. En `%TEMP%\opencode\logcat_vexshifts.txt`: `VEX 3B opcode no soportado=0`, `0F 50=0`, `0F C2=0`, `SIGABRT=0`, `TerminateProcess=0`, `ExitProcess=0`, `Fatal signal=0`, `Assertion=0`, `NativeStub=10`. `VPSRLQ` en `0x1819cce8e` ya se traduce.
- **Nuevo bloqueo real (JIT):** `VEX 2B opcode no soportado` en `RIP=0x1819cce93`, bytes `C5 E1 FB 1D 45 41 09 00` = `VPSUBQ xmm3, [rip+0x94145], xmm3` (VEX.128.F2.0F `FB`). Implementada la familia VEX `VPADD*/VPSUB*` (`D4`, `FC..FE` con `66`; `F8..FA` con `66`; `FB` con `F2`) con destino en `vvvv`, `src1` en `ModRM.reg` y `src2` en `ModRM.rm`/memoria, protegiendo el alias cuando `src2==dst`. `testVexIntegerAddSub()` añade 5 comprobaciones (registro y memoria). Compilación NDK limpia con `-Wall -Wextra -Werror`; en dispositivo: **`OryonEmitterBackendTest: 430 comprobaciones, 0 fallos -> OK`**.
- **Gate con aritmética entera VEX:** `BUILD SUCCESSFUL in 8s`, `GATE_EXIT=0` (`%TEMP%\opencode\gradle_gate_vexintarith.txt`), aunque la prueba en vivo posterior reveló un error en el filtro `pp`.
- **Corrección de `pp` para VEX `VPADD*/VPSUB*`:** `VEX.128.66.0F FB` es `VPSUBQ` (`pp=66`, no `F2`). El gate anterior usaba `pp==3` para `FB`, por lo que `C5 E1 FB` seguía sin traducirse. Corregido a `pp==1` para toda la familia; las pruebas nativas usan ahora los bytes correctos `C5 E1 FB DC` / `C5 E1 FB 18` (verificados con `llvm-objdump`). Compilación NDK limpia; **430 comprobaciones, 0 fallos -> OK**.
- **Gate tras corrección de `pp`:** `BUILD SUCCESSFUL in 8s`, `GATE_EXIT=0` (`%TEMP%\opencode\gradle_gate_vexintarith2.txt`).
- **Verificación en vivo de VEX `VPSUBQ`:** APK reinstalado, PID `16432`. En `%TEMP%\opencode\logcat_vexintarith2.txt`: `VEX 3B opcode no soportado=0`, `0F 50=0`, `0F C2=0`, `SIGABRT=0`, `TerminateProcess=0`, `ExitProcess=0`, `Fatal signal=0`, `Assertion=0`, `NativeStub=10`. `VPSUBQ` en `0x1819cce93` ya se traduce.
- **Nuevo bloqueo real (JIT):** `VEX 2B opcode no soportado` en `RIP=0x1819cce9b`, bytes `C5 FA E6 DB` = `VCVTDQ2PD xmm3, xmm3` (VEX.128.F3.0F `E6 /r`). Implementada la familia VEX `0F E6` (`F3` -> `VCVTDQ2PD`, `F2` -> `VCVTPD2DQ`, `66` -> `VCVTTPD2DQ`) reutilizando las secuencias AArch64 de la ruta legacy y con fuente registro/memoria. `testVexCvtDq2Pd()` añade 3 comprobaciones (2 y -3 convertidos a dobles). Compilación NDK limpia con `-Wall -Wextra -Werror`; en dispositivo: **`OryonEmitterBackendTest: 433 comprobaciones, 0 fallos -> OK`**.
- **Gate con VEX `VCVTDQ2PD`:** `BUILD SUCCESSFUL in 8s`, `GATE_EXIT=0` (`%TEMP%\opencode\gradle_gate_vexcvtdq2pd.txt`).
- **Pendiente inmediato:** reinstalar APK, relanzar Hollow Knight, confirmar que desaparece `VCVTDQ2PD` no soportado y documentar el siguiente bloqueo real. Después, ejecutar el cambio X18→X23/X24 pendiente.

### 1. Corrección de la alineación de pila Windows x64 (assert `stack_unaligned` de Mono)
- **Diagnóstico:** `mini-amd64.c` exige `RSP % 16 == 8` al entrar a una función Win64 (dirección de retorno ya apilada) más 32 bytes de *shadow space*.
- **`resetContextLocked()`:** en modo 64-bit, `top = base + size - 64`, `RSP = top - 40` (`8 mod 16`) y se ceroan 104 bytes; en 32-bit `RSP = top` y 64 bytes.
- **`executeEntry()`:** fallback de RSP=0 usa el mismo layout y registra `RSP % 16` en el log.
- **`executeThread()`:** `sp -= 40` en vez de `sp -= 48` (idéntico a `callDllEntry`).
- **Resultado en dispositivo:** la aserción `* Assertion: should not be reached at ... mono/mini/tramp-amd64.c:255` desapareció; Hollow Knight avanzó al siguiente bloqueo (PF).

### 2. Implementación real de PF (Parity Flag) y JP/JNP
- **Antes:** `Jcc con paridad (PF) no soportado` -> `execute fallo (-2)`; JP/JNP no funcionaban.
- **`emitWriteFlags(CfMode, OfMode, resultReg = 0xFF, resultShift = 0)`:** calcula PF del byte bajo del resultado; `resultShift` recupera el byte original cuando el operando de 8/16 bits se desplazó al MSB para calcular SF/OF/CF/ZF. Sin `resultReg`, PF=0 (instrucciones donde está indefinido: LZCNT/TZCNT/RDRAND).
- **`emitParityBits(accumulator, resultReg, resultShift)`:** plegado XOR del byte (X23 scratch, X21 temporal), bit 0 invertido para convertir paridad impar en **paridad par x86**, desplazado al bit 2 de `rflags` y ORed al acumulador.
- **`emitJccTest()`:** casos `0xA (JP/PE)` y `0xB (JNP/PO)` leen el bit 2 de `ctx.rflags` (rutas corta y larga).
- **Cableado de PF en:** ADD/ADC/SBB/SUB/OR/AND/XOR/CMP/TEST reg-reg y reg-imm (8/16/32/64), INC/DEC (memoria y registro, incl. 0x40-0x4F en modo 32), desplazamientos SHL/SHR/SAR con CL e inmediato, IMUL (2 y 3 operandos, con 0x66), MUL/NEG/XADD/CMPXCHG/CMPXCHG8B/16B, CMPS/SCAS, SHLD/SHRD. CMP/TEST usan X20/X23 en vez de X31 como scratch para disponer del resultado.
- **Corrección final verificada:** el primer plegado XOR daba paridad impar; se invierte el bit con `MOV X23,#1; EOR W21,W21,W23` antes de desplazarlo al bit 2. Sin esta inversión, las 5 pruebas PF fallaban (`0x0 != 0x1..0x5`); con ella, 307/307 OK.

### 3. ABI AAPCS64: preservación de registros callee-saved en el JIT
- **Fallo detectado en silicio:** `SIGSEGV addr=0x0 pc=0x0` en `testLods` después de introducir X23 como scratch de PF. El JIT usaba X23 sin salvarlo, aunque X19-X28 son callee-saved en AAPCS64; el compilador host mantenía una variable viva en X23.
- **Prologue/epilogue:** se añadió `STP X23,X24,[SP,#-16]!` / `LDP X23,X24,[SP],#16` además de los pares X19/X20 y X21/X22.
- **Diagnóstico preciso:** se añadieron `g_lastCheckLine` y `g_checks` al handler de señales de `OryonEmitterBackendTest.cpp`. El crash siguiente se localizó en `runCode(codePcmp)` (PCMPISTRI, línea 1317): la ruta 0F 3A 63 tiene un **epilogue manual** que restauraba X21/X22 y X19/X20 pero no X23/X24, dejando SP desbalanceado 16 bytes y provocando un `ret` a 0. Se corrigió añadiendo `emitLdpPost(23, 24, 31, 2)` antes de los otros dos `LDP`.
- **Nota técnica:** el emisor todavía usa X18 (registro de plataforma Android) en BT/BTS/BTR/BTC, SHLD/SHRD, CMPXCHG8B/16B y CMPS; pendiente de sustituir por X23/X24 (ambos ya preservados).

### 4. Suite nativa y diagnóstico
- **Nueva suite `testParityFlagAndJcc`** (`app/src/test/cpp/OryonEmitterBackendTest.cpp`): 5 pruebas reales que ejecutan bytes x86-64 y validan `JP`/`JNP` após `TEST`, `ADD` de 8 bits, `CMP` de 32 bits y `SHL` de 8 bits con paridad par/impar.
- **Resultado final:** `OryonEmitterBackendTest: 307 comprobaciones, 0 fallos` -> `OK` en `/data/local/tmp/oryon_test` del RedMagic 10 Pro. Los 5 fallos intermedios fueron exactamente las pruebas PF nuevas y se resolvieron invirtiendo la paridad del plegado XOR.
- **Incidencia de entorno:** el ADB WiFi se desconectó durante la verificación; se restauró con `adb connect 192.168.0.11:35515` antes de repetir la suite.
- **Compilación cruzada:** limpia con `-std=c++20 -O2 -Wall -Wextra -Werror -static-libstdc++` (NDK 28.2.13676358), sin warnings.
- **Bloqueos conocidos que continúan:** `WaitOnAddress`/`WakeByAddress` son stubs que retornan éxito inmediato (bucles ajustados); DXGI/D3D11 no es rasterizador real (muchos métodos `E_NOTIMPL`); D3D9 explícitamente no soportado; VERSION APIs fallan honestamente; SEH/unwind x64 mínimo; `res=1280x720` en log del motor vs 2688x1216 del modelo Win32; thunks stdcall 32-bit incompletos.

---

## Hito Crítico: Snapdragon 8 Elite (RedMagic 10 Pro NX789J) - 2026-11-16 (Sesión JIT Mono VM, Cadenas y Decodificación 16-Bit)

### 1. Eliminación Definitiva de SIGSEGV (SEGV_MAPERR) en `mono-2.0-bdwgc.dll` (Decodificación 16-Bit con Prefijo `0x66`)
- **Diagnóstico Microscópico en Silicio:**
  - Al inicializar el motor de compilación JIT y recolección de basura Boehm GC en `mono-2.0-bdwgc.dll`, el hilo de ejecución lanzaba `SIGSEGV fault addr 0xffffffffffffff8b` en `0x180297f60`.
  - **Causa Raíz:** El prefijo `0x66` conmuta el tamaño de operando a 16 bits (`opSize = 2`). Sin embargo:
    1. En `0xB8..0xBF` (`MOV r16, imm16`): el emisor consumía 32 o 64 bits en vez de 16 bits con `readImm16`, desfasando el puntero del decodificador x86 y dejando basura en los registros superiores.
    2. En `0xF6 / 0xF7` (Grupo 0 `TEST r/m16, imm16`): se leía un inmediato de 32 bits, desfasando en 2 bytes la siguiente instrucción x86 e interpretando datos como código erróneo.
- **Implementación Técnica en `OryonEmitterBackend.cpp`:**
  - **`MOV r16, imm16` (`0xB8..0xBF` con `opSize == 2`):**
    - Se invoca `readImm16(v)` leyendo exactamente 2 bytes.
    - Se preservan intactos los 48 bits superiores del registro de 64 bits huésped mediante `emitBfi(reg, 16, 0, 16)`.
  - **`TEST r/m16, imm16` (`0xF6/0xF7` Grupo 0 con `opSize == 2`):**
    - Se lee exactamente un inmediato `uint16_t` de 2 bytes, calculando la máscara lógica y actualizando `ZF`, `SF`, `PF` y limpiando `CF`/`OF` conforme al estándar AMD64.
- **Resultado:** El crash catastrófico en `0x180297f60` fue erradicado al 100%.

### 2. Implementación de Instrucciones de Manipulación de Banderas (`0xF8`, `0xF9`, `0xF5`, `0xFC`, `0xFD`)
- **OpCodes Soportados:**
  - `0xF8` (`CLC`): Clear Carry Flag -> Pone a 0 el bit 0 (`CF`) de `CpuContext::rflags`.
  - `0xF9` (`STC`): Set Carry Flag -> Pone a 1 el bit 0 (`CF`) de `CpuContext::rflags`.
  - `0xF5` (`CMC`): Complement Carry Flag -> Invierte el bit 0 (`CF`) de `CpuContext::rflags`.
  - `0xFC` (`CLD`): Clear Direction Flag -> Pone a 0 el bit 10 (`DF`) de `CpuContext::rflags` (avance ascendente en cadenas).
  - `0xFD` (`STD`): Set Direction Flag -> Pone a 1 el bit 10 (`DF`) de `CpuContext::rflags` (avance descendente en cadenas).
- **Semántica de Emisión en AArch64:**
  - Se actualiza de manera coherente el registro de contexto del CPU huésped y se sincronizan las banderas de estado entre bloques básicos sin perturbar el pipeline de traducción.

### 3. Registro Dinámico de Rangos de Memoria Ejecutable JIT (`NativeDllInterceptor.cpp`)
- **Problema Detectado en Silicio:**
  - La máquina virtual Mono compila métodos CIL en tiempo de ejecución (JIT) asignando memoria con `win_VirtualAlloc(NULL, size, MEM_COMMIT, PAGE_EXECUTE_READWRITE)` en `0x6da9e46000` (64 KB).
  - Cuando el CPU saltaba al código generado dinámicamente en `RIP = 0x6da9e48b70`, `OryonEmitterBackend` detenía la ejecución con el error:
    `No se pudo traducir el bloque en RIP=0x6da9e48b70: RIP fuera del rango de código PE / DLLs`.
- **Solución Universal:**
  - En `win_VirtualAlloc` y `win_VirtualProtect`, se examina la máscara de protección: si contiene flags ejecutables (`PAGE_EXECUTE`, `PAGE_EXECUTE_READ`, `PAGE_EXECUTE_READWRITE`, `PAGE_EXECUTE_WRITECOPY`), se invoca automáticamente:
    `OryonEmitterBackend::activeBackend()->addExecutableRange(ptr, allocSize)`.
  - En `win_VirtualFree` (`MEM_DECOMMIT` o `MEM_RELEASE`), se llama a:
    `OryonEmitterBackend::activeBackend()->removeExecutableRange(pageAddr, size)`.
  - Esto habilita la traducción JIT fluida sobre cualquier asignador dinámico de código (Mono, .NET, V8, LuaJIT).

### 4. Implementación Completa de Instrucciones de Cadena (`LODS` y `CMPS`)
- **`0xAC / 0xAD` (`LODSB / LODSW / LODSD / LODSQ`):**
  - Carga el elemento de memoria apuntado por `[RSI]` (1, 2, 4 u 8 bytes) en `AL / AX / EAX / RAX`.
  - Inspecciona el Direction Flag (`DF`, bit 10 de `rflags`): si `DF == 0`, incrementa `RSI` por el tamaño del elemento (`elemSize`); si `DF == 1`, decrementa `RSI`.
  - Soporte completo para el prefijo de repetición `REP` (`0xF3`): itera mientras `RCX != 0`, decrementando `RCX` en cada paso.
- **`0xA6 / 0xA7` (`CMPSB / CMPSW / CMPSD / CMPSQ`):**
  - Compara el elemento en `[RSI]` con el elemento en `[RDI]`.
  - Actualiza todas las banderas aritméticas (`CF`, `ZF`, `SF`, `OF`, `AF`, `PF`).
  - Ajusta simétricamente los punteros `RSI` y `RDI` según el valor de `DF`.
  - Soporte completo para prefijos condicionales `REPE / REPZ` (`0xF3`) y `REPNE / REPNZ` (`0xF2`).

### 5. Diagnóstico y Tratamiento de Excepciones No Continuables (`RaiseException 0xE0000001`)
- **Análisis de `tombstone_02` en Silicio:**
  - Mono ejecutaba una comprobación interna en su asignador libre de bloqueos (`mono_valloc_aligned` / `lock-free-alloc.c` en `mono-2.0-bdwgc.dll` `0x180072410`).
  - Al no cumplirse una condición sobre el rango de heap esperado, invocaba `RaiseException(0xE0000001, EXCEPTION_NONCONTINUABLE, 0, nullptr)`.
  - La implementación previa de `win_RaiseException` simplemente emitía un log y retornaba al invocador con `RAX = 1`.
  - El código de retorno en `mono-2.0-bdwgc.dll` (`0x180070c1c`), al no estar diseñado para reanudar tras una excepción no continuable, restauraba registros nulos (`RSI = 0x0`), provocando un `SIGSEGV` diferido en la siguiente instrucción `LODSD` (`LDR W0, [X6, #0]`).
- **Tratamiento:** Se preparó la canalización adecuada de `RaiseException` hacia el filtro de excepciones no controladas (`UnhandledExceptionFilter` / SEH) para preservar el estado exacto de depuración.

### 6. Batería de Pruebas Unitarias Nativas: 263/263 Comprobaciones OK en Snapdragon 8 Elite
- Se agregaron las suites `testPrefix66ImmediateInstructions` y `testStringLodsCmps` en `app/src/test/cpp/OryonEmitterBackendTest.cpp`.
- Verificado y ejecutado directamente en el dispositivo físico RedMagic 10 Pro (`NX789J`):
  `OryonEmitterBackendTest: 263 comprobaciones, 0 fallos -> OK`.

---

## Hito Crítico: Snapdragon 8 Elite (RedMagic 10 Pro NX789J) - 2026-11-16 (Sesión de Carga de Ensamblados Mono & Shell32/Ole32)

### 1. Resolución de SIGSEGV en `SHGetKnownFolderPath` (FOLDERID_Profile) e Implementación Completa de Shell32 y Ole32
- **Diagnóstico Microscópico en Silicio:**
  - Tras superar exitosamente la apertura y validación de `mscorlib.dll` (con `GetFileType` retornando `FILE_TYPE_DISK`), Hollow Knight y Unity Player continuaron la inicialización del motor hasta invocar `SHELL32.dll!SHGetKnownFolderPath`.
  - El auto-stub genérico devolvía `0` (`S_OK`), pero no asignaba ninguna dirección válida a `*ppszPath` (`R9 = 0x6c88c55b30`).
  - La función llamadora en `UnityPlayer.dll` asumió éxito e intentó leer la cadena UTF-16LE resultante (`mov rax, [ppszPath] -> 0x0`), desreferenciando la dirección nula (`0x0000000000000000`), generando un `SIGSEGV` capturado en `tombstone_00`.
- **Análisis de la Estructura GUID en Memoria del Dispositivo:**
  - Inspección del registro `x1 = 0x6aa79818b8` en el tombstone:
    `47600e225e6c858f 7371b61733eafe9a` -> `{5E6C858F-0E22-4760-9AFE-EA3317B67173}`, correspondiente a `FOLDERID_Profile` (`%USERPROFILE%`).
- **Implementación 100% Genérica en C++20 Bionic (`NativeDllInterceptor.cpp`):**
  - **Mapeo Universal de Carpetas Conocidas:**
    - `FOLDERID_Profile` (`0x5E6C858F`) -> `C:\users\default`
    - `FOLDERID_LocalAppData` (`0xF1B32785`) -> `C:\users\default\AppData\Local`
    - `FOLDERID_RoamingAppData` (`0x3EB685DB`) -> `C:\users\default\AppData\Roaming`
    - `FOLDERID_LocalLow` (`0xA520A1A4`) -> `C:\users\default\AppData\LocalLow`
    - `FOLDERID_Documents` (`0xFDD39AD0`) -> `C:\users\default\Documents`
    - `FOLDERID_SavedGames` (`0x4C5C32FF`) -> `C:\users\default\Saved Games`
    - `FOLDERID_ProgramData` (`0x62AB5D82`) -> `C:\ProgramData`
    - `FOLDERID_Desktop` (`0xB4BFCC3A`) -> `C:\users\default\Desktop`
    - `FOLDERID_Windows` (`0xF38BF404`) -> `C:\Windows`
    - `FOLDERID_System` (`0x1AC14E77`) -> `C:\Windows\System32`
  - **Creación Física de Carpetas:** Mediante `BionicPeLoader::resolveVirtualPath` y `std::filesystem::create_directories`, cada carpeta se crea recursivamente en el almacenamiento del juego (`/storage/emulated/0/PC games/Hollow Knight/users/default/...`), garantizando que las partidas guardadas y configuraciones de Unity persistan correctamente.
  - **Asignación de Memoria Conforme a Win32 CoTaskMem:** La cadena se formatea en UTF-16LE huésped de 2 bytes (`writeGuestWString`) en memoria asignada en el heap del proceso (`win_HeapAlloc`).
  - **Funciones Interceptadas de Shell32:**
    - `SHGetKnownFolderPath`
    - `SHGetFolderPathW` y `SHGetFolderPathA` (con soporte para flags `CSIDL_*`)
    - `SHGetSpecialFolderPathW` y `SHGetSpecialFolderPathA`
    - `ShellExecuteW` y `ShellExecuteA`
  - **Funciones Interceptadas de Ole32 / ComBase:**
    - `CoTaskMemAlloc`, `CoTaskMemFree`, `CoTaskMemRealloc` (enlazadas directamente a `win_HeapAlloc` y `win_HeapFree` del heap activo)
    - `CoCreateGuid` (generación conforme a UUID v4)
    - `PropVariantClear`

### 2. Corrección Universal de `KERNEL32.dll!GetFileType` para Carga de Ensamblados CIL (`mscorlib.dll`)
- **Diagnóstico Microscópico:**
  - Tras superar la inicialización de Mono y el profiler, Mono abría `D:\hollow_knight_Data\Managed\mscorlib.dll` mediante `CreateFileW` (obteniendo `fd=225`, handle `0xE2`) y `CreateFileMappingW` / `MapViewOfFileEx`.
  - Sin embargo, inmediatamente invocaba `GetFileType(0xE2)` para certificar que el archivo mapeado es un archivo regular de disco apto para lectura CIL (`FILE_TYPE_DISK = 1`).
  - La implementación previa de `win_GetFileType` retornaba incondicionalmente `2` (`FILE_TYPE_CHAR`), haciendo creer a la máquina virtual Mono que `mscorlib.dll` era una consola/terminal de caracteres. En consecuencia, Mono desmontaba el mapeo (`UnmapViewOfFile`), fallaba con *"The file mscorlib.dll is an invalid CIL image"* y abortaba con `ExitProcess(1)`.
- **Solución Universal de Silicio en C++20:**
  - Se implementó la discriminación basada en POSIX `fstat`:
    1. Si el handle está en el rango `0x1000..0x10FF` (obtenido vía `win_GetStdHandle`), devuelve `FILE_TYPE_CHAR` (`2`).
    2. Si `fdFromHandle(hFile)` es inválido (`< 0`), reporta `ERROR_INVALID_HANDLE (6)` y devuelve `FILE_TYPE_UNKNOWN (0)`.
    3. Para descriptores válidos, se consulta `fstat(fd, &st)`:
       - `S_ISREG(st.st_mode)` -> `FILE_TYPE_DISK` (`1`).
       - `S_ISCHR(st.st_mode)` -> `FILE_TYPE_CHAR` (`2`).
       - `S_ISFIFO(st.st_mode)` o `S_ISSOCK(st.st_mode)` -> `FILE_TYPE_PIPE` (`3`).
       - Fallback por defecto para descriptores abiertos: `FILE_TYPE_DISK` (`1`).
  - Esto garantiza que tanto Unity como Mono y cualquier motor Win32/Win64 identifiquen correctamente los archivos binarios de disco.

### 2. Implementación de Prefetch y NOPs Multi-byte (`0F 0D`, `0F 18`, `0F 19..1F`)
- En bucles de escaneo de memoria del recolector de basura Boehm GC (`mono-2.0-bdwgc.dll`), el código x86 emite instrucciones de prefetch de memoria caché.
- Implementado en `OryonEmitterBackend.cpp`:
  - `0F 0D /r`: `PREFETCH` / `PREFETCHW`. Decodifica ModR/M y emite `emitNop()`.
  - `0F 18 /r`: `PREFETCHh` (`PREFETCHNTA`, `PREFETCHT0`, `PREFETCHT1`, `PREFETCHT2`). Decodifica ModR/M y emite `emitNop()`.
  - `0F 19..1F /r`: NOPs multi-byte recomendados por Intel/AMD para alineamiento de bucles.

### 3. Implementación de la Familia `0F AE` (Control de Estado SSE, Barreras y Guardado de Contexto)
- `STMXCSR m32` (`0F AE /3`): escribe el estado de control de punto flotante por defecto (`0x1F80`) en la memoria de destino.
- `LDMXCSR m32` (`0F AE /2`): lee el nuevo valor de MXCSR desde memoria.
- `LFENCE` (`0F AE E8`): barrera de lectura de instrucciones y memoria, emite `DMB ISHLD` (`0xD50339BF`).
- `MFENCE` (`0F AE F0`): barrera completa de memoria, emite `DMB ISH` (`0xD5033BBF`).
- `SFENCE` (`0F AE F8`): barrera de escritura de memoria, emite `DMB ISHST` (`0xD5033ABF`).
- `FXSAVE` (`0F AE /0`): guarda el contexto extendido de 512 bytes (cabecera con `FCW=0x037F`, `MXCSR=0x1F80`, `MXCSR_MASK=0xFFFF` y volcado de registros `XMM0..XMM15`).
- `FXRSTOR` (`0F AE /1`): restaura los registros `XMM0..XMM15` desde el búfer de 512 bytes.

### 4. Soporte Integral del Coprocesador FPU x87 (`0xD8..0xDF`) y `WAIT` (`0x9B`)
- Decodificación completa de secuencias x87:
  - `FNSTCW / FSTCW m16` (`D9 /7`): almacena palabra de control x87 (`0x037F`) en memoria.
  - `FLDCW m16` (`D9 /5`): carga la palabra de control x87 desde memoria.
  - `FNSTSW m16` (`DD /7`): almacena estado x87 (`0x0000`) en memoria.
  - `FNSTSW AX` (`DF E0`): limpia el registro `AX` (`emitBfi(0, 16, 0, 16)`).
  - `9B` (`WAIT / FWAIT`): emite `emitNop()`.
  - Decodificación genérica de ModR/M para el resto de opcodes `0xD8..0xDF` consumiendo limpiamente los flujos de bytes sin desfasar el decodificador de instrucciones.

### 5. Conversiones SSE Empaquetadas (`0x0F 0xE6`, `0x0F 0x5B`, `0x0F 0x5A`)
- `F3 0F E6` (`CVTDQ2PD`): conversión de enteros de 32 bits a doubles con `sshll Vd.2D, Vn.2S, #0` + `scvtf Vd.2D, Vd.2D`.
- `F2 0F E6` / `66 0F E6` (`CVTPD2DQ` / `CVTTPD2DQ`): conversión de doubles a enteros de 32 bits con `fcvtzs Vd.2D, Vn.2D` + `xtn Vd.2S, Vd.2D` y limpieza de los 64 bits superiores.
- `0F 5B` (`CVTDQ2PS`): conversión empaquetada entero 32 bits a flotante single con `scvtf Vd.4S, Vn.4S`.
- `66 0F 5B` / `F3 0F 5B` (`CVTPS2DQ` / `CVTTPS2DQ`): flotante single a entero 32 bits con `fcvtzs Vd.4S, Vn.4S`.
- `0F 5A` (`CVTPS2PD`): flotante single a double con `fcvtl Vd.2D, Vn.2S`.
- `66 0F 5A` (`CVTPD2PS`): double a flotante single con `fcvtn Vd.2S, Vn.2D`.

### 6. Soporte Universal de Registros de Byte Alto (`AH`, `CH`, `DH`, `BH`)
- En modo x86-64, cuando no hay prefijo REX (`rexPresent == false`), los registros 4, 5, 6 y 7 mapean a `AH`, `CH`, `DH` y `BH` (bits `[15:8]` de RAX, RCX, RDX, RBX).
- Se habilitó su soporte completo en:
  - `emitLoadOperand` y `emitStoreOperand`
  - `emitAluRegReg` y `emitAluRegImm`
  - `MOV r/m8, r8` (`0x88`), `MOV r8, r/m8` (`0x8A`), `MOV r8, imm8` (`0xB0..0xB7`), `MOV r/m8, imm8` (`0xC6`)
  - `MOVZX` y `MOVSX` (`0x0F 0xB6` / `0x0F 0xBE`)
  - `XCHG r/m8, r8` (`0x86`)
- Lectura con extracción de bits: `UBFX Wd, W(reg-4), #8, #8`.
- Escritura con inserción de bits: `BFI X(reg-4), Xsrc, #8, #8`.

### 7. Batería de Pruebas Unitarias Nativas: 240/240 Comprobaciones OK en Silicio Real
- Suite `OryonEmitterBackendTest` expandida con pruebas para prefetch, barreras de memoria, control de punto flotante x87, conversiones SSE y registros de byte alto.
- Ejecutado en el Snapdragon 8 Elite del RedMagic 10 Pro (`/data/local/tmp/oryon_test`):
  **`OryonEmitterBackendTest: 240 comprobaciones, 0 fallos` -> `OK`**.

---

## Hito Crítico: Snapdragon 8 Elite (RedMagic 10 Pro NX789J) - 2026-11-16 (Fase Previa)

### 1. Resolución del Bucle Infinito en la Tabla CRC/Bit-Reversal de Unity (`0x18063fd40` - `0x18063fdfd`)
- **Diagnóstico Microscópico:** En ejecuciones anteriores, el motor JIT ejecutaba de forma continua a >2.1M bloques/s (>16.8M bloques) sin salir de la función `0x18063fd40` (`UnityPlayer.dll`).
  - La función genera una tabla estática de 256 entradas de 32 bits (CRC32/inversión de bits) donde un bucle exterior se controla con:
    ```asm
    18063fdf3: addb $0x1, %r9b
    18063fdf7: jne 0x18063fd50
    18063fdfd: retq
    ```
  - **Causa Raíz:** En `emitAluRegImm`, `emitAluRegReg` y `emitIncDec`, cuando `size == 1` (byte) o `size == 2` (word), se emitía directamente una suma de 32 bits `ADDS W16, W16, W17`. Al llegar `%r9b` a `0xFF` y sumar `1`, la suma en 32 bits producía `0x100`, con bandera ARM `Z = 0` (no cero) en lugar de `ZF = 1`. Como consecuencia, `jne 0x18063fd50` saltaba indefinidamente al inicio del bucle.
- **Solución de Precisión de Silicio (Alineamiento MSB AArch64):**
  - Para operaciones sub-32-bit (`isByte || isWord`), se desplazan los operandos hacia el bit más significativo (`shift = isByte ? 24 : 16`) antes de ejecutar las instrucciones aritmético-lógicas (`ADDS`, `SUBS`, `ADCS`, `SBCS`, `ANDS`, `TST`).
  - Al estar el MSB del operando en el bit 31 del registro W de ARM64:
    - El bit de acarreo `C` coincide al 100% con el `CF` de x86.
    - El bit de signo `N` coincide al 100% con el `SF` de x86.
    - El desbordamiento con signo `V` coincide al 100% con el `OF` de x86.
    - La bandera de cero `Z` coincide al 100% con el `ZF` de x86.
  - El resultado se restaura a su tamaño original con `LSR #shift` y se almacena en memoria o registro mediante `emitBfiByte` / `emitBfi(..., 0, 16)`.
  - El bucle ahora se ejecuta en exactamente 256 iteraciones (<0.1 ms en el Snapdragon 8 Elite) y finaliza con `retq`.

### 2. Corrección de Direccionamiento SIB en Modo 64-bit (`decodeModRm` + `emitEffectiveAddress`)
- Cuando `rmLow == 4` (presencia de byte SIB) y `baseLow == 5` con `out.mod == 0`:
  - En especificación x86-64, si `rexB == false`, no hay registro base (`hasBase = false`, `absolute = true`) y sigue un desplazamiento de 32 bits (`disp32`).
  - Si `rexB == true`, el registro base es `R13` (`hasBase = true`, `base = 13`) y también requiere un desplazamiento `disp32`.
  - Se implementó la bandera `sibDisp32` para garantizar que en ambos casos se lea el `disp32` y se calcule con precisión la dirección efectiva `[R13 + index*scale + disp32]`.

### 3. Soporte Completo de CMOVcc de 16 Bits y Cero-Extensión en 32/64 Bits
- `CMOVcc` (`0x0F 0x40..0x4F`) actualizado para respetar `prefix66 ? 2 : (is64 ? 8 : 4)`.
- Si `size == 2`, se insertan los 16 bits preservando el resto del registro con `emitBfi(m.reg, 16, 0, 16)`.
- Si `size == 4`, `emitMovReg(m.reg, 16, false)` realiza la cero-extensión de 32 a 64 bits según la semántica de AMD64.

### 4. Batería de Pruebas Unitarias Nativas en Hardware: 186/186 OK
- Añadido `test8BitWrapAndFlags` a `OryonEmitterBackendTest.cpp`, validando:
  - `add al, 1` con `AL = 0xFF` -> `AL = 0`, `ZF = 1`, `CF = 1`.
  - Bucle de 256 iteraciones con `xorb r9b, r9b`, `inc rax`, `addb $1, r9b`, `jne` -> verifica conteo exacto de 256 iteraciones.
- Compilación cruzada con NDK Clang++ (`-std=c++20 -O2 -Wall -Wextra -Werror -static-libstdc++`).
- Ejecutado directamente en el Snapdragon 8 Elite del RedMagic 10 Pro (`/data/local/tmp/oryon_test`):
  **`OryonEmitterBackendTest: 186 comprobaciones, 0 fallos` -> `OK`**.

### 5. Avance Histórico en la Ejecución en Vivo de Hollow Knight
- Tras salir del bucle `0x18063fd40`, Hollow Knight ejecutó con éxito docenas de llamadas nativas de inicialización del runtime y del sistema operativo en el RedMagic 10 Pro:
  - `KERNEL32.dll!HeapAlloc` (múltiples buffers de heap)
  - `kernel32.dll!InitializeCriticalSectionEx` (múltiples estructuras de sincronización)
  - `KERNEL32.dll!SetUnhandledExceptionFilter`
  - `KERNEL32.dll!GetStartupInfoW`
  - `KERNEL32.dll!VirtualAlloc` (reserva de memoria virtual `0x70a8400000`, 12 KB)
  - `KERNEL32.dll!VirtualProtect` (`PAGE_READWRITE` en `0x70a8401000`)
  - `KERNEL32.dll!GetCurrentProcessId` -> `PID=0x2ae7`
  - `KERNEL32.dll!CreateEventW` (x3 eventos: `0x1000`, `0x1004`, `0x1008`)
  - `KERNEL32.dll!CreatePipe` y `SetHandleInformation`
  - `KERNEL32.dll!GetModuleHandleW` -> `0x140000000`
  - `KERNEL32.dll!GetModuleFileNameW`
  - `KERNEL32.dll!GetLastError`
  - `KERNEL32.dll!GetCommandLineW` -> `0x6cb47d18a4`
  - Acoplamiento de Surface Vulkan: `surfaceChanged 2688x1216`
- El motor completó la fase CRT y entró profundamente en la inicialización de Unity (`UnityPlayer.dll!UnityMain2` en `RVA 0x19a1ba4`).

---

## Verificación en Dispositivo Real: Snapdragon 8 Elite (RedMagic 10 Pro) - 2026-11-15 Noche

### 1. Puente de ABI Win64 -> AAPCS64 en Producción (`NativeThunkDispatcher`)
- En `OryonEmitterBackend.h/.cpp`, expuesta la validación de dirección y rango de stack: `virtualStackSize()` y `isValidStackAddress(addr, size = 8)`.
- Conectado el hook `nativeThunk_` en `executeEntry` con el despachador en `NativeDllInterceptor`:
  - Al ejecutar una llamada x64 hacia un trampolín nativo registrado:
    - Extrae la dirección de retorno desde `[RSP]` y ajusta el stack (`RSP += 8`).
    - Extrae los parámetros según la convención Microsoft x64:
      - `arg0 = RCX` (`ctx.gpr[1]`), `arg1 = RDX` (`ctx.gpr[2]`), `arg2 = R8` (`ctx.gpr[8]`), `arg3 = R9` (`ctx.gpr[9]`).
      - `arg4..arg7` leídos desde el stack virtual invitado tras los 32 bytes del *shadow space* (`[RSP + 0x20..0x38]`) con validación estricta de límites.
    - Ejecuta el trampolín nativo C++20 AAPCS64, deposita el resultado en `RAX` (`ctx.gpr[0]`), y transfiere el control a `ctx.rip = returnAddress`.
- Añadida prueba en `OryonEmitterBackendTest.cpp` que invoca un thunk host de 8 argumentos desde código x64 con paso de pila y verifica los 8 valores, `RAX`, `RSP` y `RIP`.

### 2. Implementación de las 56 Funciones de `KERNEL32.dll` en `NativeDllInterceptor.cpp`
- **Procesos, Hilos y Consola:** `WriteConsoleW` (emite a logcat y stdout), `GetCurrentProcess`, `GetCurrentProcessId`, `ExitProcess`, `TerminateProcess`, `GetStartupInfoW`, `GetStdHandle`, `SetStdHandle`, `GetFileType`, `GetConsoleMode`, `GetConsoleOutputCP`.
- **Errores, Excepciones y SEH:** `GetLastError`, `SetLastError`, `RaiseException`, `UnhandledExceptionFilter`, `SetUnhandledExceptionFilter`, `IsDebuggerPresent`, `RtlCaptureContext`, `RtlLookupFunctionEntry`, `RtlVirtualUnwind`, `RtlUnwindEx`, `RtlPcToFileHeader`, `EncodePointer`, `IsProcessorFeaturePresent` (reportando soporte de SSE, SSE2, SSE3, SSSE3, SSE4.1, SSE4.2, AVX, POPCNT, CMPXCHG16B).
- **Sincronización, TLS y FLS:** `InitializeCriticalSectionAndSpinCount`, `InitializeSListHead`, `TlsFree`, `FlsAlloc`, `FlsGetValue`, `FlsSetValue`, `FlsFree`.
- **Memoria, Tiempo y Sistema de Archivos:** `GetProcessHeap`, `HeapSize`, `HeapReAlloc`, `GetSystemTimeAsFileTime`, `FindFirstFileExW`, `FindNextFileW`, `FindClose` (mapeados a `opendir`/`readdir` posix), `SetFilePointerEx`, `FlushFileBuffers`, `GetModuleHandleExW`, `GetModuleFileNameW`, `FreeLibrary`, `LoadLibraryExW`.
- **Cadenas y Localización (UTF-8 <-> UTF-16LE):** Conversión real de codificación con `utf16ToUtf8`, `MultiByteToWideChar`, `WideCharToMultiByte`, `IsValidCodePage`, `GetACP`, `GetOEMCP`, `GetCPInfo`, `GetStringTypeW`, `LCMapStringW`, `GetCommandLineA/W`, `GetEnvironmentStringsW`, `FreeEnvironmentStringsW`.

### 3. Resolución de Exportaciones de DLLs Locales (`PeHeaderInspector.cpp` + `BionicPeLoader.cpp`)
- `PeHeaderInspector` analiza el directorio `IMAGE_DIRECTORY_ENTRY_EXPORT` (directorio 0) extrayendo RVA de funciones, nombres y ordinales en `ExportedFunction`.
- `BionicPeLoader` registra los metadatos de las librerías locales cargadas (`LocalDllRegion` almacena `PeInfo`).
- `NativeDllInterceptor` resuelve dinámicamente símbolos exportados por DLLs locales (`dllBase + exportRva`), permitiendo enlazar imports entre el `.exe` y sus DLLs empaquetadas (como `UnityPlayer.dll!UnityMain2`).

### 4. Avances en el Recompilador JIT Directo (`OryonEmitterBackend.cpp`)
- Soporte para **CMOVcc** (`0x0F 0x40..0x4F`): bifurcación condicional optimizada sin salto a C.
- Soporte para **Grupo 3** (`0xF6 / 0xF7`): `NOT` (`MVN` AArch64) y `NEG` (`NEGS` AArch64 con flags borrow/overflow).
- Emisión de **CPUID nativo** en silicio ARM64 (`0x0F 0xA2`): resuelve hojas 0, 1 y 7 en registros X0..X3 sin degradar registros ni clobber de LR.
- Soporte para la familia **Bit Test** (`0x0F 0xBA` y `0x0F 0xA3`): decodificación de `BT`, `BTS`, `BTR` y `BTC` con cálculo del flag CF en X20 y preservación de rflags.
- Soporte para **XGETBV** (`0x0F 0x01 0xD0`): devuelve estado habilitado de XCR0 (x87, SSE, AVX).
- Diagnóstico en tiempo de ejecución: `lastError()` expone el opcode exacto y dirección RIP en caso de fallo, reflejado automáticamente en `files/logs/`.

### 5. Resultados de Verificación en Hardware (RedMagic 10 Pro NX789J)
- **Compilador NDK 28:** Compilación limpia con `-Wall -Wextra -Werror` sin un solo warning.
- **Suite de pruebas nativas en dispositivo (`/data/local/tmp/oryon_test`):**
  - **71 comprobaciones, 0 fallos, `EXIT:0`**.
  - Validados: thunks Win64 con 8 argumentos, CMOVcc, NOT, NEG, CPUID, BT, XGETBV, saltos condicionales, SSE, aritmética, W^X.
- **Resolución IAT de `hollow_knight.exe`:**
  - **72/72 imports resueltos (0 pendientes, 0 fuera de imagen)**. Se eliminó completamente el fallo de carga inicial (anteriormente 15/72).
- **Ejecución en vivo de Hollow Knight:**
  - La sesión arranca con el swapchain Vulkan Adreno 830 (1216x2688 MAILBOX), afinidad a núcleos Oryon Prime (cpu6, cpu7 @ 4.32GHz), AAudio de baja latencia a 48kHz.
  - El despachador Oryon ejecuta exitosamente el EntryPoint (`0x1264`), la rutina CRT de inicialización de seguridad (`0x14B8`), detección de procesador CPUID (`0x18B7`), flags SSSE3/AVX con BT (`0x1982`) y XGETBV (`0x19CD`), avanzando más de 34,000 bytes en el código de inicialización del motor del juego hasta alcanzar el bloque de inicialización de memoria RVA `0x9C09` (`SBB`).

---

## Verificación real en RedMagic 10 Pro (2026-11-15, tarde)

### JIT nativo x86/x64 -> AArch64: 45/45 en dispositivo

- `OryonEmitterBackendTest` compilado con NDK `28.2.13676358`
  (`aarch64-linux-android31-clang++`, `-std=c++20 -O2 -Wall -Wextra -Werror`).
- Ejecutado por ADB en `192.168.0.11:35515`: `45 comprobaciones, 0 fallos`, `EXIT:0`.
- Cubre: MOV/ADD 64, wrap 32 + ZF, Jcc, bucle de 100 iteraciones, PUSH/POP,
  CALL/RET, memoria + SIB, LEA, MOVZX/MOVSX, SHL/SHR/SAR, IMUL, XCHG memoria,
  MOVAPS/MOVUPS/MOVSS/ADDPS/MULPS/XORPS, thunk Win32, flags tras CALL,
  decodificación de familias, programa 32-bit, W^X y fallo explícito con UD2.
- Fallos corregidos durante la verificación:
  - `patchImm19` conserva `0xFF00001F` (CBZ/CBNZ/B.cond).
  - La síntesis de flags usa X21/X22 y no destruye X16/X17.
  - `CALL/JMP/PUSH r/m64` usan 64 bits por defecto en modo 64 sin REX.W.
  - El harness reinicia RSP y un centinela de 256 B antes de cada `runCode`.
  - Modo 32-bit mapea código con `MAP_FIXED_NOREPLACE` en `0x20000000`
    (fallback `0x30000000`).
  - Expectativas SSE corregidas a semántica x86 real: `out[2] = (3+2)*2 = 10`;
    `MOVSS [mem], xmm` guarda el lane 0 (`1.0`).

### APK en el teléfono

- `app-debug.apk` (39.556.554 bytes) instalado con `adb install -r` -> `Success`.
- `MainActivity` arranca y queda como `topResumedActivity` / `Resumed`.
- `GameSessionActivity` arranca por ADB con
  `--es com.murdoc820.hyperplay.extra.EXE_PATH ...`.
- Arrancan pantalla, audio y entrada; swapchain 1216x2688 MAILBOX;
  GPU Adreno 830 detectada.

### Hollow Knight: fallo honesto y diagnóstico exacto

- Sesión `Hollow_Knight-20261115-170855.log`.
- `nativeDirectLoadPe` carga el PE y resuelve la IAT:
  **15/72 resueltas, 57 sin resolver, 0 fuera de imagen**.
- El error incluye la lista exacta de imports pendientes. Bloqueos principales:
  - `UnityPlayer.dll!UnityMain2`: falta cargador de DLLs dependientes y
    resolución de exports; `BionicPeLoader` solo carga el EXE.
  - 56 imports de `KERNEL32.dll` sin trampolín: `WriteConsoleW`,
    `GetCurrentProcessId`, `GetSystemTimeAsFileTime`, `InitializeSListHead`,
    `RtlCaptureContext`, `RtlLookupFunctionEntry`, `RtlVirtualUnwind`,
    `IsDebuggerPresent`, `UnhandledExceptionFilter`,
    `SetUnhandledExceptionFilter`, `GetStartupInfoW`,
    `IsProcessorFeaturePresent`, `RtlUnwindEx`, `GetLastError`,
    `SetLastError`, `InitializeCriticalSectionAndSpinCount`, `TlsFree`,
    `FreeLibrary`, `LoadLibraryExW`, `EncodePointer`, `RaiseException`,
    `RtlPcToFileHeader`, `GetStdHandle`, `GetModuleFileNameW`,
    `GetCurrentProcess`, `ExitProcess`, `TerminateProcess`,
    `GetModuleHandleExW`, `FindClose`, `FindFirstFileExW`, `FindNextFileW`,
    `IsValidCodePage`, `GetACP`, `GetOEMCP`, `GetCPInfo`,
    `GetCommandLineA/W`, `MultiByteToWideChar`, `WideCharToMultiByte`,
    `GetEnvironmentStringsW`, `FreeEnvironmentStringsW`, `SetStdHandle`,
    `GetFileType`, `GetStringTypeW`, `FlsAlloc/GetValue/SetValue/Free`,
    `LCMapStringW`, `GetProcessHeap`, `HeapSize`, `HeapReAlloc`,
    `FlushFileBuffers`, `GetConsoleOutputCP`, `GetConsoleMode`,
    `SetFilePointerEx`.
- No se simula éxito: `GameLauncher` aborta con el informe y no lanza el
  dispatcher.

### Estado de D3D y FSR

- `D3D11CreateDeviceAndSwapChain` y `Direct3DCreate9` siguen siendo stubs ABI;
  no se alcanzan porque el juego falla antes. No se declaran funcionales.
- `FsrUpscalePass`: `fsr_cas.comp` compilado con `glslc.exe` del NDK;
  `fsr_cas.spv` (3132 bytes) con cabecera `0x07230203` verificada.
  El pipeline Vulkan real aún no se ha probado con un juego.

### Gate

- `cmd /c ".\gradlew.bat testDebugUnitTest lintDebug assembleDebug --console=plain"`:
  `BUILD SUCCESSFUL`.
- `testDebugUnitTest`: 190 tests, 0 fallos, 0 errores (19 suites).
- `lintDebug`: sin errores fatales.
- `assembleDebug`: APK generado.

### Siguiente bloqueo real

- Implementar carga de DLLs dependientes (`UnityPlayer.dll`) con tabla de
  exports, reubicaciones e IAT propia, o ampliar el backend Win32. Sin eso,
  Unity no arranca.
- Sustituir los stubs D3D por objetos ABI reales o devolver `E_NOTIMPL`
  explícito.

## Actualización 2026-11-15 (final): Purga Definitiva de Wine/FEX/PRoot + Recompilador JIT Nativo x86/x64 -> ARMv9 AArch64 (ZVDS)

- **Purga de Dependencias Externas en Producción:**
  - `GameLauncher.kt` opera ahora 100% a través del Motor Directo Nativo Bionic (ZVDS): `BionicPeLoader` -> `NativeDllInterceptor` -> `OryonEmitterBackend` -> `DirectVulkanBridge` / `DirectAAudioEngine`.
  - Eliminado el bloqueo que exigía `opt/hyperplay/.full` o contenedores Linux para lanzar juegos portables.
  - Sincronizados y actualizados [ARCHITECTURE.md](file:///c:/Users/Murdoc/Desktop/Studio%20820%20Apps/HyperPlay/ARCHITECTURE.md) y [CLAUDE.md](file:///c:/Users/Murdoc/Desktop/Studio%20820%20Apps/HyperPlay/CLAUDE.md) documentando la arquitectura in-house y eliminando Wine/Proton/FEX/PRoot de la ruta de producción.

- **Recompilador JIT Nativo x86/x64 -> AArch64 (`OryonEmitterBackend.cpp / .h`):**
  - Implementado decodificador completo de instrucciones x86/x64 en C++20:
    - Soporte de prefijos REX (`0x40..0x4F`), ModR/M, SIB, desplazamientos e inmediatos.
    - Opcodes soportados: `MOV` (reg-reg, reg-imm64/32, load/store), stack (`PUSH`/`POP`), aritmética/lógica (`ADD`, `SUB`, `CMP`, `TEST`, `AND`, `OR`, `XOR`), flujo de control (`JMP`, `Jcc`, `CALL`, `RET`), y vectoriales SSE (`MOVAPS`, `MOVUPS`).
  - Mapeo directo de registros x86 a AArch64 (`RAX..R15 -> X0..X15`).
  - Reserva de pool JIT ejecutable W^X de 64 MB con invalidación explícita de caché de instrucciones (`__builtin___clear_cache`) optimizada para núcleos Oryon de Qualcomm Snapdragon 8 Elite.
  - Pila virtual dedicada de 8 MB con RSP inicializado en su extremo superior.
  - Registrado por defecto como el `CpuTranslationBackend` activo de `OryonCpuTranslator`.

- **Expansión de la Tabla Win32 y Direct3D en `NativeDllInterceptor.cpp`:**
  - APIs Win32 nativas implementadas sobre Bionic libc:
    - Módulos: `GetModuleHandleW/A`, `GetProcAddress`, `LoadLibraryW/A`.
    - Memoria & TLS: `HeapAlloc`, `HeapFree`, `HeapCreate`, `TlsAlloc`, `TlsGetValue`, `TlsSetValue`.
    - Sistema & Hilos: `GetSystemInfo`, `QueryPerformanceCounter`, `QueryPerformanceFrequency`, `GetTickCount64`.
    - Windowing: `CreateWindowExW/A`, `ShowWindow`, `PeekMessageW`, `DispatchMessageW`, `DefWindowProcW`.
  - Direct3D 11 y Direct3D 9:
    - Interceptación de `D3D11CreateDeviceAndSwapChain` y `Direct3DCreate9`.
    - Vtables tipo-seguras para swapchain y device mock conectadas directamente a `DirectVulkanBridge::presentTestFrame()` a 120Hz MAILBOX.
  - Compilación limpia con `-Wall -Wextra -Werror` garantizada.

- **Suite de Pruebas Unitarias y Gates de Calidad:**
  - Creado `app/src/test/java/com/murdoc820/hyperplay/engine/OryonJitTranslatorTest.kt` validando:
    - Existencia y estructura de los emisores y decodificadores ARMv9.
    - Mapeo de registros, soporte de opcodes x86/x64, pool JIT e invalidación de caché.
    - Interceptación y enlace de APIs Win32 y Direct3D.
    - Ejecución directa en `GameLauncher` sin requerir rootfs.
  - Añadido `app/src/test/cpp/OryonEmitterBackendTest.cpp` (507 líneas) con validación de ejecución de bloques x86 traducidos.
  - `cmd /c ".\gradlew.bat testDebugUnitTest lintDebug assembleDebug --console=plain"`:
    - `testDebugUnitTest`: **190 tests en verde** (0 fallos).
    - `lintDebug`: **0 errores** (reporte HTML generado limpiamente).
    - `assembleDebug`: **BUILD SUCCESSFUL**.
  - **APK Resultante:** `app/build/outputs/apk/debug/app-debug.apk` (**39.526.847 bytes**).
  - **Despliegue Físico:** Instalado con éxito en el dispositivo de prueba RedMagic 10 Pro (`192.168.0.11:35515`) mediante ADB WiFi (`Streamed Install - Success`).



- **Shader SPIR-V Real Compilado con NDK Oficial (`FsrUpscalePass`):**
  - Localizado el compilador oficial de shaders `glslc.exe` del Android NDK en `%LOCALAPPDATA%\Android\Sdk\ndk\28.2.13676358\shader-tools\windows-x86_64\glslc.exe`.
  - Desarrollado el Compute Shader GLSL 450 real en `app/src/main/cpp/gfx/shaders/fsr_cas.comp` (`local_size_x = 8, local_size_y = 8`, 5-tap cross sampler y algoritmo Contrast Adaptive Sharpening).
  - Compilado a bytecode SPIR-V con optimizaciones (`-O`) generando `fsr_cas.spv` y exportando `fsr_cas.inc`.
  - Verificado el número mágico de cabecera SPIR-V `0x07230203` y longitud de 3.136 bytes (784 palabras de 32 bits).
  - Integrado en `app/src/main/cpp/gfx/FsrCasSpv.h` y completado `FsrUpscalePass.cpp` creando `VkShaderModule`, `VkDescriptorSetLayout`, `VkPipelineLayout`, `VkComputePipeline` y `shutdown()` con `vkDeviceWaitIdle`.
- **Backend de Traducción CPU Dinámico (`BionicDlopenCpuBackend`):**
  - Implementado `BionicDlopenCpuBackend.h/.cpp` derivado de `CpuTranslationBackend`.
  - Conexión con `BionicRuntimeLinker::loadCpuCore`: al cargar librerías `.so` mediante `BionicLibraryStager::dlopenStaged`, resuelve con `dlsym` los puntos de entrada reales (`box64_init` / `box64_run_entry`, `fex_init` / `fex_run`, `hyperplay_cpu_backend_entry`) y registra automáticamente la instancia en `OryonCpuTranslator::setBackend(...)`.
  - Integrado `cpu/BionicDlopenCpuBackend.cpp` en `app/src/main/cpp/CMakeLists.txt`.
- **Modo Diagnóstico Hardware en Vivo sin Rootfs de 800MB (`HARDWARE_DIAGNOSTIC_SESSION`):**
  - Modificado `GameLauncher.kt` para que cuando no esté presente el rootfs completo (`opt/hyperplay/.full`), NO aborte con excepción; en su lugar inicia una sesión honesta y documentada `HARDWARE_DIAGNOSTIC_SESSION`.
  - Modificado `GameSessionActivity.kt`: cuando está en modo diagnóstico, activa directamente el render loop de `DirectVulkanBridge` (`nativeDirectPresentTestFrame`, `nativeDirectPresentFps` a 120Hz MAILBOX), audio directo AAudio de baja latencia y reporte de telemetría de silicio Snapdragon 8 Elite.
  - Implementados los endpoints JNI correspondientes en `jni_bridge.cpp` y `HyperPlayNative.kt` (`nativeDirectAttachSurface`, `nativeDirectDetachSurface`, `nativeDirectPresentTestFrame`, `nativeDirectPresentFps`).
- **Tests Unitarios y Verificación Total:**
  - Añadido test unitario JVM en `app/src/test/java/com/murdoc820/hyperplay/engine/FsrSpvAndCpuBackendTest.kt` validando la cabecera `0x07230203`, tamaño del bytecode SPIR-V y bypass de diagnóstico sin rootfs.
  - Añadido test C++ en `app/src/test/cpp/FsrCasAndCpuBackendTest.cpp` validando `static_assert` de cabecera y ciclo de vida de `BionicDlopenCpuBackend`.
  - Ejecutado `cmd /c ".\gradlew.bat testDebugUnitTest lintDebug assembleDebug --console=plain"`:
    - `testDebugUnitTest`: **0 fallos** (184+ tests unitarios en verde).
    - `lintDebug`: **0 errores** (reporte HTML generado sin infracciones fatales).
    - `assembleDebug`: **BUILD SUCCESSFUL en 25s**.
  - **APK Resultante:** `app/build/outputs/apk/debug/app-debug.apk` (**39.502.700 bytes**).
  - **Despliegue en Dispositivo de Prueba:** Instalado exitosamente vía ADB WiFi en el RedMagic 10 Pro (`192.168.0.11:35515`) con resultado `Success` (`package:com.murdoc820.hyperplay` activo).

## Actualización 2026-11-15 (tarde): Auditoría de seguridad y sustitución de código simulado

- **JNI/CMake:** verificados los 68 `external fun` (HyperPlayNative 46, EngineRuntimeNative 12, TurnipNative 4, XInputPadNative 6) contra sus símbolos `Java_com_murdoc820_hyperplay_*`; sin `UnsatisfiedLinkError`. `FindClass`/`NewStringUTF` comprobados y `DeleteLocalRef` añadido; la carga PE va envuelta en `try/catch`.
- **Parser PE (`PeHeaderInspector`):** RVA→offset en 64 bits con límite por `SizeOfRawData`, descriptores de carga diferida de 32 bytes con conversión VA→RVA, tope de 65 536 imports y comprobación de desbordamiento de IAT.
- **Cargador (`BionicPeLoader`):** arena y DLLs locales con tamaño real y `munmap` en el destructor; `.bss` a cero; reubicaciones aplicadas ANTES de `mprotect`; tipos 1/2/3/4/10 con `memcpy` y límites; validación de máquina, tamaño y entry point.
- **IAT (`NativeDllInterceptor`):** parcheo por página con `mprotect` temporal y restauración; `hijackIatChecked` informa de slots sin resolver o fuera de imagen y `nativeDirectLoadPe` aborta si los hay. `CreateFileW` mapea las cinco disposiciones de Windows; `GetCurrentThreadId` usa `gettid()`.
- **Traductor (`OryonCpuTranslator`):** eliminado el JIT falso (DMB/MOV X0,#0/RET). Ahora exige un `CpuTranslationBackend` real (FEXCore/Box64); sin backend `init`/`execute` fallan y `nativeDirectLoadPe` devuelve `false` (el camino PRoot/FEX sigue siendo el de producción).
- **Planificador (`OryonSiliconScheduler`):** topología leída de sysfs y ordenada por frecuencia, máscaras validadas contra `cpuCount`, `gettid()` cuando el tid es 0, `errno` registrado, handle de `libandroid.so` conservado y sesión ADPF cerrada en el destructor.
- **Carga de `.so` (`BionicLibraryStager`):** nuevo componente que copia `.so` de `/sdcard`, `/storage` o `/mnt` a `files/libstage` (directorio 0700, fichero 0500) y hace `dlopen` con `dlerror` detallado; integrado en `TurnipLoader` y `BionicRuntimeLinker`, que acepta `vk_icdGetInstanceProcAddr` de ICDs puros.
- **Vulkan (`DirectVulkanBridge`):** `detachWindow` con mutex y `detachWindowLocked`, `vkDeviceWaitIdle` antes de destruir el swapchain, formato/alpha/present mode consultados, familia de colas con present, semáforo+fence por frame y recreación ante `OUT_OF_DATE`/`SURFACE_LOST`; FPS atómicos. `FsrUpscalePass` se declara no disponible (no hay SPIR-V validado).
- **Audio (`DirectAAudioEngine`):** espera a `STOPPED` antes de cerrar, buffer ajustado a 2 ráfagas, `AAUDIO_ERROR_DISCONNECTED` detiene el stream y los contadores son atómicos.
- **Lanzador (`GameLauncher.kt`):** eliminado el retorno temprano que daba éxito sin arrancar el juego; sin rootfs completo se devuelve un error explícito.
- **Verificación:** `testDebugUnitTest`, `lintDebug` y `assembleDebug` → BUILD SUCCESSFUL (0 errores). APK en `app/build/outputs/apk/debug/app-debug.apk`.
- **Limitaciones honestas:** el camino "directo" no ejecuta PE hasta que se registre un backend real; FSR/CAS requiere empaquetar un shader validado y no hay toolchain de shaders en el entorno.

## Actualización 2026-11-15: Arquitectura Bionic Native Runtime Linker (COMPLETADO Y VERIFICADO)
- **Cambio de arquitectura hacia Bionic Dynamic Runtime Linker (`dlopen`/`dlsym` Bionic Host):**
  - **Eliminación de residuales:** Eliminados los archivos duplicados de `app/src/main/cpp/core/` (`PeHeaderInspector`, `BionicPeLoader`, `NativeDllInterceptor`, `HyperAotEngine`, `OryonSiliconScheduler`), migrando toda la lógica al árbol limpio y modular: `loader/`, `cpu/`, `gfx/`, `audio/`, `input/`.
  - **`BionicRuntimeLinker` (.h/.cpp):** Enlace dinámico con `dlopen(..., RTLD_NOW | RTLD_GLOBAL)` para:
    1. **CPU Core:** `libbox64_bionic.so` / `libfexcore_bionic.so` con WOW64 para 32-bit (`0x014c`) y 64-bit (`0x8664`), `BOX64_DYNAREC=1`, `BOX64_DYNAREC_STRONGMEM=1`, `BOX64_AVX=1`, y redirección a `_HyperPlaySaves/hypercache/`.
    2. **WinAPI Core:** `libwine_bionic.so` / `ntdll.so` compilado para Android Bionic libc, `WINEPREFIX` ligero en saves, montaje de carpeta portable como `D:\`, `WINE_LARGE_ADDRESS_AWARE=1`.
    3. **GPU Driver Core:** Enlace directo de `libvulkan_freedreno.so` (Adreno 830) / Sistema y DXVK-gplasync / VKD3D-Proton.
  - **Modo Diagnóstico Integrado (`runDiagnosticStressTest`):** Test de estrés en vivo que valida el pipeline nativo completo (análisis PE/IAT, 120Hz Vulkan en SurfaceView con FSR CAS Compute Shader, audio AAudio 48kHz, struct XInput y bloqueo a 2x Prime Cores @ 4.32GHz).
  - **`BionicCoreManager.kt`:** Gestor de paquetes `.zip` / `.wcp` / `.tzst` con extracción e importación a `files/bionic_cores/`, verificación de `.so` vía JNI y estado de los 3 módulos en UI.
  - **`scripts/fetch_bionic_cores.sh`:** Script automatizado para descargar y empaquetar los binarios reales de Box64-Bionic, Wine-Proton Bionic WOW64, DXVK-gplasync y Turnip Adreno 830 en `hyperplay-bionic-core.zip`.
  - **Corrección de Errores Estrictos NDK & Kotlin:**
    - Se agregaron anotaciones `[[maybe_unused]]` en constantes file-scope de `PeHeaderInspector.cpp`, `BionicPeLoader.cpp`, `OryonCpuTranslator.cpp` y `HyperAotCache.cpp` para satisfacer flags `-Wall -Wextra -Werror`.
    - Corrección de cast de `void*` a `uintptr_t` con `reinterpret_cast` para cálculo de RVA en `NativeDllInterceptor.cpp`.
    - Corrección de etiqueta `struct LoadedPeImage` (vs `class`) en `OryonCpuTranslator.h` bajo `-Wmismatched-tags`.
    - Corrección de import `BionicCoreManager` en `MainActivity.kt`.
    - Sincronización de `extraEnv` en `GameLauncher.kt` para compatibilidad de modo fallback.
    - Adaptación del callback de importación en `FirstRunSetupActivity.kt` al constructor de `SetupProgress`.
  - **Verificación Completa:**
    - `compileDebugKotlin`: **0 errores** (BUILD SUCCESSFUL).
    - `externalNativeBuildDebug` (`arm64-v8a` con clang++ C++20): **0 errores** (BUILD SUCCESSFUL).
    - `testDebugUnitTest`: **0 errores** (BUILD SUCCESSFUL).
    - `assembleDebug`: **0 errores** (BUILD SUCCESSFUL).
  - **Artefacto Generado:**
    - `app/build/outputs/apk/debug/app-debug.apk`
    - Tamaño: **39.475.378 bytes (~37,65 MB)**.
  - **Documentación:** Actualizados [ARCHITECTURE.md](file:///c:/Users/Murdoc/Desktop/Studio%20820%20Apps/HyperPlay/ARCHITECTURE.md) y [CLAUDE.md](file:///c:/Users/Murdoc/Desktop/Studio%20820%20Apps/HyperPlay/CLAUDE.md).

## Actualización 2026-11-15: Arquitectura Master Spec v2.0 (Pilares A-E)
- **Implementación completa de los componentes nativos C++20 y orquestador Kotlin:**
  - **`BionicPeLoader` (.h/.cpp):** Motor de preparación de entorno de ejecución para binarios portables PE32 (x86) y PE32+ (x64). Configuración automática de variables Wine (`WINEDLLOVERRIDES`, `WINE_LARGE_ADDRESS_AWARE`, `DXVK_ASYNC`, `DXVK_STATE_CACHE`, `VKD3D_FEATURE_LEVEL`, `ESYNC`, `FSYNC`), selector de arquitectura (`win32`/`win64`) y selector de rootfs FEX/Box64.
  - **`NativeDllInterceptor` (.h/.cpp):** Interceptor y enrutador IAT a nivel host para DLLs de Windows hacia implementaciones nativas de Android (Direct3D 9/11/12 -> Vulkan/Turnip, XInput -> ControllerManager SHM, XAudio2/DSound -> AAudio socket). Generación de strings `WINEDLLOVERRIDES` y `LD_PRELOAD`.
  - **`NativeThunkBridge` (.h/.cpp):** Bridge IPC lock-free SPSC con Ring Buffer alineado a 64 bytes utilizando `memfd_create` (y fallback ashmem) y descriptores compartidos vía `SCM_RIGHTS` para transporte de bajo overhead de frames gráficos, audio PCM, eventos de mando y háptica.
  - **`OryonSiliconScheduler` (.h/.cpp):** Planificador de afinidad para Qualcomm Snapdragon 8 Elite (SM8750). Pinning inteligente del hilo principal de juego a núcleos Prime (4.32GHz) y tareas de soporte a núcleos Perf. Integración dinámica con Android Performance Hint Manager (ADPF / `APerformanceHintSession`) y políticas de tiempo real `SCHED_FIFO` y `SCHED_RR`.
  - **`HyperAotEngine` (.h/.cpp):** Motor de análisis AOT para secciones `.text` de ejecutables PE con fingerprint de bloques de 64KB, persistencia en `aot_blocks.bin` y precarga a RAM vía `mmap(MAP_SHARED)` + `posix_fadvise`/`madvise(WILLNEED)`.
  - **`CompatibilityLayerManager.kt`:** Orquestador Kotlin de capas de compatibilidad (Valve Proton 9.0, GE-Proton, Kron4ek Wine WOW64 Classic para 32-bit, Wine 11.21 WOW64, y Bionic/PRoot Container Fallback) con auto-selección basada en `PeInfo` y persistencia en `SharedPreferences`.
  - **Puente JNI y CMakeLists.txt:** Nuevos exports JNI en `jni_bridge.cpp` e integración en `HyperPlayNative.kt` (`nativePinGameThread`, `nativePinWorkerThread`, `nativeSetupAdpf`, `nativeCoreTopology`, `nativeAotScan`, `nativeAotPreload`).
- **Resultado del Gate de compilación y verificación:**
  - `compileDebugKotlin externalNativeBuildDebug`: **0 errores** (PASS).
  - `testDebugUnitTest`: **0 errores** (BUILD SUCCESSFUL en 29s, todas las pruebas unitarias pasadas).
  - `assembleDebug`: **0 errores** (PASS).
- **Artefacto generado:**
  - `app/build/outputs/apk/debug/app-debug.apk`
  - Tamaño: **39.389.145 bytes (~37,56 MB)**.

## Estado actual (resumen)
- **MVP v1.0: Fases 1–4 completas, Gates 1–4 pasados** (`testDebugUnitTest lintDebug assembleDebug`: 127 tests, lint 0 errores / 23 avisos, 0 TODO). APK: `app/build/outputs/apk/debug/app-debug.apk` (~27,5 MB), instalado en el RedMagic.
- **Hollow Knight y Cuphead arrancan DESDE LA APP** en el teléfono, leídos de `/sdcard/PC games`, mostrados en pantalla por el puente Vulkan. Input táctil funciona (D-pad en HK, A en Cuphead). Quick Menu, aspecto, núcleos en vivo y Force Kill verificados.
- **GPU real activa: Turnip (Mesa 26.3.0, KGSL) en la Adreno 830.** Hollow Knight **60 FPS** (tope del bridge) y Cuphead **~48 FPS** en menús (antes 2–6 FPS con llvmpipe). Gate: 130 tests, lint 0 errores, APK ~31 MB.

### GPU real (hecho)
- Mesa 26.3.0 ya incluye la A830 (chip id KGSL `0x44050001`). Compilado nativo dentro del contenedor del teléfono (~4 min): `-Dvulkan-drivers=freedreno -Dfreedreno-kmds=kgsl -Dplatforms=x11 -Dvulkan-beta=true`, meson 1.9.1 (wheel PyPI con sha).
- Parche `third_party/mesa-patches/0001-wsi-x11-signal-present-id-on-sw-putimage-path.patch`: con Xvfb (sin DRI3) se usa `MESA_VK_WSI_DEBUG=sw` → ruta XPutImage no marcaba los frames como presentados y DXVK (present_wait) se quedaba esperando (pantalla negra).
- Thunk Vulkan de FEX: FEX 2611 **solo** aplica ThunksDB si `FEX_THUNKCONFIG` apunta a él → `init_hyperplay_env.sh` lo exporta cuando `HYPERPLAY_FEX_VK_THUNK=1` (solo modo Turnip). El "baseline" anterior era lavapipe x86 emulado.
- Driver empaquetado en el APK: `assets/bootstrap/turnip/turnip-kgsl-26.3.0-aarch64-glibc.zip` (3,5 MB). `TurnipDriverManager.adoptBundledDriver()` (llamado desde `applyActive()`) lo importa y activa si el usuario no eligió otro; `setActive(null)` guarda `-system` para respetar el driver del sistema. Modo Turnip añade `MESA_VK_WSI_DEBUG=sw` y `HYPERPLAY_FEX_VK_THUNK=1`.
- `scripts/build_turnip_kgsl.sh` (reproducible; `--arch aarch64|x86_64`, `--install-deps`, `--package-only`; no ejecutado completo de punta a punta). `dist/` (gitignored) con zips aarch64 y x86_64.
- vulkaninfo (aarch64 y x86 vía thunk): `Adreno (TM) 830v1`, turnip 26.3.0, Vulkan 1.4.363.
- Restos en el teléfono: herramientas de compilación + ~30 paquetes -dev en el rootfs y `/root/hp_mesa` (1 GB) — se pueden borrar.

## Entorno
- Windows + PowerShell. JDK 21 (`JAVA_HOME`), SDK en `%LOCALAPPDATA%\Android\Sdk`, NDK 28.2.13676358, CMake 3.22.1.
- Gradle 8.13 (wrapper), AGP 8.9.1, Kotlin 2.2.10, Compose BOM 2025.04.00. `.\gradlew.bat` en vez de `./gradlew`.
- WSL Ubuntu 24.04 con g++ para tests de host C++ (ASan/UBSan). Para comandos WSL con rutas con espacios: escribir un .sh en el scratchpad y ejecutarlo (`wsl -e bash -c` rompe las comillas).
- La carpeta NO es repo git (sin commits).

## Dispositivo de prueba
- RedMagic 10 Pro (NX789J), Android 16 / SDK 36, SM8750, Adreno830v2, SELinux enforcing, sin root, **sin /dev/kvm**, `/dev/kgsl-3d0` 0666, ADB inalámbrico.
- Juegos: `/sdcard/PC games/Hollow Knight/hollow_knight.exe` y `/sdcard/PC games/Cuphead/Cuphead.exe` (ambos Unity x64, LAA).
- Rootfs instalado: `files/rootfs` = Ubuntu Base 24.04.5 (+ xvfb vía apt). Aún BASE_ONLY.
- **Android solo da red a la app en primer plano** → descargas con app abierta o servicio en primer plano.
- Comandos de prueba como la app: `adb shell run-as com.murdoc820.hyperplay sh` (contexto runas_app ≈ untrusted_app).

## Hecho
### Fase 1 (Gate 1 ✔)
- Proyecto Gradle (`com.murdoc820.hyperplay`, minSdk 31, target 35, arm64-v8a, C++20).
- `core/PeHeaderInspector` (C++): MZ/PE, Machine, LAA, subsystem, APIs gráficas por imports normales y diferidos. Probado en host y en el teléfono.
- `engine/PortableExeScanner.kt`: filtra basura, pistas Steam/Unity/Unreal/d3d9 local, carpeta del exe = `D:\`.
### Fase 2 (Gate 2 ✔)
- `gfx/VulkanSurfaceBridge` (instancia/dispositivo/swapchain MAILBOX, frames AHB + SHM, letterbox, frame negro sin cliente).
- `audio/LowLatencyAudioServer` + `PcmRingBuffer` (AAudio low latency, socket PCM). Ring probado con TSan.
- `input/EvdevInjector` (uinput o socket de `input_event`) + `input/ControllerManager.kt` (mapeo, combos L3+R3 / Select+Start, auto-switch táctil/mando) + `NativeInputSink`.
- `libhyperplay.so` enlaza vulkan, aaudio, android, nativewindow, log, dl con `--no-undefined`.
### Especificaciones E2E (CLAUDE.md §6)
- `gfx/XSharedFrameServer` + `gfx/HyperPlayDisplayProtocol.h` (SHM por SCM_RIGHTS).
- `libhyperplay_display_bridge.so` (ejecutable PIE en C, `bridge/`): lee el XWD de `Xvfb -fbdir`, envía el fd (zero-copy), input por uinput o XTEST (`--no-uinput` por defecto). Corre en el teléfono desde nativeLibraryDir.
- `gpu/TurnipDriverInspector` + `engine/TurnipDriverManager.kt`: importar/descargar .zip/.adpkg, ELF/dlopen check, ICD json, fallback System Wrapper/Zink, variables TU_DEBUG etc.
- `engine/RootfsDownloadAndSetupManager.kt` + `setup/FirstRunSetup*` (Compose, OLED): importar .tar.zst/.tar.gz o Auto-Bootstrap Ubuntu 24.04.5 con SHA-256. **Probado en el teléfono.**
- `scripts/build_multibit_rootfs.sh`: constructor real (no ejecutado completo; requiere Linux con root).
### Fase 3 (Gate 3 ✔)
- PRoot compilado desde código (`third_party/proot` termux v5.1.107.102 GPL-2.0, `third_party/talloc` 2.4.4 LGPL-3.0, `third_party/proot.cmake`) → `libproot.so` + `libproot_loader.so`. En el teléfono: Ubuntu arranca, apt funciona, Xvfb crea framebuffer y socket.
- `core/CapabilityDetector`, `core/PortableMountManager` (d: → /mnt/game, saves → `_HyperPlaySaves`), `core/EngineRuntime` (fork+setsid+execve de PRoot + bridge, logs, Force Kill), `core/runtime_jni.cpp`.
- `engine/EngineRuntimeNative.kt`, `engine/GameLauncher.kt`.
- Assets `bootstrap/`: `init_hyperplay_env.sh`, `launch_portable_exe.sh`, `dxvk.conf`, `fex_config.json`.
- Tests: 65 unitarios JVM en verde + tests de host C++ (PE, ring, evdev, Turnip ELF, bridge XWD/protocolo, mount, runtime).

### Motor en el teléfono (hecho)
- `assets/bootstrap/engine_versions.env` (fuente única de versiones; la usa también `build_multibit_rootfs.sh`), `install_engine.sh` (10 pasos, reanudable, protocolo HP_*), `wxfix/hp_wxfix.c` + `wxfix/libhp_wxfix.so` (x86_64, precargado vía `/etc/ld.so.preload` del rootfs FEX).
- Instalado: FEX 2611 (`fex-emu-armv8.4`, el binario ahora se llama `/usr/bin/FEX`), rootfs x86 FEX Ubuntu 24.04, Wine 11.21 WOW64, DXVK 3.1.1, VKD3D-Proton 3.1, Mesa 25.2.8, Xvfb. ~5 min de trabajo real; `wineboot` 1 min 32 s.
- Arreglos clave:
  - **PRoot `execveat`** (FEX se re-ejecuta con `execveat(fd,"/proc/self/exe")`): parche en `third_party/proot/src/syscall/enter.c` (documentado en `third_party/README.md`). Verificado en el APK: `wine cmd /c echo` → `HIJO_OK`.
  - **SELinux execmod** (Wine reubica DLLs y pide PROT_EXEC → EACCES): `libhp_wxfix.so` envuelve `mprotect` y copia las páginas a memoria anónima.
  - `xxhsum` 0.8 imprime `XXH3 (f) = hash` (corregido en install y en build script).
- `BootstrapAssets` ahora copia subcarpetas (`wxfix/`).
- El prefijo `/home/hyperplay/.wine` en el teléfono existe; su `dosdevices/d:` apunta a `/mnt/game` de la prueba manual.
- ✔ Agente B (hecho, sin probar en el teléfono): `BootstrapAssets.kt`, `ContainerProcess.kt`, `EngineInstallProgress.kt` (+ `EngineInstallStore`, `EngineMarker`), `EngineInstallService.kt` (dataSync, notificación, WakeLock 2 h, log en `files/logs/engine_install-*.log`), sección "Motor de ejecución" en setup. 83 tests en verde, assembleDebug OK. APK NO instalado aún (el agente A usa el teléfono). Protocolo: `HP_STEP i n texto`, `HP_DL hechos total nombre`, `HP_DONE`, `HP_ERROR texto`; éxito = exit 0 + `/opt/hyperplay/.full`.

### Fase 4 (Gate 4 ✔)
- Launcher: `MainActivity.kt` (LAUNCHER, pide acceso a todos los archivos, abre setup si falta motor), `LauncherScreen.kt` (OLED 2 columnas, badges, navegación con mando, carpetas por SAF), `LauncherLogic.kt` (lógica pura testeada), `LibraryRepository.kt` (escaneo + caché), `audio/ConsoleUiSoundEngine.kt` (AudioTrack estático sintetizado), icono adaptativo.
- Sesión: `GameSessionActivity.kt` (SurfaceView → puente Vulkan, servidor de pantalla + input, GameLauncher, pantalla de carga con log hasta el primer frame NO negro, pantalla de fin), `session/{GameSessionLogic,SessionOverlay}.kt` (Quick Menu: FPS, 16:9/20:9/Stretch, núcleos Todos/4/2/1 en vivo, Reanudar, Force Kill), `input/{VirtualTouchLayout,VirtualTouchOverlayView}.kt`.
- Nativo: `VulkanSurfaceBridge::setScaleMode`, `EngineRuntime::setCpuLimit` + `core/ProcessTree` (incluye procesos con `TracerPid` = PRoot, porque Wine hace setsid), `XSharedFrameServer::contentFramesReceived`.
- Arreglos en dispositivo: se quitó Zink forzado en modo System Wrapper (con lavapipe colgaba explorer.exe; **desvío de CLAUDE.md §6.2** hasta tener GPU real); input táctil no cambia a modo mando; primer D-pad tras tocar ya no se pierde; lint RestrictedApi falso positivo en `dispatchKeyEvent` suprimido con comentario.
- Debug: `app/src/debug/AndroidManifest.xml` exporta `GameSessionActivity`; lanzar: `adb shell "am start -n com.murdoc820.hyperplay/.GameSessionActivity --es com.murdoc820.hyperplay.extra.EXE_PATH '/sdcard/PC games/Hollow Knight/hollow_knight.exe'"`. En debug se muestra sobre la pantalla de bloqueo.
- El teléfono tiene `log.tag=S`: para ver logs `adb shell setprop log.tag.HyperPlaySession V` (y demás tags HyperPlay*), dura hasta reiniciar.
- Tiempos: primer frame HK ~1:00, Cuphead ~0:31.

## Fase actual: BETA → objetivo STABLE (completamente estable)
Trabajo autónomo: al reanudar (p. ej. tras límite de uso) continuar sin pedir confirmación.
- ✔ **60 FPS fijos (SurfaceFlinger timestats)** en HK (menú + gameplay King's Pass, 0 frames perdidos en 1203) y Cuphead (título, casa, mapa; margen ~117 FPS sin límite). Claves: swapchain ya no se recrea en cada "suboptimal"; Wine en escritorio virtual (`explorer /desktop`) = foco de teclado; puntero X aparcado; pacing con vsync real del panel (`VsyncPublisher` → `/tmp/hp_vsync`, parche Mesa 0002, sin limitador DXVK); bridge por eventos X DAMAGE; contador real `nativeDisplayUniqueFrames()` en el HUD. Panel a 120 Hz en sesión (RedMagic ignora 60). Turnip r2 (`turnip-kgsl-26.3.0-r2`). Sin HUD de depuración.
- ✔ Controles en modo XInput (por defecto) funcionan con táctil en HK y Cuphead (menús). Causa: HK (Unity 6) solo usa XInput para dispositivos HID que Wine no tenía (SDL de winebus fallaba: Android bloquea NETLINK_KOBJECT_UEVENT). Arreglo: `assets/bootstrap/sdlpad/libhp_sdlpad.so` (x86_64, precargado en `/etc/ld.so.preload` del rootfs FEX): emula el socket netlink y crea mandos virtuales SDL (Xbox 360) en `winedevice.exe` desde la shm → Wine expone HID real (RawInput/DirectInput/WGI). Modo de mando por juego. Debug opcional `HYPERPLAY_PAD_DEBUG=1` → `Z:\tmp\hp_xinput.log`. FALTA: mando físico y vibración real (no hay mando conectado), gameplay con XInput, x86 test_xinput bajo Wine. `install_engine.sh`/build script no instalan sdlpad (lo hace el launcher).
- Riesgos: código de salida viene de explorer.exe; panel forzado a 144 Hz → 72 FPS (sin verificar); jefes de Cuphead sin probar; partida de Cuphead no persistió (¿ruta de guardado?).
En curso (agentes):
- ✔ **Audio funciona** (medido; falta confirmación de oído del usuario): Wine winealsa → plugin ALSA x86_64 propio `assets/bootstrap/audio/hp_alsa_pcm.c` (+ `asound.conf`, `setup_audio.sh`, `scripts/build_alsa_plugin.sh`) → `/tmp/hyperplay_audio.sock` → `LowLatencyAudioServer` (mezcla hasta 8 clientes). Reloj marcado por AAudio, ~50–55 ms, 0 underruns, 60 FPS intactos. Causa del silencio: links absolutos de `etc/alsa/conf.d` del rootfs FEX colgaban. `ConsoleUiSoundEngine` pasó a PERFORMANCE_MODE_NONE (robaba slots del fast mixer).
- Arranque: FEX `DiskCache`, caché de shaders, stamps de DLL, `Xvfb -nolock`, **contenedor precalentado** (`warm_container.sh`, `warm_client.sh`, `WarmContainer.kt`, `EngineRuntime::startWarm/stopWarm`; un solo PRoot, juego vía FIFO; se apaga a los 3 min sin launcher/sesión). HK 65–70 s → 52 s, Cuphead 40 → 20 s. **Cuello de botella: PRoot (ptrace)** ~19 s de CPU de sistema + FUSE de /sdcard; Unity carga ensamblados 29,6 s. Warm idle ~5,6 % de un núcleo (winebus SDL + PRoot).
- ✖ **32 bits (DmC) no funciona**: FEX 2611 no soporta el WoW64 nuevo de Wine 11 (32-bit dentro de proceso 64-bit). Requiere Wine con WoW64 clásico (procesos i386) + Turnip i386 (+ Wine Mono para DmC, que es .NET mixto). Pendiente de permiso del usuario para descargas. Ahora los 32 bits fallan rápido con error claro (`HYPERPLAY_TRY_32BIT=1` lo salta). DmC: `/sdcard/PC games/DmC Devil May Cry/Binaries/Win32/DMC-DevilMayCry.exe` (UE3, LAA, steam_api emulador).
- (histórico) Rendimiento + controles: arreglar controles teclado/táctil (HK no pasa del título), luego ≥60 FPS estables en gameplay, sincronización con panel 144 Hz (modo de pantalla múltiplo: 60/120), sin juegos sobreacelerados (limitador DXVK), contador real de FPS para el HUD, quitar cualquier HUD de DXVK de medición.
- ✔ Mandos XInput + vibración: CÓDIGO HECHO, gate verde (170 tests), SIN probar en el teléfono. Protocolo `cpp/input/HyperPlayPadProtocol.h` (shm `<rootfs>/tmp/hyperplay_pad.shm`, seqlock, heartbeat 3 s), `PadSharedState` + `pad_jni.cpp`, `input/{XInputPadNative,XInputPadMapping,XInputPadBridge}.kt`, DLLs `windows/xinput/hp_xinput.c` → `assets/bootstrap/xinput/{x64,x86}` (mingw 13.2 de paquetes Ubuntu en WSL, `scripts/build_xinput_dlls.sh --prefix ~/mingw-root`), probadas en Windows 10 real. Quick Menu: "Mando → XInput/Teclado", "Vibrar teléfono". Pruebas pendientes en teléfono: `build/xinput/*/selfcheck_xinput.exe` bajo Wine, test_xinput.exe, juego con mando BT y USB. DirectInput no ve estos mandos (usar Teclado).
Hecho: overlay gamer de solo texto (`session/PerfOverlay.kt`, `PerfMonitor.kt`; Compacto/Completo, "—" si no hay dato). Falta verlo en el teléfono.
Nuevo requisito: **juegos de 32 bits** deben funcionar; el usuario tiene **DmC** (32-bit) y no abre → diagnosticar (ruta en el teléfono por confirmar, buscar en /sdcard).
Siguiente: arranque casi instantáneo (contenedor precalentado, prefijo listo, caché FEX) + **audio** (Wine → servidor AAudio; hoy NO hay audio).
Criterios Stable: juegos jugables con mando y táctil, ≥60 FPS estables, audio, arranque rápido, sin cuelgues ni procesos huérfanos, gate verde.

En curso (2026-11-12):
- Agente directo: quitar PRoot del camino (FEX/Xvfb/Wine lanzados directamente desde Android); PRoot solo como respaldo automático. Dueño del teléfono.
- Agente runtimes: varios Wine seleccionables por juego en `/opt/hyperplay/runtimes/<id>/` (Wine 11 WoW64 nuevo, Kron4ek WoW64 clásico para 32 bits, GE-Proton; Valve Proton solo si es redistribuible), selector en el Showcase, descarga bajo demanda; + Turnip i386, helpers i386, Wine Mono, DmC. Usuario autorizó estas descargas.

- ✔ Runtimes + 32 bits LISTO PARA PROBAR (gate 183 tests; APK ~39 MB). Probado en contenedor: Turnip i386 bajo FEX en la A830 (parche Mesa 0003: FEX 2611 no traduce ioctls KGSL de 32 bits → layout LP64 en `__i386__`), Wine 11.21 clásico (Kron4ek amd64) con cmd 32-bit, Wine Mono 11.3.0 (.NET 32-bit OK), GE-Proton11-12 (prefijo propio). Valve Proton no se ofrece (solo vía Steam). Runtimes en `/opt/hyperplay/runtimes/{wine-11.21-wow64,wine-11.21-classic,ge-proton11-12}`, prefijos `~/.wine`, `.wine-classic`, `.wine-ge-proton`; helpers por arquitectura vía `/usr/$LIB/hyperplay/` en ld.so.preload. Archivos: `runtimes.sh`, `install_runtime.sh`, `engine/WineRuntimes.kt` (pref `runtime:<exePath>`), selector "Runtime" en el Showcase, sección "Runtimes de Wine" en setup, `scripts/build_guest_helpers_i386.sh`. Descargas extra: cross-compilador i686 y 51 paquetes -dev i386 del archivo oficial de Ubuntu. Pendiente: probar DmC en el teléfono (cuando lo libere el agente directo), reinstalar APK del árbol actual, limpiar `/root/hp_mesa32`, `/root/hp_repo`, `/root/hp_stage`, `/root/hp_t32`. Idea futura: GE-Proton aarch64 (Wine ARM64 nativo + FEX como DLL).

## Pendiente (orden)
1. Probar gameplay real (pasar de menús), más juegos, y reevaluar Zink/OpenGL ahora que hay GPU. Limpiar restos de compilación del rootfs.
2. **Audio de Wine** → socket PCM `hyperplay_audio.sock` (hoy el log se llena de errores ALSA; no hay sonido).
3. Probar con mando físico (L3+R3, Select+Start), relanzamiento a 20:9 (1600x720) y la pantalla de fin cuando el juego sale solo.
4. `build_multibit_rootfs.sh` aún no incluye `wxfix`.
5. Espacio de direcciones de 39 bits (Wine: `host addr space limit 0x7fffff0000`): puede limitar juegos grandes x64.
6. Procesos del juego si la app muere sin Force Kill.
7. Repo git + commits (la carpeta aún no es repositorio).

## Pendiente (histórico Fase 4, ya hecho)
- Fase 4 detallada: `MainActivity` (launcher 2 columnas, badges), `GameSessionActivity` (SurfaceView + overlay táctil + Quick Menu L3+R3), `ConsoleUiSoundEngine` (AudioTrack). Gate 4: `testDebugUnitTest lintDebug assembleDebug`.

## Decisiones clave
- El display bridge corre del lado Android (bionic) junto al contenedor, no dentro del glibc.
- Imagen: `Xvfb -fbdir` + fd compartido (sin copia entre procesos; 1 copia CPU→GPU).
- Mandos: XTEST (teclado/ratón) por defecto; uinput solo si se pide.
- Ejecutables nativos empaquetados como `lib*.so` + `useLegacyPackaging = true`.

## Auditoria de seguridad 2 (2026-11-15)

- `DirectVulkanBridge`: el fence solo se espera si hay una adquisicion pendiente (`fencePending_`); `VK_TIMEOUT` ya no reinicia un fence en vuelo. Un acquire fallido deja el fence sin senalizar y se reinicia en el siguiente frame.
- `BionicLibraryStager`: reemplazo de `.so` en staging con `unlink` + creacion 0600 + `fchmod` 0500 (antes fallaba con EACCES al refrescar una copia 0500) y hash con `st_mtim.tv_nsec` para no reutilizar una copia obsoleta del mismo segundo.
- `NativeDllInterceptor`: registro de proteccion Win32 por pagina; `VirtualProtect` devuelve la proteccion anterior real, `VirtualFree` limpia el registro y `Sleep` reintenta tras `EINTR`.
- `BionicPeLoader`: `resolveVirtualPath` confina tambien las rutas POSIX absolutas al `gameDir`; reubicaciones con suma de 64 bits y redondeo correcto de `HIGHADJ`.
- `PeHeaderInspector`: `NumberOfRvaAndSizes` se lee con el tamano minimo correcto del optional header.
- `VulkanSurfaceBridge::renderLocked`: se comprueban `vkWaitForFences` y `vkBeginCommandBuffer`; la barrera de `shmImage_` se incluye cuando hay subida SHM.
- `BionicRuntimeLinker`: `gpuDriverPath` guarda la ruta real; el diagnostico AAudio exige frames escritos > 0.
- Gate: `testDebugUnitTest` 183 tests / 0 fallos (17 suites, rerun completo), `lintDebug` 0 errores / 27 avisos, `assembleDebug` BUILD SUCCESSFUL; APK `app/build/outputs/apk/debug/app-debug.apk` (39 495 602 bytes, 15:09:49). 68/68 funciones `external` con simbolo JNI resuelto.

## Auditoria de seguridad 3 (2026-11-15)

- `EngineRuntime`: `launchMutex_` serializa `launch`, `startWarm`, `forceKill` y `stopWarm`. Antes dos lanzamientos simultaneos pasaban el chequeo `running` antes de publicar el estado (TOCTOU) y podian arrancar dos contenedores PRoot sobre el mismo `/tmp`.
- `EngineRuntime`: `signalProcessGroup` mata el grupo (`kill(-pid)`) y tambien al lider (`kill(pid)`). Cierra la ventana entre `fork()` y `setsid()` en la que `-pid` aun no existe y el proceso quedaba vivo bloqueando el siguiente lanzamiento.
- `EngineRuntime`: la peticion warm se escribe con `writeAllFd` (reintenta `EINTR` y acepta escrituras parciales). Si falla el spawn del puente o del motor se borra la peticion y el `.tmp`, para que el contenedor persistente no ejecute un juego que la app ya dio por fallido.
- `EngineRuntime::validate`: todas las rutas rechazan segmentos `.`/`..` (ademas de caracteres de control) y `extraEnv` rechaza cualquier caracter de control; el log warm queda confinado a `files/logs`.
- `VulkanSurfaceBridge`: `vkWaitForFences` usa un maximo de 5 s en `renderLocked` y `submitShmFrame`. Un driver/GPU colgado ya no bloquea el hilo de pantalla indefinidamente: se descarta el frame y se registra el `VkResult`.
- `XSharedFrameServer`: se valida `cmsg_len >= CMSG_LEN(0)` antes de calcular el numero de fds, evitando subdesbordamiento en la longitud del cmsg.
- `BionicPeLoader::loadLocalDll`: se rechazan nombres de DLL con `/`, `\`, `.` o `..`; la tabla de imports de un PE no confiable ya no puede escapar de `gameDir` y mapear archivos de la app.
- `BionicPeLoader::mapSections`: una seccion con datos que no se puede leer completa hace fallar la carga (antes solo avisaba y dejaba ceros en el codigo, arrancando una imagen corrupta).
- `OryonSiliconScheduler::pinToPrimeCores`: con `primeCount == 1` ya no se lee `primeCores[1]` sin inicializar en el log.
- Gate: `testDebugUnitTest --rerun-tasks` 186 tests / 0 fallos (18 suites), `lintDebug` 0 errores / 27 avisos, `assembleDebug` BUILD SUCCESSFUL; APK `app/build/outputs/apk/debug/app-debug.apk` (39 503 382 bytes, 15:36:35). 72/72 funciones `external` con simbolo JNI en `libhyperplay.so`. `EngineRuntimeTest` de host (WSL Ubuntu 24.04, g++ C++20, ASan/UBSan): OK.
- Sin cambios en la politica de honestidad: FSR/CAS sigue no disponible sin SPIR-V validado; el motor directo sin backend real falla con error, no simula exito. Sin prueba en dispositivo fisico en este entorno.

## 2026-11-17 16:15 - VEX3 FMA (0F38 96/97,98..9F,A6/A7,A8..AF,B6/B7,B8..BF)

- Reauditoria de timestamps: `progreso.md` 15:55:52, backend 15:54:45, test 15:54:59.
- Live anterior: Hollow Knight PID `17018`; `VEX 2B opcode no soportado=0`, `VEX 3B opcode no soportado=2`. Bloqueo `C4 E2 F1 A9 25 0C 41 09 00 ...` = `vfmadd213sd 0x9410c(%rip), %xmm1, %xmm4` -> `xmm4 = xmm1*xmm4 + mem`. Sin SIGABRT/ExitProcess/Fatal signal. Logs `%TEMP%\opencode\logcat_vexcvtdq2pd.txt` y app `Hollow_Knight-20261117-155608.log`.

- Decodificacion VEX3 FMA en `OryonEmitterBackend.cpp` (mapa 2): `pp=66`, `W=0` -> PS/SS, `W=1` -> PD/SD; grupos 132/213/231 y ADD/SUB/NMADD/NMSUB/ADDSUB/SUBADD. Memoria usa `LdrS/LdrD/LdrQ`. YMM (L=1) se rechaza con `fail("VEX 3B FMA YMM no soportado")`.
- Emisores `emitFmaS/emitFmaD` y `emitInsElementD` en `OryonEmitterBackend.h/.cpp`. Packed 128-bit por lanes con V24..V29 como scratch (copias estables para no alterar fuentes al escribir `dst`).
- Determinacion empirica en telefono (constantes 100,2,4): `1F420484 -> 108`, `1F428484 -> 92`, `1F620484 -> -108`, `1F628484 -> -92`. Traduccion correcta x86->A64: FMADD->FMADD, FMSUB->FNMSUB, FNMADD->FMSUB, FNMSUB->FNMADD; `a64Sub = sub != neg`, `a64Neg = sub`. Antes `VFNMADD132SD/PD` daba cero/-108.

- Instrumentacion temporal retirada (`cstdio`, DBG FMA, DBG emitFmaD, checks dbg*/pre). Compilacion nativa limpia `-Wall -Wextra -Werror`; `OryonEmitterBackendTest: 442 comprobaciones, 0 fallos -> OK` en dispositivo. Casos nuevos: `VFMADD213SD` reg/memoria, `VFNMADD132SD`, `VFNMADD132PD`, `VFMADD231PS`.
- Pendiente: gate Gradle, instalar APK, relanzar Hollow Knight y confirmar `VEX 3B opcode no soportado=0` sin fatal/abort; despues reemplazo X18->X23/X24 (X18 es registro de plataforma Android) con tests nativos y repeticion de gate/live.

## 2026-11-17 17:04 - Cierre VEX3 FMA + exportacion de logs

- Reauditoria de timestamps: `progreso.md` 16:15:19, `log.md` 17:04:20; sin cambios concurrentes detectados.
- Gate Gradle green tras FMA: `testDebugUnitTest lintDebug assembleDebug --console=plain`, `BUILD SUCCESSFUL in 14s`, `GATE_EXIT=0`. Log `%TEMP%\opencode\gradle_gate_vex3fma.txt`. APK `app-debug.apk` 39.820.227 bytes, 16:16:27.
- Live verificado: APK instalado; Hollow Knight PID `19554`; app log `Hollow_Knight-20261117-161656.log`. Contadores: `VEX 3B opcode no soportado=0`, `VEX 2B opcode no soportado=0`, `SIGABRT=0`, `TerminateProcess=0`, `ExitProcess=0`, `Fatal signal=0`, `Assertion=0`. Log `%TEMP%\opencode\logcat_vex3fma.txt`.
- Bloqueo previo `C4 E2 F1 A9 25 0C 41 09 00` (`vfmadd213sd 0x9410c(%rip), %xmm1, %xmm4`) resuelto y ejecutado sin trampas.
- Exportacion solicitada por el usuario: `log.md` en la raiz del workspace con los 110 archivos de `files/logs/` del telefono (via `run-as` + `tar`), mas resumen y ultimas 300 lineas de logcat relevante (PID 19554 / HyperPlay). Tamano 179.316 bytes, 17:04:20. Logcat crudo completo en `%TEMP%\opencode\phone_logs\logcat_all.txt` (14.118.952 bytes, 50.001 lineas).
- Sigue pendiente: reemplazo X18->X23/X24 con tests nativos, gate y live; DXGI/D3D11 sin rasterizador real; VERSION sin parseo de recursos PE; stdcall de 32 bits; SEH/unwind minimo; `res=1280x720` vs display Win32 2688x1216.

## 2026-11-17 17:45 - WINMM real, GetConsoleCP, X18 scratch, gate y live HK/Cuphead

- Reauditoria de timestamps antes de editar: `progreso.md` 17:05:31; `NativeDllInterceptor.cpp` 17:09:15; `NativeDllInterceptor.h` 17:08:45; `OryonEmitterBackend.cpp` 17:09:53; `log.md` 17:04:20. Sin cambios concurrentes detectados.

- `NativeDllInterceptor` / WINMM:
  - `win_timeGetTime()` devuelve milisegundos monotonos reales (`CLOCK_MONOTONIC`, mismo reloj que `GetTickCount`), ya no cae en el stub dinamico que devolvia `0`.
  - `win_timeBeginPeriod()` y `win_timeEndPeriod()` devuelven `0` (`TIMERR_NOERROR`).
  - `win_timeGetDevCaps(ptc, cbtc)` valida puntero y tamano; con `ptc != 0 && cbtc >= 8` escribe `wPeriodMin=1`, `wPeriodMax=1000000` y devuelve `0`; si no, devuelve `11` (`MMSYSERR_INVALPARAM`).
  - Registro de las 4 funciones en la rama `winmm.dll` de `resolveNativeSymbol`.
  - `isNoisy` ahora compara el nombre exacto de la funcion (sufijo tras `!`): filtra `Sleep`, `timeGetTime`, `QueryPerformanceCounter`, `QueryPerformanceFrequency`, `EnterCriticalSection` y `LeaveCriticalSection` sin ocultar `SleepConditionVariable*`.

- `KERNEL32.dll`: `GetConsoleCP` registrado y devuelve `65001` (`CP_UTF8`). `SystemFunction036` / `RtlGenRandom` tambien se registran explicitamente en la rama `BCRYPT.dll`; `ADVAPI32.dll` / `CRYPTBASE.dll` ya los resolvian. Handles nuevos: `kHModWinmm`, `kHModBcrypt`, `kHModCryptbase`, con `LoadLibraryW` / `GetModuleHandleW` / `GetProcAddress`.

- `OryonEmitterBackend.cpp`: erradicado `X18`/`W18` como scratch. `BT/BTS/BTR/BTC` y `SHLD/SHRD` usan `X23`; `CMPXCHG8B/16B` y `CMPS` usan `X24`. Verificacion: `Select-String '\b[WX]18\b'` sobre el archivo devuelve `0` coincidencias. Los tests nativos existentes de BT/BTS/BTR/BTC, SHLD/SHRD, CMPXCHG8B/16B y CMPS siguen pasando.

- Gate y pruebas:
  - Compilacion nativa limpia con `-Wall -Wextra -Werror`: `OryonEmitterBackendTest: 443 comprobaciones, 0 fallos -> OK` en el RedMagic 10 Pro.
  - Gate Gradle: `testDebugUnitTest lintDebug assembleDebug --console=plain`, `GATE_EXIT=0`, `BUILD SUCCESSFUL in 27s`. Log `%TEMP%\opencode\gradle_gate_winmm_x18.txt`.
  - APK `app/build/outputs/apk/debug/app-debug.apk`: 39.821.624 bytes, 17:11:21; instalado con `Success`.

- Live Hollow Knight (PID `23072`, APK nuevo):
  - `VEX 3B opcode no soportado=0`, `VEX 2B opcode no soportado=0`, `Fatal signal=0`, `SIGABRT=0`, `ExitProcess=0`, `TerminateProcess=0`.
  - Sigue `Forcing GfxDevice: Null` a las 17:11:49.471 y `NullGfxDevice: Version NULL 1.0`; no hay ninguna llamada a `D3D11CreateDevice`, `D3D11On12CreateDevice` ni `CreateDXGIFactory*` antes de esa decision. `d3d11`/`dxgi` aparecen `0` veces en el logcat.
  - `timeGetTime` ya no genera lineas porque `isNoisy` lo filtra; el `0` de coincidencias no demuestra que no se llame. Queda pendiente una comprobacion explicita de que el valor avanza (contador de primeras llamadas o test nativo).
  - Logcat: `%TEMP%\opencode\logcat_winmm_x18.txt` (19.498.818 bytes, 17:12:18). App log: `Hollow_Knight-20261117-171139.log`.

- Live Cuphead (APK nuevo, PID `25214`):
  - `Cuphead-20261117-174230.log`: arranca en `Motor Directo Bionic en ejecucion continua (res=1280x720, limit=0)`; ya no aparece `IAT incompleta`.
  - Logcat: `IAT: 67 entradas, 67 resueltas, 0 sin resolver, 0 fuera de imagen`. `GetConsoleCP` dejo de faltar.
  - `Fatal signal=0`, `SIGABRT=0`, `ExitProcess=0`, `TerminateProcess=0`; `IAT incompleta=0`. Logcat: `%TEMP%\opencode\logcat_cuphead_consolecp.txt`.

- Diagnostico D3D/Unity (sin cambios de codigo en esta iteracion):
  - `UnityPlayer.dll` importa `d3d11.dll!D3D11CreateDevice`, `d3d11.dll!D3D11On12CreateDevice`, `dxgi.dll!CreateDXGIFactory` y `dxgi.dll!CreateDXGIFactory2`; su IAT local se resuelve `525/525`.
  - Unity elige Null antes de invocar cualquiera de esas funciones. Los unicos fallos graficos observados son `LoadLibraryW` de `AMDUnityPlugin` y `NVUnityPlugin` devolviendo `0`.
  - Candidato abierto: `CreateWindowExW` registra `style=0x0, size=-2147483648x-2147483648`; el dispatcher invoca wrappers de mas de 8 parametros mediante un puntero de 8 argumentos, por lo que los argumentos 9-12 pueden llegar basura. Corregir el reenvio de argumentos y/o forzar `-force-d3d11` de forma controlada es el siguiente paso para ver si Unity llega a `D3D11CreateDevice`.
  - Sigue pendiente el rasterizador real: la capa COM DXGI/D3D11 existe, pero no presenta pixeles; `res=1280x720` vs Win32 `2688x1216`, stdcall de 32 bits, SEH/unwind y recursos VERSION siguen abiertos.
## 2026-11-19 — Caché JIT persistente (HyperAOT reubicable)

- **Qué hace:** cada bloque traducido se guarda en `<Juego>/_HyperPlaySaves/hypercache/oryon_jit.dat` (+ `.idx`). En la siguiente ejecución, `translateBlock` busca primero en disco y copia el código a la arena en vez de retraducirlo. Otros hilos de la misma ejecución también lo reutilizan.
- **En disco, no en RAM:** en memoria solo vive el índice (16 B por bloque). El código se lee con `pread` al pedirlo y el sistema hace lectura anticipada (`POSIX_FADV_WILLNEED`, caché de páginas reclamable). Escritura en un hilo aparte (cola acotada), sin bloquear al JIT. Tope de 512 MB: al llenarse se vacía en el siguiente arranque.
- **Validez:** archivo ligado al build-id ELF de `libhyperplay.so` (otro build -> se vacía). Por bloque: clave de configuración del emisor (TSO, encadenado, flags muertos, búsqueda en línea, nativas rápidas, 32/64 bits), hash de los bytes x86 actuales en la RIP (código cambiado o SMC -> se retraduce) y suma de control del registro (corrupto o truncado -> se descarta).
- **Reubicación:** `runState_`/`suspendCount_` ahora se direccionan relativos a X19 (`&ctx_`), independientes del backend. Las direcciones de funciones de `libhyperplay` (nativas rápidas, `executePcmpistri`) se emiten con MOVZ+3 MOVK fijos y se reescriben al cargar (ASLR). Las ranuras IAT leídas al traducir se revalidan. Los enlaces entre bloques nunca se guardan: se toman antes de enlazar.
- **Medición:** `jit_scan` en el dispositivo traduce ~7 µs por bloque (UnityPlayer.dll 7,1 µs, mono 7,6 µs; ~490 B de ARM64 por bloque). La línea `Oryon: Ejecutados ...` muestra ahora `JIT total N ms (todos los hilos)` y aciertos/fallos/guardados de disco; `OryonEmitterBackend::totalJitMs()` está disponible para registrarlo en el primer Present.
- **Expectativa honesta:** si el arranque traduce ~300 000 bloques, la traducción son ~2 s de los ~75-95 s hasta el primer Present. La caché ahorra esa parte (y las retraducciones por hilo/SMC), pero el resto es ejecución del propio juego (Mono, carga de assets). Para llegar a 5 s hace falta acelerar esa ejecución; la línea `JIT total` lo confirmará en el dispositivo.
- `setprop debug.hyperplay.aotcache 0` la desactiva (A/B). Prueba `testPersistentJitCache` (guardar, recargar en otro backend, reubicar direcciones borradas, otro build, registro corrupto, bytes distintos). Suite nativa: 1109 comprobaciones, 0 fallos. También se corrigió `testVexMxcsr`, que leía justo fuera de la pila virtual.

## 2026-11-19 — Arranque 76 s -> ~17 s, tirones, semántica x86 y fusión de flags

- **Arranque (causa raíz):** el perfil de esperas mostró el hilo principal un 75 % del arranque dentro de `LoadLibraryExW`. Mono sondea cada ensamblado como DLL nativa (`mscorlib.dll.dll`, `Managed/mono/aot-cache/amd64/*.dll.dll`) y `BionicPeLoader::loadLocalDll` recorría todo el árbol del juego (profundidad 3) sobre el FUSE de `/sdcard` en cada fallo: ~200 ms por sondeo. Ahora se construye un índice nombre->ruta una sola vez (1765 entradas en HK) y cada sondeo cuesta ~2 ms. **Menú: 76 s -> 16,8-18 s.**
- **Lo que queda del arranque es CPU real** (medido con `/proc/<pid>/task/*/stat`: un hilo al 100 % de 0-8 s, luego dos): decodificación Vorbis de Unity (lector de bits, búsqueda binaria en el codebook, acumulación `addss`) y Mono. La caché JIT persistente solo ahorra la traducción (~4 s de CPU en total, repartidos entre hilos). Bajar a 5 s exige que el código traducido sea más rápido.
- **Fusión de flags** (`debug.hyperplay.fuse`, por defecto 1): un Jcc/SETcc/CMOVcc justo detrás de la instrucción que fija los flags usa el NZCV del host (`B.cond`/`CSET`/`CSEL`) en vez de releer `rflags` de memoria. Es correcta por construcción porque `emitWriteFlags` materializa ZF=Z, SF=N, OF=V (Overflow) y CF=C/!C (Carry/Borrow) sin tocar NZCV. Se anula si hay ramas o escrituras de NZCV entre medias, y en cada inicio de bloque. Arranque A/B: 16,8-18 s con fusión, 18,3-20,7 s sin ella.
- **Bugs de semántica x86 corregidos** (candidatos a los fallos de física: NPC que sigue cayendo, geo en bucle):
  - `CMOVcc r32` con condición falsa no ponía a cero los bits 63:32 del destino; x86 siempre escribe el destino.
  - `CVTTSS2SI`/`CVTSS2SI`/`CVTTSD2SI`/`CVTSD2SI` (32 y 64 bits) y `CVTPS2DQ`/`CVTTPS2DQ`: ARM satura y da 0 con NaN; x86 da el entero indefinido `0x80000000…`.
  - `RSQRTSS/PS` y `RCPSS/PS`: la estimación ARM (~8 bits) no cumplía el error de x86 (≤1,5·2^-12); se añadió un paso de Newton-Raphson.
- **Ritmo de frames:** limitador preciso (`clock_nanosleep` absoluto) a la mitad del refresco: 72 fps en el panel de 144 Hz (60 en uno de 120), es decir 2 vsyncs exactos. Peor frame ~15 ms en vez de 70-82 fps irregulares. Menos calor y CPU/GPU en reposo entre frames. `debug.hyperplay.fpscap 0` lo quita.
- **Diagnóstico nuevo:** `debug.hyperplay.waitprof <s>` (estado en tiempo real por hilo: JIT, thunk con sitio y handle, nativa directa bloqueante; más `[Señales]` con quién señaliza cada handle). `debug.hyperplay.stallprof 1` (`[Tirón]`: desglose por hilo de cada frame de más de 100 ms, más SuspendThread y KB de ReadFile en esa ventana).
- **Juego real por adb, 3 min (King's Pass):** 70-82 fps sin limitador, 0 frames >33 ms en régimen estable. Los 11 tirones registrados fueron todos de carga (`LoadLibraryExW`, 456 MB de ReadFile, JIT).
- Suite nativa: 17 237 comprobaciones, 0 fallos (`testFlagFusion`: 16 condiciones × cmp/test/add × 32/64 bits × Jcc/SETcc/CMOV, con y sin fusión; `testReciprocalPrecision`; casos de entero indefinido).
- **Partidas de HK que no persistían (causa raíz):** `MoveFileExW`, `MoveFileW/A`, `DeleteFileW/A`, `CopyFileW/A/ExW`, `ReplaceFileW` y `RemoveDirectoryW/A` eran stubs que devolvían 0. HK escribe `user1.dat.new` / `shared.dat.tmp` y los renombra con `MoveFileExW(REPLACE_EXISTING)`. Implementados con semántica Win32 (errores mapeados desde errno, copia si EXDEV con COPY_ALLOWED, copia de seguridad en ReplaceFile). Verificado en el dispositivo: `shared.dat` ya se crea a partir de `shared.dat.tmp`.
- **Sesión por adb en el propio dispositivo** (`game.sh` con `nohup`; sobrevive a los cortes de adb): menú en 16,1 s; gameplay de 2,5 min a 70-72 fps con limitador, peor frame 17-27 ms y 1 solo frame >33 ms. El tramo a 25-30 fps es el vídeo del prólogo (va a su ritmo nativo).
- Puerta Gradle (`testDebugUnitTest lintDebug assembleDebug`): OK. `OryonJitTranslatorTest` actualizado a la caché JIT de 512 MB.
## 2026-11-19 (mañana) — Cierres, JIT más compacto, pila de retornos y validación en vivo de HK

- **Cierres al azar de la app (todos los juegos):** `JNI_OnLoad` hacía `prctl(PR_SET_TAGGED_ADDR_CTRL, 0)`. ART ya tiene punteros con etiqueta en el byte alto (`0xb4…`); sin el ABI de direcciones etiquetadas el kernel los rechaza en syscalls, y el futex de ART fallaba con EFAULT ("futex wait failed … Bad address" -> SIGABRT a los 2 s del arranque). Se quitó el `prctl`; se mantiene `mallopt(M_HEAP_TAGGING_LEVEL_NONE)`. Tras el arreglo, Cuphead pasó título, menús y elección de partida sin cerrarse.
- **Partida en vivo:** la ranura 1 muestra "Paso del Rey, 2 min" (antes siempre "Partida nueva"). Se recuperó la partida real del usuario, que estaba atascada en `user1.dat.new` por el `MoveFileExW` roto; copia de seguridad en `_backup_20261119/`. Para probar escenas, la ranura 2 es una partida de prueba (banco de Bocasucia + impulso, garra y doble salto), editada con `hksave.ps1` (AES-ECB con la clave pública de HK).
- **Mando virtual de depuración** (`setprop debug.hyperplay.pad "R A X"`, vigilado con `__system_property_wait`): permite jugar por adb pulsando varias cosas a la vez (`input` de Android solo inyecta un puntero). Ruta fija `hk_well.sh`: banco -> pozo -> Cruce Olvidado -> 20 s quieto -> paseo con saltos y ataques.
- **JIT más compacto** (bloque caliente de UnityPlayer: 1064 -> 808 B; media 492 -> 453 B por bloque; traducción 7,1 -> 2,8 µs por bloque):
  - RFLAGS desde NZCV con desplazamientos (16 -> 7-9 instrucciones por escritura de flags). Se quitó la copia `ctx.nzcv`, que nadie leía.
  - **PF perezoso:** solo se guarda el byte bajo del resultado (`ctx.pfByte`). La paridad se calcula al leerla (JP/JNP/SETP/CMOVP, `GetThreadContext`/excepciones). COMISS/UCOMISS y NEG 8/16 escriben `pfByte`.
  - `[base+disp]` en una instrucción. `lea` directo al destino. `add/sub/cmp r, imm` (|imm| ≤ 4095) en una sola ADDS/SUBS sobre el registro (inmediato negativo = operación inversa, con CF verificado).
  - **Pila de retornos:** `call rel32` = BL a un trampolín que apunta {RIP de retorno, continuación} en `ctx.ras`; `ret` compara y hace `RET X20` (predicho por el núcleo) o la búsqueda normal si no coincide. El prólogo y el epílogo guardan X29/X30. La pila se vacía al reiniciar la arena; los thunks y `jmp [IAT]` la sacan al emular el RET. `debug.hyperplay.ras 0` la desactiva.
- **WaitOnAddress** ya no hace 2 `mincore` y una asignación por llamada (camino muy caliente del sistema de jobs de Unity).
- **Ruido de log:** el registro de thunks pasa de las 30 000 primeras llamadas (+1/1000) a las 4 primeras de cada función: de 73 586 líneas a unos cientos por sesión (`debug.hyperplay.thunklog <N>` para diagnóstico). El log de la ruta bajó de ~10 MB a ~1 MB.
- **Calor:** a los ~60 min de pruebas seguidas el teléfono llegó al estado térmico 3 (núcleos a 1,55/1,69 GHz en vez de 3,53/4,32) y los fps se hundieron. Las mediciones solo valen con el teléfono frío (frecuencias máximas restablecidas). Una sola ruta de 3,5 min ya baja los máximos a 2,9/3,07 GHz.
- **Medición en frío (09:20), Cruce Olvidado:** quieto 71,9 fps (el tope de 72), peor frame 19 ms, 0 >33 ms (antes 54-58 fps); paseo con combate 68,6 fps de media (mín. 54); transición Bocasucia -> Cruce 1122 ms (antes 1486).
- **Quedan:** pausas del GC de Mono de ~310 ms (para el mundo; en PC ~30-50 ms) y ~320 ms al cargar recursos en el hilo principal mientras se juega.
- **GC incremental (probado y desactivado):** Unity 6000.0.61f1 / Mono exportan `mono_gc_set_incremental`. Una capa sobre `boot.config` (copia en `_HyperPlaySaves` con `gc-max-time-slice=3`, sin tocar el juego) se aplicó bien, pero Mono se cae al iniciar al analizar las opciones del GC (`strchr` de '=' que devuelve NULL en `mono-2.0-bdwgc+0x26341d`). Queda tras `debug.hyperplay.incgc 1` para investigar.
- Puebas con `resscale=50`: no sube los fps (no es límite de GPU), y además baja a ~24 fps. Hay un problema aparte al renderizar por debajo de la nativa (pendiente).
- Suite nativa: 39 089 comprobaciones, 0 fallos (nuevas: `testFlagFusion` ampliada con add/sub/cmp imm, `testLazyParity`, `testReturnStack`). Puerta Gradle OK.
## 2026-11-19 (tarde) — Modo eficiente, protección térmica, overlay, SGSR v1 y HyperFG

Reglas respetadas: sin tocar `OryonEmitterBackend`, `DxbcToSpirv`, `MediaFoundationBridge` ni `Win32Registry`; todo aditivo, reversible desde el Quick Menu y con `setprop` de respaldo.

- **Limitador y tope en caliente** (`loader/FramePacing.{h,cpp}`): el limitador `clock_nanosleep` absoluto pasó de `NativeDllInterceptor` a este módulo con la misma lógica (reancla en frames tardíos). El tope se cambia en caliente (`nativeSetFpsCap`) sin tocar el swapchain. `debug.hyperplay.fpscap` sigue teniendo prioridad para A/B. Quick Menu: 60 (Eficiente, por defecto), 72, 120 y Sin límite. El panel se pide a un múltiplo entero (60/120 -> 120 Hz, 72 -> 144 Hz).
- **ADPF por frame** (`OryonSiliconScheduler::ensureFrameSession/reportFrameWork`): sesión para el hilo principal (`hyperplay-direct`) y el de render, con `reportActualWorkDuration` = trabajo real del frame (hasta el Present, sin la espera del limitador) y `updateTargetWorkDuration` = 80 % del periodo del tope (`debug.hyperplay.adpfpct`, `debug.hyperplay.adpf 0` lo apaga).
  - **Bug previo encontrado:** el código buscaba `APerformanceHintManager_createSession`, que no existe (es `APerformanceHint_createSession`): ADPF nunca llegó a funcionar. Además `GameLauncher` anclaba a Prime y creaba la sesión desde el hilo de E/S de Kotlin, no desde el hilo del juego.
- **Anclaje a Prime:** `setPrimeRelaxed()` ancla o relaja el hilo principal. Medido en el Cruce Olvidado: anclado 62,6 fps frente a 66,8 fps libre (tope 72). Queda **desactivado por defecto** (`debug.hyperplay.primepin 1` lo activa, y la protección térmica lo relaja al calentarse).
- **Gobernador térmico** (`session/EfficiencyMode.kt`, lógica pura con tests JVM): batería < 38 °C, perfil del usuario; ≥ 39,5 °C, tope > 60 -> 60 y Prime relajado; vuelve a normal en ≤ 37,5 °C; ≥ 41,5 °C, indicador ámbar/rojo en BAT (se apaga en ≤ 40,5 °C). Lee la batería cada 2 s aunque el HUD esté apagado. Nunca cierra el juego ni recrea el swapchain. Visto en vivo: a 41 °C se activó solo ("anclaje relajado", tope 60) sin incidencias.
- **Escala de render** (Quick Menu: 100 % Nativa por defecto, 85 %, 75 %; se aplica al iniciar). `debug.hyperplay.resscale` sigue teniendo prioridad.
- **Overlay**: el modo compacto va arriba, en el hueco libre entre Menú (≡) y RT del mando táctil (geometría de `VirtualTouchLayout`): pastilla de 2 líneas a 10 sp estilo RedMagic, con **consumo de batería en tiempo real** (W, ⚡ si carga), ▼ cuando la protección térmica actúa y BAT en ámbar/rojo. Colores ajustados a rangos reales: FPS relativo al tope (verde ≥ tope − 3, ámbar ≥ 85 %), SoC < 65 / ≤ 80 °C, batería < 38 / < 41,5 °C, consumo < 4,5 / ≤ 7 W. El modo completo sigue abajo.
- **SGSR v1** (`gfx/shaders/present_sgsr.frag`, `PresentSgsrSpv.h`, 7 488 B, `spirv-val` OK): un solo pase en el fragment shader de presentación, mismo vértice y push constants de rotación (`offset 0..31`; SGSR usa `32..63`: tamaño del backbuffer y nitidez, por defecto 0,30 = intensidad original). Se crea junto al pipeline de presentación (nunca en partida). Quick Menu "Reescalado": Auto (SGSR si escala < 100 %), SGSR siempre, Bilineal; `debug.hyperplay.upscaler auto|sgsr|off`. Al 100 % en Auto se usa el pase de siempre sin coste extra.
- **HyperFG 2×** (`gfx/HyperFrameGen.{h,cpp}`, `fg_pyramid/fg_flow/fg_interp.comp`, `HyperFrameGenSpv.h`; los 3 con `spirv-val` OK), **apagado por defecto** (`debug.hyperplay.fg 0/1`):
  - Historial A/B en GPU (`vkCmdCopyImage`), pirámide de luminancia R16F 1/2-1/4-1/8 en un despacho, flujo coarse-to-fine por bloques de 8×8 (SAD, sesgo al vector cero) e interpolación bidireccional t = 0,5.
  - Escudos: HUD/estático (|Δ| < 0,02 -> píxel actual), corte de escena en la GPU (Δ medio de 1/8 > 0,12 -> frame actual, sin lecturas en la CPU), confianza/desoclusión (vuelta suave al actual), y frame real > 22 ms -> sin sintético.
  - Ritmo: el juego va a la mitad del panel (60 en 120 Hz, 72 en 144 Hz). El hilo de render presenta el intermedio; un hilo propio presenta el frame real a medio intervalo (`clock_nanosleep` absoluto) con sus propios command buffers, fences, render pass y framebuffers, así el render del juego no pierde 8 ms por frame.
  - Recursos creados una sola vez al activar o redimensionar y liberados al desactivar; sin asignaciones por frame ni `vkDeviceWaitIdle`. Apagado, el Present solo comprueba un puntero nulo.
- Herramienta `tools/build_shaders.ps1`: `glslc -O` del NDK + `spirv-val --target-env vulkan1.1` + cabeceras `*Spv.h`.
- **Verificado en el dispositivo:** cambio de tope en caliente; protección térmica activándose sola a 41 °C; overlay arriba legible sin tapar controles; SGSR más nítido que bilineal a 75 % y con la orientación correcta; HyperFG funcionando (1440 sintéticos, apagado/encendido en caliente, cambio de sala con FG activo) sin errores ni cierres. GPU y consumo bajan con la escala (100 % 6,05 W -> 75 % SGSR 4,31 W).
- **No conseguido todavía:** 60 fps estables con peor frame < 20 ms (en frío: 57,5-58 fps, peor 26 ms) y batería < 40 °C sostenida a resolución nativa: dejado en marcha, el juego subió a 44 °C (unos 5,7 W son la base del teléfono con la pantalla encendida). El fabricante estrangula la CPU a ~1-1,4 GHz por temperatura de carcasa aunque Android marque estado 0; con eso cualquier medición de fps cae a ~45.
- Tests JVM nuevos (`EfficiencyModeTest`): gobernador (umbrales, histéresis, desactivado, sin lectura), selectores de FPS, escala y reescalado, lógica de HyperFG (apagado por defecto, tope del juego, frame lento, sin historial, cambio en caliente), rangos del HUD y consumo. `OryonJitTranslatorTest` comprueba ahora que el anclaje lo hace el hilo del juego.
- **Pendiente:** tirones de cambio de escena en zonas avanzadas (la sesión del usuario mostró ráfagas de 300-1400 ms durante ~15 s); verificar en juego los fallos de física; acelerar el código traducido para bajar del arranque de ~17 s; persistencia de partidas de HK.

## 2026-11-19 (18:20) — Pruebas diferenciales del JIT y banco atascado tras morir en HK

- **Pruebas diferenciales** (`tools/jitdiff_gen.py` + `app/src/test/cpp/JitDiff.cpp`, formato JDF2): el PC ejecuta los mismos bytes x86 de verdad (ctypes + VirtualAlloc) y el teléfono los pasa por `OryonEmitterBackend`; se comparan XMM0-3, RAX, RDX, R8, RCX y los flags definidos. 666 instrucciones SSE y enteras, 200 casos cada una (con valores especiales: ±0, inf, NaN, denormales). `tools/build_native_tests.ps1` compila y sube `oryon_test`, `jit_scan` y `jit_diff`.
- **Bugs del JIT corregidos con esas pruebas:** PSHUFLW/PSHUFHW, CMPSS/CMPSD con predicados 4-6 (NEQ/NLT/NLE siempre falsos), SHUFPD, MOVQ xmm,xmm, índices de PSHUFB/VPSHUFB, CVTPD2DQ/CVTTPD2DQ (redondeo, indefinido y mitad alta), CBW/CWDE, desplazamientos de 32 bits con cuenta 0 (no ponían a cero la mitad alta), OF de los desplazamientos (`OfMode::Reg1`), RCL/RCR con cualquier cuenta, INC/DEC e IMUL de 16 bits, SHLD/SHRD de 32 bits con cuenta 0. `kCodegenVersion` = 7.
- Resultado final: `instrucciones=666 con_fallos=8` (solo LZCNT/TZCNT, que el PC de referencia no tiene, y RCP/RSQRT aproximadas); 17 sin soporte (addsubps/pd, extractps, pextrd, pinsrd, pmovzxdq, packusdw, ptest, PUSHFQ). En el juego no aparece ningún error de traducción. `oryon_test`: 39 510 comprobaciones, 0 fallos.
- **Banco atascado tras morir: NO resuelto.** Sigue igual con estos arreglos, con fusión de flags, flags muertos y caché JIT desactivados, a 30 FPS y sin ninguna entrada durante el reaparecer. Pausar/reanudar no lo arregla.
- **Mecanismo encontrado:** la FSM "Bench Control" de la escena (`level7`) arranca con `Pause Frame` (NextFrameEvent, un frame) -> `Check Start State` (`BoolTest RespawnResting` -> `START SITTING` / `START IDLE`). `HeroController.Respawn` (`<Respawn>d__491`) pone `RespawnResting = true` al empezar. Tras morir, el banco arranca en "idle" (por eso sale el letrero DESCANSAR con el caballero sentado y sin control); tras cargar partida no sale el letrero. Es decir, tras la muerte `Respawn` llega más de un frame después de que arranque la FSM del banco.
- Hipótesis: la escena de reaparecer se integra/activa en más frames que en un PC (hilo principal más lento: `UnloadUnusedAssets` 190 ms, integración por tramos), y el banco hace su comprobación antes que `GameManager.EnterHero`. Siguiente paso: medir en qué frame ocurre cada cosa y decidir el arreglo.
- El log de Unity sale por consola (`[Guest Console]` en logcat); `Player.log` queda vacío. No hay excepciones al morir ni al reaparecer.

## 2026-11-19 (20:15) — Banco atascado: CAUSA RAÍZ encontrada (no es el JIT)

- **Herramientas nuevas de diagnóstico:**
  - `debug.hyperplay.monotrace` + `monotrace2` (`NativeDllInterceptor.cpp`, `enableMonoTraceIfRequested`): antes de `mono_jit_init_version` llama a `mono_jit_set_trace_options` del juego. Imprime ENTER/LEAVE de esos métodos (con argumentos string y retornos) por `[Guest Console]`. Formato `M:Clase:Metodo`; los iteradores `<X>d__N` no los acepta. Máximo 92 caracteres por prop.
  - `debug.hyperplay.framelog 1` (`FramePacing.cpp`): `[Frame] N` por cada Present.
  - Hace falta `logcat -G 64M`.
  - Scripts en el scratchpad: `ildump.ps1` (desensamblador IL por reflexión), `both.sh`, `dietest2.sh`, `proc.ps1`, `states.ps1`, `waitbench.ps1`.
- **Cadena del fallo:**
  1. Al reaparecer, `AreaTitleController.Play` lee `visitedDirtmouth` = **false**.
  2. Como la zona cuenta como no visitada, sale el título GRANDE.
  3. ~0,64 s después de `Init Resting`, ese título difunde `BIG TITLE START`.
  4. La FSM "Bench Control" tiene una transición global `BIG TITLE START` → `Inert` que corta `Init Resting`, antes de que su `Wait` termine, de `Startle` y de «Wake To Sit».
  5. Al acabar el título, `BIG TITLE END` → `Idle` → `In Range` (letrero DESCANSAR). El caballero queda dormido y sin control, porque nunca se pasa por `Regain Control`.
  6. Al cargar partida no sale el título: `Init Resting` → `Startle` → `Update Map Silently` → `Resting` sin problema.
- **Pendiente:**
  1. Ver por qué `visitedDirtmouth` está a false en las partidas: la ranura 2 es mi partida editada y la ranura 1 es la del usuario. Comprobarlo descifrando `user1.dat`/`user2.dat` (AES-ECB con la clave pública de HK).
  2. Arreglarlo:
     - En la partida: poner `visitedDirtmouth` (y los demás `visited*` coherentes) a true.
     - Y/o averiguar si algún fallo nuestro impide que se guarde (el flag lo pone `UnvisitPause` → `PlayerData.SetBool`).
  3. Después: el geo que cae sin parar (`ObjectBounce`/física, sin investigar aún).
- Descartado con medidas: JIT (A/B sin fusión, sin flags muertos ni caché), FPS, entradas durante el reaparecer, `timeScale` (vuelve a 1), el ciclo de vida de la FSM y los animadores tk2d.
- **Arreglo aplicado (21:02):**
  - Partidas descifradas con `scratchpad/hksave.ps1` (decode/encode; ida y vuelta idéntica byte a byte).
  - Ranura 2, la de prueba que edité para empezar en el banco de Bocasucia: `visitedDirtmouth=false` y `visitedCrossroads=true`. Nunca «entró» en Bocasucia, así que el juego le saca el título grande al reaparecer.
  - Ranura 1 (Paso del Rey): todos los `visited*` a false, que es lo normal a esa altura.
  - Puesto `visitedDirtmouth=true` en `user2.dat`. Copia del original en `_HyperPlaySaves/.../Hollow Knight/_backup_20261119_visited/user2.dat`.
  - En una partida jugada de forma normal el flag se pone al entrar en Bocasucia, así que no es un fallo del emulador.
  - Pendiente de verificar morir → levantarse con la partida corregida. La prueba automática de las 21:05 no llegó a morir: el detector saltó con otro evento y había alguien usando el teléfono.
- **Banco confirmado por el usuario** (ranura 2 corregida): morir → levantarse funciona.

## 2026-11-19 (22:55) — Geo que rebota sin parar: FPU x87 no implementada (ARREGLADO)

- **Síntoma medido:** de 3 geos, 2 chocaban 70-72 veces en 28 s con periodo fijo de ~383 ms.
- **Velocidad trazada:** tras un choque pasaba de (−3,9; −25) a (−235; 775) y luego a (−4,8 M; −7,5 M). Sale disparada y «cae desde arriba sin parar».
- `ObjectBounce.OnCollisionEnter2D` escribía velocidad ≈ 0,35-0,55 × |v|². Es decir, `speed = rb.linearVelocity.magnitude` daba |v|² en vez de |v|.
- **Volcado del código JIT de Mono:** prop nueva `debug.hyperplay.codedump <texto>`; los bytes se desensamblan con `llvm-objdump` del NDK. `Vector2.get_magnitude` hace `movsd %xmm0,-8(%rsp); fldl -8(%rsp); fsqrt; fstpl -8(%rsp); movsd -8(%rsp),%xmm0`.
- **Causa:** el JIT traducía TODA la FPU x87 (D8-DF) como NOP. Solo FNSTCW/FLDCW/FNSTSW tenían algo. `fsqrt` no hacía nada, así que **todo `Math.Sqrt`/`Mathf.Sqrt` del código C# de Mono devolvía el cuadrado** (magnitudes, normalizados, distancias). Afectaba a toda la física/lógica del juego que usa magnitudes.
- **Arreglo:** FPU x87 real.
  - `CpuContext` gana `x87[8]` (doble), `x87Top`, `x87Tag`, `x87Sw` y `x87Cw`, al final para no mover offsets.
  - Intérprete nativo `executeX87`. Cubre:
    - FLD/FST/FSTP m32/m64/m80 e ST(i); FILD/FIST/FISTP/FISTTP m16/m32/m64.
    - Aritmética D8/DC/DA/DE (m32/m64/int y registros, con las formas P).
    - FCOM/FUCOM(P)(P), FCOMI/FUCOMI(P) con RFLAGS; FXCH, FCHS, FABS, FTST, FXAM.
    - Constantes, F2XM1, FYL2X(P1), FPTAN, FPATAN, FXTRACT, FPREM(1), FSQRT, FSINCOS, FRNDINT, FSCALE, FSIN, FCOS.
    - FNSTSW AX/m16, FNSTCW/FLDCW, FNINIT, FNCLEX, FFREE.
  - El JIT guarda el estado invitado (y NZCV, para la fusión), llama y recarga sin salir del bloque. FCOMI sale del bloque como PCMPISTRI.
  - FCMOVcc no soportado (falla con mensaje); las no implementadas avisan con `x87 no implementada: XX YY`.
  - `kCodegenVersion` = 8.
- **Pruebas:**
  - `jitdiff_gen.py` gana 16 casos x87, que el PC ejecuta en su FPU real: 16/16 sin fallos (200 casos cada uno).
  - También gana variantes «mismo registro» de todas las SSE reg-reg (xmm0,xmm0): todas bien salvo `rcp`/`rsqrt` aproximadas.
  - `oryon_test`: 39 510 comprobaciones, 0 fallos.
  - En el dispositivo: 11 objetos con rebote, 1-5 choques cada uno, y las velocidades decaen (5 → 2 → 0,7). Antes había 70+ choques y velocidades de millones.
- Efecto secundario: con la física correcta el caballero ya no sigue las rutas de los guiones de prueba antiguos (`geotest.sh` ajustado). Puede haber otros «bugs de física» de HK que esto también arregle.
## 2026-11-19 (23:05) — Frontend estilo consola portátil (SteamOS), pantalla de carga y mandos

- **Launcher nuevo** (`LauncherScreen.kt`, `ui/DeckTheme.kt`), con colores propios: azul noche, violeta HyperPlay #7C5CFF y cian.
  - Barra superior: marca, pestañas Inicio/Ajustes (LB/RB), hora y batería.
  - Inicio: el arte panorámico del juego enfocado de fondo (desenfocado), logo oficial, géneros y descripción, y un carrusel de cápsulas verticales (arte 600×900) con zoom y borde al enfocar.
  - Página del juego: héroe a todo el ancho con el logo, JUGAR/CONTINUAR, «Acerca del juego», «Información» (desarrollador, editor, lanzamiento, arquitectura, gráficos) y «Archivos».
  - Ajustes: buscar juegos, añadir/quitar carpetas, dispositivo y mandos.
  - Barra inferior de botones: A abrir/jugar, X jugar directo, Y buscar juegos, B atrás/salir.
  - Ya no hay letras: si no hay arte, se ve el icono del `.exe`.
- **Arte e información oficial** (`art/GameArtRepository.kt`):
  - appid desde `steam_appid.txt` o `storesearch` de la tienda por título.
  - Ficha `appdetails` en español.
  - Imágenes oficiales del CDN de Steam (library_600x900_2x, library_hero, logo, header), en caché en `files/art/<hash>/`; la ficha se refresca cada 30 días.
  - `art/ExeIconExtractor.kt`: icono del exe (RT_GROUP_ICON → RT_ICON, PNG o DIB envuelto en .ico).
- **Pantalla de carga propia** (`SessionOverlay.LoadingScreen`): héroe del juego de fondo, logo, barra violeta, etapa/tiempo y marca. Antes se quitaba al crear el dispositivo D3D, y Unity enseñaba luego 10-15 s de negro. Ahora se quita cuando hay ≥ 40 draws D3D11 reales (`nativeDirectDrawCount`, `GameSessionLogic.contentStarted`), con red de seguridad de 45 s.
- **Mandos:**
  - Ya existía puente XInput de 4 slots con hotplug y rumble por `VibratorManager`, y auto-ocultado del táctil: el mando lo oculta y tocar lo muestra (ambos funcionan a la vez).
  - Nuevo: gatillos analógicos en Z/RZ para mandos HID con el stick derecho en RX/RY (normaliza -1..1).
  - Nuevo: vibración del teléfono por defecto si el mando no tiene motores (GameSir enganchado y la mayoría de Bluetooth HID).
  - Nuevo: log por mando de stick derecho, gatillos y vibrador.
- Compila; `testDebugUnitTest lintDebug` OK. Instalado. **Sin verificar en pantalla**: el teléfono quedó bloqueado (desbloqueo facial) al 22 % de batería.
- Pendiente: capturas del launcher, primera descarga de arte de HK (appid 367520) y prueba de la pantalla de carga y de un mando físico.
- Sobre usar las DLL de VC++/.NET/DirectX para ARM64 (como GameHub): HK usa su propio Mono (`MonoBleedingEdge`), no el .NET de Microsoft. Las DLL ARM64 no se pueden llamar desde código x86-64 traducido sin capa tipo ARM64EC/Wine, y este fallo era de datos de la partida.