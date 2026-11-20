#pragma once

// Intérprete de programas DXBC (ver DxbcShader.h) para el rasterizador por software.
//
// Una invocación = un vértice o un píxel. Registros vec4 de 32 bits (float/int/uint según la
// instrucción), swizzle/máscara/select1, modificadores neg/abs y saturate. Control de flujo
// estructurado (if/else/endif, loop/endloop, break(c)/continue(c), switch/case/default, ret(c),
// discard) con saltos precalculados. Las texturas se leen a través de TextureAccess.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "DxbcShader.h"

namespace hyperplay::dxbc {

struct Vec4 {
    union {
        float f[4];
        uint32_t u[4];
        int32_t i[4];
    };
    Vec4() : u{0, 0, 0, 0} {}
};

// Muestreo y carga de texturas (implementado por el rasterizador sobre los recursos D3D11).
class TextureAccess {
public:
    virtual ~TextureAccess() = default;
    // sample/sample_l/sample_b: coordenadas normalizadas, lod explícito o sesgo.
    virtual Vec4 sample(uint32_t texSlot, uint32_t samplerSlot, const float* coord, float lod, bool explicitLod,
                        const int8_t* offset) = 0;
    // ld: coordenadas enteras (xyz) y mip en w.
    virtual Vec4 load(uint32_t texSlot, const int32_t* coord, const int8_t* offset) = 0;
    // resinfo: ancho, alto, profundidad/array, número de mips.
    virtual Vec4 info(uint32_t texSlot, uint32_t mip) = 0;
};

struct ConstantBufferView {
    const uint8_t* data = nullptr;
    uint32_t sizeBytes = 0;
};

class Vm {
public:
    explicit Vm(const Program& prog) : prog_(prog) {
        temps_.resize(std::max<uint32_t>(prog.tempCount, 1));
        for (const auto& it : prog.indexableTemps) {
            if (it.reg >= indexable_.size()) indexable_.resize(it.reg + 1);
            indexable_[it.reg].resize(it.count);
        }
        buildJumps();
    }

    Vec4 inputs[32];
    Vec4 outputs[16];
    ConstantBufferView cbuffers[14];
    TextureAccess* textures = nullptr;
    bool discarded = false;
    float outputDepth = 0.0f;
    bool wroteDepth = false;

    // Ejecuta el programa completo. false si se excedió el presupuesto de instrucciones (bucle).
    bool run() {
        discarded = false;
        wroteDepth = false;
        size_t pc = 0;
        uint32_t budget = 1u << 20;
        while (pc < prog_.code.size()) {
            if (--budget == 0) return false;
            const Instruction& ins = prog_.code[pc];
            size_t next = pc + 1;
            switch (ins.opcode) {
                case kRet:
                    return true;
                case kRetc:
                    if (cond(ins)) return true;
                    break;
                case kDiscard:
                    if (cond(ins)) {
                        discarded = true;
                        return true;
                    }
                    break;
                case kIf:
                    if (!cond(ins)) next = jump_[pc] + 1;  // a else (+1) o endif (+1)
                    break;
                case kElse:
                    next = jump_[pc] + 1;  // fin del bloque then: saltar a endif
                    break;
                case kEndif:
                    break;
                case kLoop:
                    break;
                case kEndloop:
                    next = jump_[pc] + 1;  // volver al cuerpo
                    break;
                case kBreak:
                    next = breakTarget(pc);
                    break;
                case kBreakc:
                    if (cond(ins)) next = breakTarget(pc);
                    break;
                case kContinue:
                    next = continueTarget(pc);
                    break;
                case kContinuec:
                    if (cond(ins)) next = continueTarget(pc);
                    break;
                case kSwitch:
                    next = switchTarget(pc, src(ins.operands[0]).u[0]);
                    break;
                case kCase:
                case kDefault:
                case kEndswitch:
                    break;
                default:
                    exec(ins);
                    break;
            }
            pc = next;
        }
        return true;
    }

private:
    const Program& prog_;
    std::vector<Vec4> temps_;
    std::vector<std::vector<Vec4>> indexable_;
    std::vector<size_t> jump_;       // if->else/endif, else->endif, loop->endloop, endloop->loop

    void buildJumps() {
        jump_.assign(prog_.code.size(), 0);
        std::vector<size_t> ifs, loops, switches;
        for (size_t i = 0; i < prog_.code.size(); ++i) {
            switch (prog_.code[i].opcode) {
                case kIf: ifs.push_back(i); break;
                case kElse:
                    if (!ifs.empty()) {
                        jump_[ifs.back()] = i;
                        ifs.back() = i;
                    }
                    break;
                case kEndif:
                    if (!ifs.empty()) {
                        jump_[ifs.back()] = i;
                        ifs.pop_back();
                    }
                    break;
                case kLoop: loops.push_back(i); break;
                case kEndloop:
                    if (!loops.empty()) {
                        jump_[loops.back()] = i;
                        jump_[i] = loops.back();
                        loops.pop_back();
                    }
                    break;
                case kSwitch: switches.push_back(i); break;
                case kEndswitch:
                    if (!switches.empty()) {
                        jump_[switches.back()] = i;
                        switches.pop_back();
                    }
                    break;
                default: break;
            }
        }
    }

    size_t innermostBreakable(size_t pc) const {
        // bucle o switch abierto más interno que contiene pc
        size_t best = SIZE_MAX, bestStart = 0;
        for (size_t s = 0; s < prog_.code.size(); ++s) {
            const uint32_t op = prog_.code[s].opcode;
            if ((op == kLoop || op == kSwitch) && s < pc && jump_[s] > pc && s >= bestStart) {
                best = s;
                bestStart = s;
            }
        }
        return best;
    }
    size_t breakTarget(size_t pc) const {
        const size_t s = innermostBreakable(pc);
        return s == SIZE_MAX ? prog_.code.size() : jump_[s] + 1;
    }
    size_t continueTarget(size_t pc) const {
        for (size_t s = pc; s-- > 0;) {
            if (prog_.code[s].opcode == kLoop && jump_[s] > pc) return s + 1;
        }
        return prog_.code.size();
    }
    size_t switchTarget(size_t pc, uint32_t value) {
        const size_t end = jump_[pc];
        size_t def = end;
        int depth = 0;
        for (size_t i = pc + 1; i < end; ++i) {
            const Instruction& c = prog_.code[i];
            if (c.opcode == kSwitch) ++depth;
            if (c.opcode == kEndswitch) --depth;
            if (depth != 0) continue;
            if (c.opcode == kCase && !c.operands.empty() && c.operands[0].imm[0] == value) return i + 1;
            if (c.opcode == kDefault) def = i + 1;
        }
        return def;
    }

    bool cond(const Instruction& ins) {
        if (ins.operands.empty()) return true;
        const bool nz = src(ins.operands[0]).u[0] != 0;
        return ins.testNonZero ? nz : !nz;
    }

    uint32_t resolveIndex(const Operand& op, int d) {
        uint32_t idx = op.index[d];
        if (op.relative[d] >= 0) idx += src(prog_.relOperands[static_cast<size_t>(op.relative[d])]).u[0];
        return idx;
    }

    Vec4 readRaw(const Operand& op) {
        Vec4 v;
        switch (op.type) {
            case kOpTemp: {
                const uint32_t r = resolveIndex(op, 0);
                if (r < temps_.size()) v = temps_[r];
                break;
            }
            case kOpInput: {
                const uint32_t r = resolveIndex(op, 0);
                if (r < 32) v = inputs[r];
                break;
            }
            case kOpOutput: {
                const uint32_t r = resolveIndex(op, 0);
                if (r < 16) v = outputs[r];
                break;
            }
            case kOpIndexableTemp: {
                const uint32_t r = op.index[0];
                const uint32_t e = resolveIndex(op, 1);
                if (r < indexable_.size() && e < indexable_[r].size()) v = indexable_[r][e];
                break;
            }
            case kOpImm32:
                std::memcpy(v.u, op.imm, 16);
                break;
            case kOpConstantBuffer: {
                const uint32_t slot = op.index[0];
                const uint32_t elem = resolveIndex(op, 1);
                if (slot < 14 && cbuffers[slot].data != nullptr && (elem + 1) * 16u <= cbuffers[slot].sizeBytes) {
                    std::memcpy(v.u, cbuffers[slot].data + elem * 16u, 16);
                }
                break;
            }
            case kOpImmConstantBuffer: {
                const uint32_t elem = resolveIndex(op, 0);
                if ((elem + 1) * 4u <= prog_.immConstantBuffer.size()) {
                    std::memcpy(v.u, prog_.immConstantBuffer.data() + elem * 4u, 16);
                }
                break;
            }
            default:
                break;
        }
        return v;
    }

    // Fuente con swizzle; floatMods aplica neg/abs como float (si no, enteros).
    Vec4 src(const Operand& op, bool floatMods = true) {
        const Vec4 raw = readRaw(op);
        Vec4 v;
        if (op.type == kOpImm32) {
            v = raw;
        } else {
            for (int c = 0; c < 4; ++c) v.u[c] = raw.u[op.swizzle[c] & 3];
        }
        if (op.abs || op.neg) {
            for (int c = 0; c < 4; ++c) {
                if (floatMods) {
                    if (op.abs) v.f[c] = std::fabs(v.f[c]);
                    if (op.neg) v.f[c] = -v.f[c];
                } else {
                    if (op.abs) v.i[c] = v.i[c] < 0 ? -v.i[c] : v.i[c];
                    if (op.neg) v.i[c] = -v.i[c];
                }
            }
        }
        return v;
    }

    Vec4* destPtr(const Operand& op) {
        switch (op.type) {
            case kOpTemp: {
                const uint32_t r = resolveIndex(op, 0);
                if (r >= temps_.size()) temps_.resize(r + 1);
                return &temps_[r];
            }
            case kOpOutput: {
                const uint32_t r = resolveIndex(op, 0);
                return r < 16 ? &outputs[r] : nullptr;
            }
            case kOpIndexableTemp: {
                const uint32_t r = op.index[0];
                const uint32_t e = resolveIndex(op, 1);
                if (r < indexable_.size() && e < indexable_[r].size()) return &indexable_[r][e];
                return nullptr;
            }
            default:
                return nullptr;
        }
    }

    void write(const Operand& op, const Vec4& value, bool saturate, bool isFloat = true) {
        if (op.type == kOpOutputDepth || op.type == kOpOutputDepthGe || op.type == kOpOutputDepthLe) {
            outputDepth = saturate ? std::fmin(std::fmax(value.f[0], 0.0f), 1.0f) : value.f[0];
            wroteDepth = true;
            return;
        }
        Vec4* d = destPtr(op);
        if (d == nullptr) return;
        const uint32_t mask = op.numComponents == 4 ? (op.selMode == 0 ? op.mask : 0xF) : 1;
        for (int c = 0; c < 4; ++c) {
            if ((mask >> c) & 1u) {
                if (saturate && isFloat) {
                    float f = value.f[c];
                    f = std::isnan(f) ? 0.0f : std::fmin(std::fmax(f, 0.0f), 1.0f);
                    d->f[c] = f;
                } else {
                    d->u[c] = value.u[c];
                }
            }
        }
    }

    // Componentes escalares (select1) toman el valor del primer componente de la máscara.
    template <typename F>
    void unaryF(const Instruction& ins, F fn) {
        const Vec4 a = src(ins.operands[1]);
        Vec4 r;
        for (int c = 0; c < 4; ++c) r.f[c] = fn(a.f[c]);
        write(ins.operands[0], r, ins.saturate);
    }
    template <typename F>
    void binaryF(const Instruction& ins, F fn) {
        const Vec4 a = src(ins.operands[1]), b = src(ins.operands[2]);
        Vec4 r;
        for (int c = 0; c < 4; ++c) r.f[c] = fn(a.f[c], b.f[c]);
        write(ins.operands[0], r, ins.saturate);
    }
    template <typename F>
    void binaryU(const Instruction& ins, F fn) {
        const Vec4 a = src(ins.operands[1], false), b = src(ins.operands[2], false);
        Vec4 r;
        for (int c = 0; c < 4; ++c) r.u[c] = fn(a.u[c], b.u[c]);
        write(ins.operands[0], r, false, false);
    }
    template <typename F>
    void compareF(const Instruction& ins, F fn) {
        const Vec4 a = src(ins.operands[1]), b = src(ins.operands[2]);
        Vec4 r;
        for (int c = 0; c < 4; ++c) r.u[c] = fn(a.f[c], b.f[c]) ? 0xFFFFFFFFu : 0u;
        write(ins.operands[0], r, false, false);
    }
    template <typename F>
    void compareI(const Instruction& ins, F fn) {
        const Vec4 a = src(ins.operands[1], false), b = src(ins.operands[2], false);
        Vec4 r;
        for (int c = 0; c < 4; ++c) r.u[c] = fn(a.i[c], b.i[c], a.u[c], b.u[c]) ? 0xFFFFFFFFu : 0u;
        write(ins.operands[0], r, false, false);
    }

    static float frcf(float x) { return x - std::floor(x); }
    static int32_t ftoiSat(float f) {
        if (std::isnan(f)) return 0;
        if (f >= 2147483647.0f) return 2147483647;
        if (f <= -2147483648.0f) return INT32_MIN;
        return static_cast<int32_t>(f);
    }
    static uint32_t ftouSat(float f) {
        if (std::isnan(f) || f <= 0.0f) return 0;
        if (f >= 4294967295.0f) return 0xFFFFFFFFu;
        return static_cast<uint32_t>(f);
    }

    void dot(const Instruction& ins, int n) {
        const Vec4 a = src(ins.operands[1]), b = src(ins.operands[2]);
        float s = 0.0f;
        for (int c = 0; c < n; ++c) s += a.f[c] * b.f[c];
        Vec4 r;
        r.f[0] = r.f[1] = r.f[2] = r.f[3] = s;
        write(ins.operands[0], r, ins.saturate);
    }

    void exec(const Instruction& ins) {
        const auto& ops = ins.operands;
        switch (ins.opcode) {
            case kMov: {
                write(ops[0], src(ops[1]), ins.saturate);
                break;
            }
            case kMovc: {
                const Vec4 c = src(ops[1], false), a = src(ops[2]), b = src(ops[3]);
                Vec4 r;
                for (int i = 0; i < 4; ++i) r.u[i] = c.u[i] != 0 ? a.u[i] : b.u[i];
                write(ops[0], r, ins.saturate);
                break;
            }
            case kAdd: binaryF(ins, [](float a, float b) { return a + b; }); break;
            case kMul: binaryF(ins, [](float a, float b) { return a * b; }); break;
            case kDiv: binaryF(ins, [](float a, float b) { return a / b; }); break;
            case kMin: binaryF(ins, [](float a, float b) { return std::fmin(a, b); }); break;
            case kMax: binaryF(ins, [](float a, float b) { return std::fmax(a, b); }); break;
            case kMad: {
                const Vec4 a = src(ops[1]), b = src(ops[2]), c = src(ops[3]);
                Vec4 r;
                for (int i = 0; i < 4; ++i) r.f[i] = a.f[i] * b.f[i] + c.f[i];
                write(ops[0], r, ins.saturate);
                break;
            }
            case kDp2: dot(ins, 2); break;
            case kDp3: dot(ins, 3); break;
            case kDp4: dot(ins, 4); break;
            case kRsq: unaryF(ins, [](float a) { return 1.0f / std::sqrt(a); }); break;
            case kSqrt: unaryF(ins, [](float a) { return std::sqrt(a); }); break;
            case kRcp: unaryF(ins, [](float a) { return 1.0f / a; }); break;
            case kExp: unaryF(ins, [](float a) { return std::exp2(a); }); break;
            case kLog: unaryF(ins, [](float a) { return std::log2(a); }); break;
            case kFrc: unaryF(ins, frcf); break;
            case kRoundNe: unaryF(ins, [](float a) { return std::nearbyint(a); }); break;
            case kRoundNi: unaryF(ins, [](float a) { return std::floor(a); }); break;
            case kRoundPi: unaryF(ins, [](float a) { return std::ceil(a); }); break;
            case kRoundZ: unaryF(ins, [](float a) { return std::trunc(a); }); break;
            case kSincos: {
                const Vec4 a = src(ops[2]);
                Vec4 s, c;
                for (int i = 0; i < 4; ++i) {
                    s.f[i] = std::sin(a.f[i]);
                    c.f[i] = std::cos(a.f[i]);
                }
                if (ops[0].type != kOpNull) write(ops[0], s, ins.saturate);
                if (ops[1].type != kOpNull) write(ops[1], c, ins.saturate);
                break;
            }
            case kDerivRtx:
            case kDerivRty:
            case kDerivRtxCoarse:
            case kDerivRtxFine:
            case kDerivRtyCoarse:
            case kDerivRtyFine: {
                Vec4 z;  // invocaciones escalares: sin cuádruplos de píxeles, derivada 0
                write(ops[0], z, ins.saturate);
                break;
            }
            case kEq: compareF(ins, [](float a, float b) { return a == b; }); break;
            case kNe: compareF(ins, [](float a, float b) { return a != b; }); break;
            case kLt: compareF(ins, [](float a, float b) { return a < b; }); break;
            case kGe: compareF(ins, [](float a, float b) { return a >= b; }); break;
            case kIeq: compareI(ins, [](int32_t a, int32_t b, uint32_t, uint32_t) { return a == b; }); break;
            case kIne: compareI(ins, [](int32_t a, int32_t b, uint32_t, uint32_t) { return a != b; }); break;
            case kIlt: compareI(ins, [](int32_t a, int32_t b, uint32_t, uint32_t) { return a < b; }); break;
            case kIge: compareI(ins, [](int32_t a, int32_t b, uint32_t, uint32_t) { return a >= b; }); break;
            case kUlt: compareI(ins, [](int32_t, int32_t, uint32_t a, uint32_t b) { return a < b; }); break;
            case kUge: compareI(ins, [](int32_t, int32_t, uint32_t a, uint32_t b) { return a >= b; }); break;
            case kAnd: binaryU(ins, [](uint32_t a, uint32_t b) { return a & b; }); break;
            case kOr: binaryU(ins, [](uint32_t a, uint32_t b) { return a | b; }); break;
            case kXor: binaryU(ins, [](uint32_t a, uint32_t b) { return a ^ b; }); break;
            case kIadd: binaryU(ins, [](uint32_t a, uint32_t b) { return a + b; }); break;
            case kIshl: binaryU(ins, [](uint32_t a, uint32_t b) { return a << (b & 31u); }); break;
            case kUshr: binaryU(ins, [](uint32_t a, uint32_t b) { return a >> (b & 31u); }); break;
            case kIshr:
                binaryU(ins, [](uint32_t a, uint32_t b) {
                    return static_cast<uint32_t>(static_cast<int32_t>(a) >> (b & 31u));
                });
                break;
            case kImin:
                binaryU(ins, [](uint32_t a, uint32_t b) {
                    return static_cast<int32_t>(a) < static_cast<int32_t>(b) ? a : b;
                });
                break;
            case kImax:
                binaryU(ins, [](uint32_t a, uint32_t b) {
                    return static_cast<int32_t>(a) > static_cast<int32_t>(b) ? a : b;
                });
                break;
            case kUmin: binaryU(ins, [](uint32_t a, uint32_t b) { return a < b ? a : b; }); break;
            case kUmax: binaryU(ins, [](uint32_t a, uint32_t b) { return a > b ? a : b; }); break;
            case kUdiv: {  // udiv dstQ, dstR, a, b
                const Vec4 a = src(ops[2], false), b = src(ops[3], false);
                Vec4 q, r;
                for (int i = 0; i < 4; ++i) {
                    q.u[i] = b.u[i] ? a.u[i] / b.u[i] : 0xFFFFFFFFu;
                    r.u[i] = b.u[i] ? a.u[i] % b.u[i] : 0xFFFFFFFFu;
                }
                if (ops[0].type != kOpNull) write(ops[0], q, false, false);
                if (ops[1].type != kOpNull) write(ops[1], r, false, false);
                break;
            }
            case kImul:
            case kUmul: {  // [i|u]mul dstHi, dstLo, a, b
                const Vec4 a = src(ops[2], false), b = src(ops[3], false);
                Vec4 hi, lo;
                for (int i = 0; i < 4; ++i) {
                    if (ins.opcode == kImul) {
                        const int64_t p = static_cast<int64_t>(a.i[i]) * b.i[i];
                        lo.u[i] = static_cast<uint32_t>(p);
                        hi.u[i] = static_cast<uint32_t>(static_cast<uint64_t>(p) >> 32);
                    } else {
                        const uint64_t p = static_cast<uint64_t>(a.u[i]) * b.u[i];
                        lo.u[i] = static_cast<uint32_t>(p);
                        hi.u[i] = static_cast<uint32_t>(p >> 32);
                    }
                }
                if (ops[0].type != kOpNull) write(ops[0], hi, false, false);
                if (ops[1].type != kOpNull) write(ops[1], lo, false, false);
                break;
            }
            case kImad:
            case kUmad: {
                const Vec4 a = src(ops[1], false), b = src(ops[2], false), c = src(ops[3], false);
                Vec4 r;
                for (int i = 0; i < 4; ++i) r.u[i] = a.u[i] * b.u[i] + c.u[i];
                write(ops[0], r, false, false);
                break;
            }
            case kIneg: {
                const Vec4 a = src(ops[1], false);
                Vec4 r;
                for (int i = 0; i < 4; ++i) r.i[i] = -a.i[i];
                write(ops[0], r, false, false);
                break;
            }
            case kNot: {
                const Vec4 a = src(ops[1], false);
                Vec4 r;
                for (int i = 0; i < 4; ++i) r.u[i] = ~a.u[i];
                write(ops[0], r, false, false);
                break;
            }
            case kItof: {
                const Vec4 a = src(ops[1], false);
                Vec4 r;
                for (int i = 0; i < 4; ++i) r.f[i] = static_cast<float>(a.i[i]);
                write(ops[0], r, ins.saturate);
                break;
            }
            case kUtof: {
                const Vec4 a = src(ops[1], false);
                Vec4 r;
                for (int i = 0; i < 4; ++i) r.f[i] = static_cast<float>(a.u[i]);
                write(ops[0], r, ins.saturate);
                break;
            }
            case kFtoi: {
                const Vec4 a = src(ops[1]);
                Vec4 r;
                for (int i = 0; i < 4; ++i) r.i[i] = ftoiSat(a.f[i]);
                write(ops[0], r, false, false);
                break;
            }
            case kFtou: {
                const Vec4 a = src(ops[1]);
                Vec4 r;
                for (int i = 0; i < 4; ++i) r.u[i] = ftouSat(a.f[i]);
                write(ops[0], r, false, false);
                break;
            }
            case kCountbits: {
                const Vec4 a = src(ops[1], false);
                Vec4 r;
                for (int i = 0; i < 4; ++i) r.u[i] = static_cast<uint32_t>(__builtin_popcount(a.u[i]));
                write(ops[0], r, false, false);
                break;
            }
            case kSample:
            case kSampleL:
            case kSampleB:
            case kSampleC:
            case kSampleCLz:
            case kSampleD: {
                // sample dst, coord, t#, s# [, lod/bias/ref]
                const Vec4 coord = src(ops[1]);
                const Operand& tex = ops[2];
                float lod = 0.0f;
                bool explicitLod = ins.opcode == kSampleL || ins.opcode == kSampleCLz;
                if ((ins.opcode == kSampleL || ins.opcode == kSampleB) && ops.size() > 4) lod = src(ops[4]).f[0];
                Vec4 texel;
                if (textures != nullptr) {
                    texel = textures->sample(tex.index[0], ops[3].index[0], coord.f, lod, explicitLod, ins.texelOffset);
                }
                if (ins.opcode == kSampleC || ins.opcode == kSampleCLz) {  // comparación: 1/0
                    const float ref = ops.size() > 4 ? src(ops[4]).f[0] : 0.0f;
                    const float t = ref <= texel.f[tex.swizzle[0] & 3] ? 1.0f : 0.0f;
                    Vec4 r;
                    r.f[0] = r.f[1] = r.f[2] = r.f[3] = t;
                    write(ops[0], r, ins.saturate);
                    break;
                }
                writeSwizzled(ops[0], texel, tex, ins.saturate);
                break;
            }
            case kLd: {
                const Vec4 coord = src(ops[1], false);
                const Operand& tex = ops[2];
                Vec4 texel;
                if (textures != nullptr) texel = textures->load(tex.index[0], coord.i, ins.texelOffset);
                writeSwizzled(ops[0], texel, tex, ins.saturate);
                break;
            }
            case kResinfo: {
                const Vec4 mip = src(ops[1], false);
                const Operand& tex = ops[2];
                Vec4 info;
                if (textures != nullptr) info = textures->info(tex.index[0], mip.u[0]);
                // modo por defecto (float): valores como float
                writeSwizzled(ops[0], info, tex, false);
                break;
            }
            case kNop:
            case kLabel:
            default:
                break;
        }
    }

    // El swizzle del operando de recurso selecciona los canales del texel.
    void writeSwizzled(const Operand& dst, const Vec4& texel, const Operand& tex, bool saturate) {
        Vec4 r;
        for (int c = 0; c < 4; ++c) r.u[c] = texel.u[tex.swizzle[c] & 3];
        write(dst, r, saturate);
    }
};

}  // namespace hyperplay::dxbc
