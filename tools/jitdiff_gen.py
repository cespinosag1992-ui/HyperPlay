"""Generador de vectores de prueba x86 reales para el JIT (prueba diferencial).

Ejecuta en la CPU x86-64 del PC (Windows, ctypes + VirtualAlloc) un "arnés" alrededor de cada
instrucción: carga XMM0-3, RAX, RDX y R8 de un estado en memoria, ejecuta la instrucción y guarda
registros y RFLAGS. El mismo arnés se ejecuta en el móvil con el JIT (tools/JitDiff.cpp) y se
comparan los resultados bit a bit. Así se encuentran fallos de semántica como el de PSHUFLW.

Uso:  python tools/jitdiff_gen.py salida.bin [casos_por_instruccion]
"""
import ctypes
import math
import random
import struct
import sys

# Estado (128 B): xmm0..xmm3 (0..63), rax (64), rdx (72), flags CF/PF/ZF/SF/OF en bytes 80..84,
# r8 (88), rcx (96), CF de entrada (104: distinto de 0 -> CF=1). Base del estado en R11 (el
# puntero llega en RCX, que queda libre para SHL/ROR... por CL).
STATE = 128
PROLOGUE = bytes.fromhex(
    "49 89 CB"            # mov r11,rcx
    "41 0F 10 03"         # movups xmm0,[r11]
    "41 0F 10 4B 10"      # movups xmm1,[r11+16]
    "41 0F 10 53 20"      # movups xmm2,[r11+32]
    "41 0F 10 5B 30"      # movups xmm3,[r11+48]
    "49 8B 43 40"         # mov rax,[r11+64]
    "49 8B 53 48"         # mov rdx,[r11+72]
    "4D 8B 43 58"         # mov r8,[r11+88]
    "49 8B 4B 60"         # mov rcx,[r11+96]
    "45 8B 4B 68"         # mov r9d,[r11+104]
    "41 83 C1 FF"         # add r9d,-1  (CF = entrada != 0; resto de flags deterministas)
    .replace(" ", ""))
EPILOGUE = bytes.fromhex(
    "41 0F 11 03"         # movups [r11],xmm0
    "41 0F 11 4B 10"      # movups [r11+16],xmm1
    "41 0F 11 53 20"      # movups [r11+32],xmm2
    "41 0F 11 5B 30"      # movups [r11+48],xmm3
    "49 89 43 40"         # mov [r11+64],rax
    "49 89 53 48"         # mov [r11+72],rdx
    "4D 89 43 58"         # mov [r11+88],r8
    "49 89 4B 60"         # mov [r11+96],rcx
    # Flags con SETcc a memoria (el JIT no soporta PUSHFQ): bytes 80..84 = CF, PF, ZF, SF, OF.
    "41 0F 92 43 50"      # setb [r11+80]
    "41 0F 9A 43 51"      # setp [r11+81]
    "41 0F 94 43 52"      # sete [r11+82]
    "41 0F 98 43 53"      # sets [r11+83]
    "41 0F 90 43 54"      # seto [r11+84]
    "C3"
    .replace(" ", ""))

# Tipo de datos de entrada: F32 (4 floats), F64 (2 doubles), INT (bytes aleatorios / enteros).
F32, F64, INT = 0, 1, 2
# Marcas de comparación: APPROX = aproximación (RCP/RSQRT: tolerancia relativa 2^-11).
APPROX = 0x10


# Flags que se comparan (bit 0 CF, 1 PF, 2 ZF, 3 SF, 4 OF): los que x86 deja "indefinidos" en una
# instrucción no se comparan (ningún programa correcto depende de ellos).
CF, PF, ZF, SF, OF = 1, 2, 4, 8, 16
ALLF = CF | PF | ZF | SF | OF


def flag_mask(name):
    n = name.split()[0]
    if n.startswith(("imul", "mul")):
        return CF | OF
    if n == "div":
        return 0
    if n in ("bsf", "bsr"):
        return ZF
    if n in ("bt", "bts", "btr", "btc"):
        return CF
    if n in ("tzcnt", "lzcnt"):
        return CF | ZF
    if n in ("shl", "shr", "sar", "shld", "shrd"):
        return ALLF if name.endswith(",1") else CF | PF | ZF | SF
    if n in ("rol", "ror", "rcl", "rcr"):
        return CF | OF if name.endswith(",1") else CF
    return ALLF


def ins(name, hexbytes, kind, flags=0):
    return (name, bytes.fromhex(hexbytes.replace(" ", "")), kind | flags)


INSTRUCTIONS = []
R = "C1"  # modrm reg=xmm0, rm=xmm1
M = "43 10"  # modrm [r11+16] (xmm1 del estado de entrada; requiere REX.B = 41)
for op, nm in [("58", "add"), ("59", "mul"), ("5C", "sub"), ("5E", "div"), ("5D", "min"), ("5F", "max"), ("51", "sqrt")]:
    INSTRUCTIONS += [
        ins(f"{nm}ss r", f"F3 0F {op} {R}", F32), ins(f"{nm}ss m", f"F3 41 0F {op} {M}", F32),
        ins(f"{nm}ps r", f"0F {op} {R}", F32), ins(f"{nm}ps m", f"41 0F {op} {M}", F32),
        ins(f"{nm}sd r", f"F2 0F {op} {R}", F64), ins(f"{nm}sd m", f"F2 41 0F {op} {M}", F64),
        ins(f"{nm}pd r", f"66 0F {op} {R}", F64), ins(f"{nm}pd m", f"66 41 0F {op} {M}", F64),
    ]
INSTRUCTIONS += [
    ins("rcpss", "F3 0F 53 C1", F32, APPROX), ins("rcpps", "0F 53 C1", F32, APPROX),
    ins("rsqrtss", "F3 0F 52 C1", F32, APPROX), ins("rsqrtps", "0F 52 C1", F32, APPROX),
    ins("andps", "0F 54 C1", F32), ins("andnps", "0F 55 C1", F32), ins("orps", "0F 56 C1", F32),
    ins("xorps", "0F 57 C1", F32), ins("andpd", "66 0F 54 C1", F64), ins("andnpd", "66 0F 55 C1", F64),
    ins("orpd", "66 0F 56 C1", F64), ins("xorpd", "66 0F 57 C1", F64),
    ins("unpcklps", "0F 14 C1", F32), ins("unpckhps", "0F 15 C1", F32),
    ins("unpcklpd", "66 0F 14 C1", F64), ins("unpckhpd", "66 0F 15 C1", F64),
    ins("movhlps", "0F 12 C1", F32), ins("movlhps", "0F 16 C1", F32),
    ins("movss r", "F3 0F 10 C1", F32), ins("movsd r", "F2 0F 10 C1", F64),
    ins("movss m", "F3 41 0F 10 43 10", F32), ins("movsd m", "F2 41 0F 10 43 10", F64),
    ins("movshdup", "F3 0F 16 C1", F32), ins("movsldup", "F3 0F 12 C1", F32), ins("movddup", "F2 0F 12 C1", F64),
    ins("cvtss2sd", "F3 0F 5A C1", F32), ins("cvtsd2ss", "F2 0F 5A C1", F64),
    ins("cvtps2pd", "0F 5A C1", F32), ins("cvtpd2ps", "66 0F 5A C1", F64),
    ins("cvtdq2ps", "0F 5B C1", INT), ins("cvtps2dq", "66 0F 5B C1", F32), ins("cvttps2dq", "F3 0F 5B C1", F32),
    ins("cvtdq2pd", "F3 0F E6 C1", INT), ins("cvtpd2dq", "F2 0F E6 C1", F64), ins("cvttpd2dq", "66 0F E6 C1", F64),
    ins("cvttss2si eax", "F3 0F 2C C0", F32), ins("cvttss2si rax", "F3 48 0F 2C C0", F32),
    ins("cvtss2si eax", "F3 0F 2D C0", F32), ins("cvtss2si rax", "F3 48 0F 2D C0", F32),
    ins("cvttsd2si eax", "F2 0F 2C C0", F64), ins("cvttsd2si rax", "F2 48 0F 2C C0", F64),
    ins("cvtsd2si eax", "F2 0F 2D C0", F64), ins("cvtsd2si rax", "F2 48 0F 2D C0", F64),
    ins("cvtsi2ss eax", "F3 0F 2A C0", INT), ins("cvtsi2ss rax", "F3 48 0F 2A C0", INT),
    ins("cvtsi2sd eax", "F2 0F 2A C0", INT), ins("cvtsi2sd rax", "F2 48 0F 2A C0", INT),
    ins("ucomiss", "0F 2E C1", F32), ins("comiss", "0F 2F C1", F32),
    ins("ucomisd", "66 0F 2E C1", F64), ins("comisd", "66 0F 2F C1", F64),
    ins("ucomiss m", "41 0F 2E 43 10", F32), ins("comisd m", "66 41 0F 2F 43 10", F64),
    ins("movmskps", "0F 50 C1", F32), ins("movmskpd", "66 0F 50 C1", F64),
    ins("pmovmskb", "66 0F D7 C1", INT),
    ins("haddps", "F2 0F 7C C1", F32), ins("hsubps", "F2 0F 7D C1", F32), ins("addsubps", "F2 0F D0 C1", F32),
    ins("haddpd", "66 0F 7C C1", F64), ins("addsubpd", "66 0F D0 C1", F64),
    ins("movd xmm,eax", "66 0F 6E C0", INT), ins("movq xmm,rax", "66 48 0F 6E C0", INT),
    ins("movd eax,xmm", "66 0F 7E C0", INT), ins("movq rax,xmm", "66 48 0F 7E C0", INT),
    ins("movq xmm,xmm", "F3 0F 7E C1", INT),
]
for imm in range(8):
    INSTRUCTIONS += [
        ins(f"cmpps {imm}", f"0F C2 C1 {imm:02X}", F32), ins(f"cmpss {imm}", f"F3 0F C2 C1 {imm:02X}", F32),
        ins(f"cmppd {imm}", f"66 0F C2 C1 {imm:02X}", F64), ins(f"cmpsd {imm}", f"F2 0F C2 C1 {imm:02X}", F64),
    ]
for imm in (0x00, 0x1B, 0x4E, 0x93, 0xE4, 0x55, 0xAA, 0xFF, 0x44, 0xEE):
    INSTRUCTIONS += [
        ins(f"shufps {imm:02X}", f"0F C6 C1 {imm:02X}", F32), ins(f"pshufd {imm:02X}", f"66 0F 70 C1 {imm:02X}", INT),
        ins(f"pshuflw {imm:02X}", f"F2 0F 70 C1 {imm:02X}", INT), ins(f"pshufhw {imm:02X}", f"F3 0F 70 C1 {imm:02X}", INT),
    ]
for imm in (0, 1, 2, 3):
    INSTRUCTIONS += [ins(f"shufpd {imm}", f"66 0F C6 C1 {imm:02X}", F64)]
for op, nm in [("FE", "paddd"), ("FA", "psubd"), ("D4", "paddq"), ("FB", "psubq"), ("FC", "paddb"), ("FD", "paddw"),
               ("F8", "psubb"), ("F9", "psubw"), ("D5", "pmullw"), ("E5", "pmulhw"), ("E4", "pmulhuw"), ("F4", "pmuludq"),
               ("EF", "pxor"), ("DB", "pand"), ("DF", "pandn"), ("EB", "por"), ("76", "pcmpeqd"), ("66", "pcmpgtd"),
               ("74", "pcmpeqb"), ("64", "pcmpgtb"), ("75", "pcmpeqw"), ("65", "pcmpgtw"), ("62", "punpckldq"),
               ("6A", "punpckhdq"), ("6C", "punpcklqdq"), ("6D", "punpckhqdq"), ("60", "punpcklbw"), ("61", "punpcklwd"),
               ("68", "punpckhbw"), ("69", "punpckhwd"), ("6B", "packssdw"), ("63", "packsswb"), ("67", "packuswb"),
               ("DA", "pminub"), ("DE", "pmaxub"), ("EA", "pminsw"), ("EE", "pmaxsw"), ("E0", "pavgb"), ("F6", "psadbw"),
               ("DC", "paddusb"), ("EC", "paddsb"), ("D8", "psubusb"), ("E8", "psubsb"), ("F5", "pmaddwd")]:
    INSTRUCTIONS += [ins(nm, f"66 0F {op} C1", INT)]
for grp, sub, nm in [("72", "F0", "pslld"), ("72", "E0", "psrad"), ("72", "D0", "psrld"), ("73", "F0", "psllq"),
                     ("73", "D0", "psrlq"), ("71", "F0", "psllw"), ("71", "E0", "psraw"), ("71", "D0", "psrlw"),
                     ("73", "D8", "psrldq"), ("73", "F8", "pslldq")]:
    for imm in (1, 3, 7, 13, 31):
        INSTRUCTIONS += [ins(f"{nm} {imm}", f"66 0F {grp} {sub} {imm:02X}", INT)]
INSTRUCTIONS += [
    ins("pshufb", "66 0F 38 00 C1", INT), ins("pmulld", "66 0F 38 40 C1", INT), ins("pminsd", "66 0F 38 39 C1", INT),
    ins("pmaxsd", "66 0F 38 3D C1", INT), ins("pminud", "66 0F 38 3B C1", INT), ins("pmaxud", "66 0F 38 3F C1", INT),
    ins("pabsd", "66 0F 38 1E C1", INT), ins("ptest", "66 0F 38 17 C1", INT), ins("pcmpeqq", "66 0F 38 29 C1", INT),
    ins("pmovsxdq", "66 0F 38 25 C1", INT), ins("pmovzxdq", "66 0F 38 35 C1", INT), ins("blendvps", "66 0F 38 14 C1", F32),
    ins("packusdw", "66 0F 38 2B C1", INT),
]
for imm in (0x00, 0x01, 0x02, 0x03, 0x04, 0x09, 0x0A, 0x0B):
    INSTRUCTIONS += [ins(f"roundss {imm}", f"66 0F 3A 0A C1 {imm:02X}", F32), ins(f"roundps {imm}", f"66 0F 3A 08 C1 {imm:02X}", F32),
                     ins(f"roundsd {imm}", f"66 0F 3A 0B C1 {imm:02X}", F64)]
for imm in (0x10, 0x4C, 0xB2, 0xE1, 0x3F, 0xC8):
    INSTRUCTIONS += [ins(f"insertps {imm:02X}", f"66 0F 3A 21 C1 {imm:02X}", F32)]
for imm in (0x71, 0xF1, 0x3F, 0xFF, 0x31):
    INSTRUCTIONS += [ins(f"dpps {imm:02X}", f"66 0F 3A 40 C1 {imm:02X}", F32)]
for imm in (0x5, 0xA, 0x3, 0xC):
    INSTRUCTIONS += [ins(f"blendps {imm:X}", f"66 0F 3A 0C C1 {imm:02X}", F32)]
for imm in (0, 1, 2, 3):
    INSTRUCTIONS += [ins(f"extractps {imm}", f"66 0F 3A 17 C0 {imm:02X}", F32), ins(f"pextrd {imm}", f"66 0F 3A 16 C0 {imm:02X}", INT),
                     ins(f"pinsrd {imm}", f"66 0F 3A 22 C0 {imm:02X}", INT)]
for imm in (0, 3, 5, 7):
    INSTRUCTIONS += [ins(f"pextrw {imm}", f"66 0F C5 C1 {imm:02X}", INT), ins(f"pinsrw {imm}", f"66 0F C4 C0 {imm:02X}", INT)]

# ---------------------------------------------------------------- FPU x87
# El JIT de Mono usa x87 para Math.Sqrt (fldl/fsqrt/fstpl), etc. Entrada: [r11+16] / [r11+24]
# (las dos mitades de xmm1); salida: fstp a [r11] (xmm0). En Windows x64 la FPU va a 53 bits.
FLD16, FLD24, FSTP0 = "41 DD 43 10", "41 DD 43 18", "41 DD 1B"
INSTRUCTIONS += [
    ins("x87 fsqrt", f"{FLD16} D9 FA {FSTP0}", F64),
    ins("x87 fsqrt f32", "41 D9 43 10 D9 FA 41 D9 1B", F32),
    ins("x87 fchs", f"{FLD16} D9 E0 {FSTP0}", F64), ins("x87 fabs", f"{FLD16} D9 E1 {FSTP0}", F64),
    ins("x87 fld1 faddp", f"{FLD16} D9 E8 DE C1 {FSTP0}", F64),
    ins("x87 fxch fsubp", f"{FLD16} {FLD24} D9 C9 DE E9 {FSTP0}", F64),
    ins("x87 fdivrp", f"{FLD16} {FLD24} DE F1 {FSTP0}", F64),
    ins("x87 fild fistp", "41 DF 6B 40 41 DF 7B 48", INT),
    ins("x87 fcomip", f"{FLD24} {FLD16} DF F1 DD D8", F64),
    ins("x87 fucomip", f"{FLD24} {FLD16} DF E9 DD D8", F64),
]
for sub, nm in [(0, "fadd"), (1, "fmul"), (4, "fsub"), (5, "fsubr"), (6, "fdiv"), (7, "fdivr")]:
    INSTRUCTIONS += [ins(f"x87 {nm} m64", f"{FLD16} 41 DC {0x43 | (sub << 3):02X} 18 {FSTP0}", F64)]


# ---------------------------------------------------------------- enteros y flags
# Para estas instrucciones el arnés deja CF de entrada = bit 0 de [rcx+96] (BT) para probar
# ADC/SBB/RCL/RCR, y la instrucción puede leer/escribir RAX, RDX y R8 (RCX es la base).
GPR, DIVSAFE = 3, 4
INT_INSTRUCTIONS = []
for w, rex in (("q", "48"), ("d", "")):
    for op, nm in [("01", "add"), ("29", "sub"), ("11", "adc"), ("19", "sbb"), ("21", "and"), ("09", "or"),
                   ("31", "xor"), ("39", "cmp"), ("85", "test")]:
        INT_INSTRUCTIONS += [ins(f"{nm} {w} rax,rdx", f"{rex} {op} D0", GPR)]
    for sub, nm in [("C0", "inc"), ("C8", "dec")]:
        INT_INSTRUCTIONS += [ins(f"{nm} {w} rax", f"{rex} FF {sub}", GPR)]
    for sub, nm in [("D8", "neg"), ("D0", "not")]:
        INT_INSTRUCTIONS += [ins(f"{nm} {w} rax", f"{rex} F7 {sub}", GPR)]
    INT_INSTRUCTIONS += [
        ins(f"imul {w} rax,rdx", f"{rex} 0F AF C2", GPR), ins(f"imul {w} rax,rdx,7", f"{rex} 6B C2 07", GPR),
        ins(f"imul {w} rax,rdx,-300", f"{rex} 69 C2 D4 FE FF FF", GPR), ins(f"mul {w} r8", f"{'49' if rex else '41'} F7 E0", GPR),
        ins(f"imul1 {w} r8", f"{'49' if rex else '41'} F7 E8", GPR),
        ins(f"div {w} r8", f"{'49' if rex else '41'} F7 F0", GPR | (DIVSAFE << 5)),
        ins(f"bsf {w} rax,rdx", f"{rex} 0F BC C2", GPR), ins(f"bsr {w} rax,rdx", f"{rex} 0F BD C2", GPR),
        ins(f"tzcnt {w} rax,rdx", f"F3 {rex} 0F BC C2", GPR), ins(f"lzcnt {w} rax,rdx", f"F3 {rex} 0F BD C2", GPR),
        ins(f"popcnt {w} rax,rdx", f"F3 {rex} 0F B8 C2", GPR), ins(f"bswap {w} rax", f"{rex} 0F C8", GPR),
        ins(f"bt {w} rax,rdx", f"{rex} 0F A3 D0", GPR), ins(f"bts {w} rax,rdx", f"{rex} 0F AB D0", GPR),
        ins(f"btr {w} rax,rdx", f"{rex} 0F B3 D0", GPR), ins(f"btc {w} rax,rdx", f"{rex} 0F BB D0", GPR),
        ins(f"bt {w} rax,13", f"{rex} 0F BA E0 0D", GPR),
        ins(f"xadd {w} rax,rdx", f"{rex} 0F C1 D0", GPR), ins(f"cmpxchg {w} rdx,r8", f"{'4C' if rex else '44'} 0F B1 C2", GPR),
        ins(f"shld {w} rax,rdx,cl", f"{rex} 0F A5 D0", GPR), ins(f"shrd {w} rax,rdx,cl", f"{rex} 0F AD D0", GPR),
        ins(f"shld {w} rax,rdx,9", f"{rex} 0F A4 D0 09", GPR), ins(f"shrd {w} rax,rdx,9", f"{rex} 0F AC D0 09", GPR),
    ]
    for sub, nm in [("E0", "shl"), ("E8", "shr"), ("F8", "sar"), ("C0", "rol"), ("C8", "ror"), ("D0", "rcl"), ("D8", "rcr")]:
        INT_INSTRUCTIONS += [ins(f"{nm} {w} rax,1", f"{rex} D1 {sub}", GPR), ins(f"{nm} {w} rax,cl", f"{rex} D3 {sub}", GPR)]
        for imm in (0, 3, 31, 33, 63):
            INT_INSTRUCTIONS += [ins(f"{nm} {w} rax,{imm}", f"{rex} C1 {sub} {imm:02X}", GPR)]
# Registros parciales (8 y 16 bits) y extensiones.
INT_INSTRUCTIONS += [
    ins("add al,dl", "00 D0", GPR), ins("add ah,dl", "00 D4", GPR), ins("add ax,dx", "66 01 D0", GPR),
    ins("sub al,dh", "28 F0", GPR), ins("mov al,dl", "88 D0", GPR), ins("mov ah,dl", "88 D4", GPR),
    ins("mov al,dh", "88 F0", GPR), ins("mov ax,dx", "66 89 D0", GPR), ins("inc ax", "66 FF C0", GPR),
    ins("neg al", "F6 D8", GPR), ins("imul ax,dx", "66 0F AF C2", GPR), ins("shl ax,5", "66 C1 E0 05", GPR),
    ins("sar al,3", "C0 F8 03", GPR), ins("rol ax,7", "66 C1 C0 07", GPR), ins("cmp al,dl", "38 D0", GPR),
    ins("test ah,dl", "84 D4", GPR), ins("xchg al,ah", "86 C4", GPR),
    ins("movzx eax,dl", "0F B6 C2", GPR), ins("movzx eax,dh", "0F B6 C6", GPR), ins("movzx eax,dx", "0F B7 C2", GPR),
    ins("movsx rax,dl", "48 0F BE C2", GPR), ins("movsx eax,dh", "0F BE C6", GPR), ins("movsx rax,dx", "48 0F BF C2", GPR),
    ins("movsxd rax,edx", "48 63 C2", GPR), ins("mov eax,edx", "89 D0", GPR), ins("cdq", "99", GPR), ins("cqo", "48 99", GPR),
    ins("cwde", "98", GPR), ins("cdqe", "48 98", GPR), ins("cbw", "66 98", GPR),
    ins("lea rax,[rdx+r8*4+7]", "4A 8D 44 82 07", GPR), ins("lea eax,[rdx+r8*8-9]", "42 8D 44 C2 F7", GPR),
    ins("xchg rax,rdx", "48 92", GPR), ins("cmc", "F5", GPR), ins("stc", "F9", GPR), ins("clc", "F8", GPR),
]
# CMOVcc / SETcc / Jcc con los flags de CMP rdx,r8 (16 condiciones).
for cc in range(16):
    INT_INSTRUCTIONS += [
        ins(f"cmov{cc:X} (cmp)", f"4C 39 C2 48 0F {0x40 + cc:02X} C2", GPR),
        ins(f"set{cc:X} (cmp)", f"4C 39 C2 0F {0x90 + cc:02X} C0", GPR),
        ins(f"cmov{cc:X} (sub32)", f"44 29 C2 0F {0x40 + cc:02X} C2", GPR),
        ins(f"set{cc:X} (test)", f"4C 85 C2 0F {0x90 + cc:02X} C0", GPR),
        # Jcc: rax = 1 si salta, 2 si no.
        ins(f"j{cc:X} (cmp)", f"4C 39 C2 {0x70 + cc:02X} 09 48 C7 C0 02 00 00 00 EB 07 48 C7 C0 01 00 00 00", GPR),
        ins(f"set{cc:X} (add)", f"4C 01 C2 0F {0x90 + cc:02X} C0", GPR),
    ]
# Variantes "mismo registro" (xmm0, xmm0): el JIT de Mono las emite constantemente (p. ej.
# cvtss2sd/sqrtsd/cvtsd2ss xmm0,xmm0 en Vector2.magnitude) y las de arriba solo prueban xmm0 <- xmm1.
SAME_REG = []
for name, code, kind in list(INSTRUCTIONS):
    hx = code.hex().upper()
    # Solo reg-reg con ModRM C1 tras un opcode 0F xx / 0F 38 xx / 0F 3A xx (sin memoria).
    for opc in ("0F38", "0F3A", "0F"):
        p = hx.find(opc)
        if p < 0:
            continue
        mpos = p + len(opc) + 2
        if hx[mpos:mpos + 2] == "C1" and " m" not in name:
            SAME_REG.append((name + " =", bytes.fromhex(hx[:mpos] + "C0" + hx[mpos + 2:]), kind))
        break
INSTRUCTIONS += SAME_REG
INSTRUCTIONS += INT_INSTRUCTIONS

# ---------------------------------------------------------------- valores de entrada
SPECIAL_F32 = [0.0, -0.0, 1.0, -1.0, 0.5, 2.0, float("inf"), float("-inf"), float("nan"), 1e-40, -1e-40,
               3.4028235e38, 1.1754944e-38, 0.1, -0.1, 1e7, 16777216.0, 2147483648.0, -2147483649.0, 0.49999997, 2.5, -2.5]
SPECIAL_F64 = [0.0, -0.0, 1.0, -1.0, 0.5, float("inf"), float("-inf"), float("nan"), 5e-324, 1.7976931348623157e308,
               0.1, -0.1, 9007199254740993.0, 2147483648.0, -2147483649.0, 4.5, -4.5, 1e-310]


def f32(rng):
    r = rng.random()
    if r < 0.25:
        return rng.choice(SPECIAL_F32)
    if r < 0.6:
        return rng.uniform(-100.0, 100.0)  # posiciones/velocidades típicas de física 2D
    if r < 0.8:
        return rng.uniform(-1.0, 1.0)
    return rng.uniform(-1.0, 1.0) * 10.0 ** rng.randint(-30, 30)


def f64(rng):
    r = rng.random()
    if r < 0.25:
        return rng.choice(SPECIAL_F64)
    if r < 0.7:
        return rng.uniform(-1000.0, 1000.0)
    return rng.uniform(-1.0, 1.0) * 10.0 ** rng.randint(-200, 200)


def pack_f32(v):
    try:
        return struct.pack("<f", v)
    except OverflowError:
        return struct.pack("<f", math.copysign(float("inf"), v))


def make_state_for(kind, rng):
    """Estado de entrada según el tipo (bits 0-3) y las marcas (DIVSAFE en el bit 7)."""
    base_kind = kind & 0x0F
    return make_state((INT if base_kind == GPR else base_kind) | (kind & 0x80), rng)


def make_state(kind, rng):
    xmm = b""
    kind_lanes = kind & 0x0F
    for _ in range(4):
        if kind_lanes == F32:
            xmm += b"".join(pack_f32(f32(rng)) for _ in range(4))
        elif kind_lanes == F64:
            xmm += b"".join(struct.pack("<d", f64(rng)) for _ in range(2))
        else:
            if rng.random() < 0.3:
                xmm += b"".join(struct.pack("<i", rng.choice([0, 1, -1, 0x7FFFFFFF, -0x80000000, 255, 256, 65535, -32768])) for _ in range(4))
            else:
                xmm += bytes(rng.getrandbits(8) for _ in range(16))
    gpr = []
    for _ in range(3):
        choice = rng.random()
        if choice < 0.3:
            gpr.append(rng.choice([0, 1, 2, 0xFF, 0x80, 0xFFFF, 0x8000, 0xFFFFFFFF, 0x80000000, 0x7FFFFFFF,
                                   0xFFFFFFFFFFFFFFFF, 0x8000000000000000, 123456789]))
        elif choice < 0.6:
            gpr.append(rng.getrandbits(32) - (1 << 31) & 0xFFFFFFFFFFFFFFFF)
        elif choice < 0.75:
            gpr.append(rng.getrandbits(16))
        else:
            gpr.append(rng.getrandbits(64))
    if kind & 0x80:  # DIV seguro: rdx = 0 y divisor distinto de 0 (sin excepción #DE)
        gpr[1] = 0
        gpr[2] |= 1
    rcx = rng.choice([0, 1, 7, 31, 32, 33, 63, 64, 65, 255]) if rng.random() < 0.5 else rng.getrandbits(8)
    cf_in = rng.getrandbits(1)
    # rax, rdx, flags(0), r8, rcx, CF de entrada, relleno (64 + 64 = STATE)
    return xmm + struct.pack("<QQQQQQQQ", gpr[0], gpr[1], 0, gpr[2], rcx, cf_in, 0, 0)


# ---------------------------------------------------------------- ejecución x86 nativa (Windows)
kernel32 = ctypes.windll.kernel32
kernel32.VirtualAlloc.restype = ctypes.c_void_p
kernel32.VirtualAlloc.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint32, ctypes.c_uint32]


def make_function(code):
    mem = kernel32.VirtualAlloc(None, 4096, 0x3000, 0x40)  # MEM_COMMIT|RESERVE, PAGE_EXECUTE_READWRITE
    ctypes.memmove(mem, code, len(code))
    return ctypes.CFUNCTYPE(None, ctypes.c_void_p)(mem)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "jitdiff.bin"
    cases = int(sys.argv[2]) if len(sys.argv) > 2 else 200
    rng = random.Random(820)
    buf = ctypes.create_string_buffer(STATE + 32)
    base = (ctypes.addressof(buf) + 15) & ~15
    records = []
    for name, code, kind in INSTRUCTIONS:
        fn = make_function(PROLOGUE + code + EPILOGUE)
        for _ in range(cases):
            st = make_state_for(kind, rng)
            ctypes.memmove(base, st, STATE)
            fn(base)
            got = ctypes.string_at(base, STATE)
            records.append((name, code, kind, st, got))
    with open(out, "wb") as f:
        f.write(b"JDF2")
        f.write(struct.pack("<B", len(PROLOGUE)) + PROLOGUE + struct.pack("<B", len(EPILOGUE)) + EPILOGUE)
        f.write(struct.pack("<I", len(records)))
        for name, code, kind, st, got in records:
            nb = name.encode()
            f.write(struct.pack("<BB", len(nb), len(code)) + nb + code + struct.pack("<BB", kind, flag_mask(name)) + st + got)
    print(f"{len(INSTRUCTIONS)} instrucciones, {len(records)} casos -> {out}")


if __name__ == "__main__":
    main()
