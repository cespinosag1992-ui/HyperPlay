# FMOD Studio 1.10 banks (Celeste) — what HyperPlay knows / Lo que HyperPlay sabe

Working notes for HyperPlay's FMOD-free audio (no Firelight code is used or shipped). Every point
below was checked against the real FMOD on a PC with `fmodstudio.dll` from the game itself
(the "oracle": a 32-bit helper that loads the banks and dumps `GUID path` per event).

Notas de trabajo del audio sin FMOD de HyperPlay (no se usa ni distribuye código de Firelight).
Cada punto se comprobó con el FMOD real en un PC, usando el `fmodstudio.dll` del propio juego.

## Container / Contenedor

`RIFF` form `FEV `, chunk `FMT ` (version 0x67), `LIST PROJ` with one `LIST` per object type,
and inside the bank a `SND ` region with an `FSB5` (codec 15 = Vorbis). GUIDs are stored as
Windows GUIDs (little-endian `Data1..Data3`).

## Strings bank (`Master Bank.strings.bank`, chunk `STDT`) / Banco de nombres

```
u32 version (1)
u16 nodeWords   -> node area = 8 .. 8 + 4*nodeWords; nodes are 8 bytes, count = nodeWords/2
u16 ?
nodes[]: w1 = value(24) | char(8) << 24      value = label offset (or 0xFFFFFF)
         w2 = first(24) | count(8) << 24     children = nodes[first .. first+count-1]
GUID table at 8 + 4*nodeWords: 16 bytes per string, indexed by the leaf's `first`
labels right after the GUID table: NUL-terminated; a node's label = labels[value..]
(it includes the node's own first char)
```

A path is the concatenation of labels from the root down. A node with char 0 is a terminator
(string index = `first`); a node with `count == 0` is also a leaf. Celeste: 996 strings, 810
events; `event:/ui/game/pause` = `c6311b80-2ae7-4155-80f1-d3e2713efaa3` (index 795).

## Event to sound / De evento a sonido

| Chunk | Content (checked) |
|---|---|
| `EVTS/EVNT/EVTB` | +0 event GUID, +32 timeline GUID, then bus/other GUIDs and flags |
| `TLNS/TMLN/TLNB` | +0 timeline GUID, +16 owner event GUID, `u16 ?, u16 count?, u16 ?`, then per placed instrument: instrument GUID, `u32 start`, `u32 length` (samples; `ui_game_pause` length 0x17301 for 94976 frames) |
| `WAIS/WAIT/WAIB` | +0 instrument GUID, +16 waveform GUID |
| `WAVS/WAV ` | +0 waveform GUID, `u32 0x0C`, `u16 fsb index?`, `u16 subsound index` (`ui_game_pause` = 2) |
| `MUIS/MUIT` (`MUIB`, `PLST`) | multi instrument: playlist of instruments (random/sequential) |
| `SPIS/SPIT` (`SPIB`, `PLST`) | scatterer instrument |
| `TRNS/TRAN` (`TRNB`, `TRTL`) | transitions / loop regions on the timeline |
| `PRMS/PARM/PRMB` | parameters (Celeste music uses them for layers and sections) |
| `CTRS/CTRL`, `CRVS/CURV` | automation curves (parameter -> property) |
| `MODS/MODU/MODB` | modulators (random pitch/volume, AHDSR) |
| `GBSS`, `IBSS`, `MBSS`, `VCAS` | buses and VCAs |

Bank sizes in Celeste: sfx 587 events / 3527 waveform instruments / 317 multi / 7 scatterer;
music 94 events / 6020 waveform instruments / 43 parameters / 110 transitions.

## Sounds / Sonidos (FSB5 Vorbis)

See `app/src/main/cpp/audio/FsbVorbis.cpp`: FMOD strips the Vorbis setup header; it is taken from
the game's `fmod.dll` by CRC32 and Android's Vorbis decoder does the rest (each packet + int32 -1).
Verified on the phone: all tested mono and stereo sounds decode to the exact frame count.
