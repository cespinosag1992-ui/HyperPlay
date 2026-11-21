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
then u16 (label block size) and the labels: NUL-terminated; a node's label = labels[value..]
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
| `TRNS/TRAN` (`TRNB`, `TRTL`) | transitions: `TRNB` +0 id, +16 region/destination, +32 `u32 start`, +36 `u32 end`. 60 bytes with start == end = end of a loop region: jump back to the region start (its position follows the region GUID in the `TLNB`). 86 bytes: +40 list of conditions `{parameter GUID, f32 min, f32 max}`; start == end = conditional jump marker, start < end = transition region (jumps as soon as the condition holds) |
| `GBSB`/`MBSB`/`IBSB`/`RBSB` + `BUS ` | bus: +0 id, +18 parent bus; `BUS `: two lists (empty = `01 00`), u16, `f32` fader dB |
| `EVTB` (more) | +16 snapshot (`SNAB` id) for snapshot events, +48 the event's input bus (`IBUS`, parent = its group bus) |
| `SNAS/SNAP/SNAB` | snapshot: +0 id, +20 list of `{u32, bus GUID, u32 type (0 = volume), f32 value (dB)}` |
| `INST` (in every instrument LIST) | +0 owner timeline GUID, **+24 `int32` loop count** (0 = once, -1 = forever) / +0 GUID de la línea de tiempo, **+24 nº de bucles** |
| `PRMS/PARM/PRMB` | parameter: +0 GUID, +21 `u16` name length, name (no NUL), then `f32` min, max, **default** (checked with FMOD's `GetParameterByIndex`) |
| `PMLS/PMLO` (`PMLB`, `CTRO`) | `PMLB` +16 parameter GUID, +32 event GUID, +48 list of instruments placed on the parameter axis (`GUID, f32 start, f32 length`); `CTRO` = list of the controllers that parameter drives |
| `CTRS/CTRL` | controller: +0 id, +16 owner (instrument), +32 curve id |
| `CRVS/CURV` | curve: +0 id, +32 list of points `{f32 x, f32 y, u32 shape, f32 ?}` (y in dB for volume; -42 = silence) |
| `PRPS/PROP` | property of the owner: +8 property GUID (`7400f0ce…` instrument volume, `d65ba128…`, `c021a555…` also dB), +24 list of its controllers |
| lists / listas | `u16 (n << 1 | 1)`, `u16` element size, elements |
| `MODS/MODU/MODB` | modulators (random pitch/volume, AHDSR) |
| `GBSS`, `IBSS`, `MBSS`, `VCAS` | buses and VCAs |

Bank sizes in Celeste: sfx 587 events / 3527 waveform instruments / 317 multi / 7 scatterer;
music 94 events / 6020 waveform instruments / 43 parameters / 110 transitions.

## Sounds / Sonidos (FSB5 Vorbis)

See `app/src/main/cpp/audio/FsbVorbis.cpp`: FMOD strips the Vorbis setup header; it is taken from
the game's `fmod.dll` by CRC32 and Android's Vorbis decoder does the rest (each packet + int32 -1).
Verified on the phone: all tested mono and stereo sounds decode to the exact frame count.
