#pragma once

// Decodificador de shaders DXBC (Shader Model 4.x / 5.0) a una representación intermedia.
//
// - Contenedor "DXBC": chunks ISGN/OSGN (firmas de entrada/salida) y SHDR/SHEX (programa).
// - Tokens de instrucción (opcode, longitud, saturate, test nz/z, opcodes extendidos) y de
//   operando (tipo, componentes, máscara/swizzle/select1, índices inmediatos y relativos,
//   modificadores neg/abs, inmediatos de 32 bits).
// - Declaraciones relevantes para ejecutar: temporales (indexables), constant buffers, buffer de
//   constantes inmediato, samplers, recursos (dimensión), entradas/salidas con su semántica de
//   sistema y modo de interpolación.
// Header-only: lo usan el rasterizador (DxbcVm) y la suite nativa con shaders reales del juego.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace hyperplay::dxbc {

enum Opcode : uint32_t {
    kAdd = 0, kAnd = 1, kBreak = 2, kBreakc = 3, kCall = 4, kCallc = 5, kCase = 6, kContinue = 7,
    kContinuec = 8, kCut = 9, kDefault = 10, kDerivRtx = 11, kDerivRty = 12, kDiscard = 13, kDiv = 14,
    kDp2 = 15, kDp3 = 16, kDp4 = 17, kElse = 18, kEmit = 19, kEmitThenCut = 20, kEndif = 21,
    kEndloop = 22, kEndswitch = 23, kEq = 24, kExp = 25, kFrc = 26, kFtoi = 27, kFtou = 28, kGe = 29,
    kIadd = 30, kIf = 31, kIeq = 32, kIge = 33, kIlt = 34, kImad = 35, kImax = 36, kImin = 37,
    kImul = 38, kIne = 39, kIneg = 40, kIshl = 41, kIshr = 42, kItof = 43, kLabel = 44, kLd = 45,
    kLdMs = 46, kLog = 47, kLoop = 48, kLt = 49, kMad = 50, kMin = 51, kMax = 52, kCustomData = 53,
    kMov = 54, kMovc = 55, kMul = 56, kNe = 57, kNop = 58, kNot = 59, kOr = 60, kResinfo = 61,
    kRet = 62, kRetc = 63, kRoundNe = 64, kRoundNi = 65, kRoundPi = 66, kRoundZ = 67, kRsq = 68,
    kSample = 69, kSampleC = 70, kSampleCLz = 71, kSampleL = 72, kSampleD = 73, kSampleB = 74,
    kSqrt = 75, kSwitch = 76, kSincos = 77, kUdiv = 78, kUlt = 79, kUge = 80, kUmul = 81, kUmad = 82,
    kUmax = 83, kUmin = 84, kUshr = 85, kUtof = 86, kXor = 87,
    kDclResource = 88, kDclConstantBuffer = 89, kDclSampler = 90, kDclIndexRange = 91,
    kDclGsOutputTopology = 92, kDclGsInputPrimitive = 93, kDclMaxOutputVertexCount = 94,
    kDclInput = 95, kDclInputSgv = 96, kDclInputSiv = 97, kDclInputPs = 98, kDclInputPsSgv = 99,
    kDclInputPsSiv = 100, kDclOutput = 101, kDclOutputSgv = 102, kDclOutputSiv = 103, kDclTemps = 104,
    kDclIndexableTemp = 105, kDclGlobalFlags = 106,
    // SM4.1
    kLod = 107, kGather4 = 108, kSamplePos = 109, kSampleInfo = 110,
    // SM5 (subconjunto que aparece en shaders de píxel/vértice comunes)
    kBufinfo = 120, kDerivRtxCoarse = 121, kDerivRtxFine = 122, kDerivRtyCoarse = 123,
    kDerivRtyFine = 124, kGather4C = 125, kGather4Po = 126, kGather4PoC = 127, kRcp = 128,
    kF32ToF16 = 129, kF16ToF32 = 130, kUaddc = 131, kUsubb = 132, kCountbits = 133,
    kFirstbitHi = 134, kFirstbitLo = 135, kFirstbitShi = 136, kUbfe = 137, kIbfe = 138, kBfi = 139,
    kBfrev = 140, kSwapc = 141,
    kDclStream = 142, kDclResourceRaw = 160, kDclResourceStructured = 161,
};

enum OperandType : uint32_t {
    kOpTemp = 0, kOpInput = 1, kOpOutput = 2, kOpIndexableTemp = 3, kOpImm32 = 4, kOpImm64 = 5,
    kOpSampler = 6, kOpResource = 7, kOpConstantBuffer = 8, kOpImmConstantBuffer = 9, kOpLabel = 10,
    kOpInputPrimitiveId = 11, kOpOutputDepth = 12, kOpNull = 13, kOpOutputCoverageMask = 15,
    kOpInputCoverageMask = 35, kOpOutputDepthGe = 38, kOpOutputDepthLe = 39,
};

enum ProgramType : uint32_t { kPixelShader = 0, kVertexShader = 1, kGeometryShader = 2 };

struct Operand {
    uint32_t type = kOpNull;
    uint32_t numComponents = 0;   // 0, 1 o 4
    uint32_t selMode = 0;         // 0 máscara, 1 swizzle, 2 select1
    uint32_t mask = 0;            // destino: bits xyzw
    uint8_t swizzle[4] = {0, 1, 2, 3};
    uint32_t indexDim = 0;
    uint32_t index[3] = {0, 0, 0};
    int relative[3] = {-1, -1, -1};  // índice en Program::relOperands (registro + inmediato)
    uint32_t imm[4] = {0, 0, 0, 0};
    bool neg = false, abs = false;
};

struct Instruction {
    uint32_t opcode = 0;
    bool saturate = false;
    bool testNonZero = false;      // if/breakc/discard/retc: _nz (true) o _z (false)
    uint32_t resourceDim = 0;      // bits 11..15 de dcl_resource / ld / sample
    int8_t texelOffset[3] = {0, 0, 0};
    std::vector<Operand> operands;
};

struct SignatureElement {
    std::string name;
    uint32_t semanticIndex = 0;
    uint32_t systemValue = 0;      // D3D_NAME (1 = SV_Position, 64 = SV_Target ...)
    uint32_t componentType = 0;    // 1 uint, 2 int, 3 float
    uint32_t reg = 0;
    uint8_t mask = 0, rwMask = 0;
};

struct InputDecl { uint32_t reg = 0; uint32_t mask = 0; uint32_t interpolation = 0; uint32_t systemValue = 0; };
struct OutputDecl { uint32_t reg = 0; uint32_t mask = 0; uint32_t systemValue = 0; };
struct ResourceDecl { uint32_t slot = 0; uint32_t dim = 0; uint32_t returnType = 0; };
struct ConstantBufferDecl { uint32_t slot = 0; uint32_t size = 0; };
struct IndexableTempDecl { uint32_t reg = 0; uint32_t count = 0; uint32_t components = 0; };

struct Program {
    uint32_t type = 0, major = 0, minor = 0;
    std::vector<SignatureElement> inputSig, outputSig;
    std::vector<Instruction> code;          // solo instrucciones ejecutables
    std::vector<Operand> relOperands;       // operandos de índice relativo
    std::vector<InputDecl> inputs;
    std::vector<OutputDecl> outputs;
    std::vector<ResourceDecl> resources;
    std::vector<uint32_t> samplers;
    std::vector<ConstantBufferDecl> constantBuffers;
    std::vector<IndexableTempDecl> indexableTemps;
    std::vector<uint32_t> immConstantBuffer;  // dwords (vec4 consecutivos)
    uint32_t tempCount = 0;
    std::string error;
    [[nodiscard]] bool ok() const noexcept { return error.empty() && !code.empty(); }
};

namespace detail {

inline uint32_t rd32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

inline bool parseSignature(const uint8_t* chunk, uint32_t size, std::vector<SignatureElement>& out) {
    if (size < 8) return false;
    const uint32_t count = rd32(chunk);
    const uint32_t offset = rd32(chunk + 4);
    if (offset + count * 24ull > size) return false;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* e = chunk + offset + i * 24;
        SignatureElement el;
        const uint32_t nameOff = rd32(e);
        if (nameOff < size) {
            const char* s = reinterpret_cast<const char*>(chunk + nameOff);
            size_t n = 0;
            while (nameOff + n < size && s[n] != '\0') ++n;
            el.name.assign(s, n);
        }
        el.semanticIndex = rd32(e + 4);
        el.systemValue = rd32(e + 8);
        el.componentType = rd32(e + 12);
        el.reg = rd32(e + 16);
        el.mask = e[20];
        el.rwMask = e[21];
        out.push_back(std::move(el));
    }
    return true;
}

class TokenReader {
public:
    TokenReader(const uint32_t* t, size_t n) : t_(t), n_(n) {}
    bool has(size_t k = 1) const { return pos_ + k <= n_; }
    uint32_t next() { return pos_ < n_ ? t_[pos_++] : 0; }
    size_t pos() const { return pos_; }
    void seek(size_t p) { pos_ = p < n_ ? p : n_; }

private:
    const uint32_t* t_;
    size_t n_;
    size_t pos_ = 0;
};

inline bool parseOperand(TokenReader& r, Program& prog, Operand& op, size_t end, int depth = 0) {
    if (!r.has() || r.pos() >= end || depth > 3) return false;
    const uint32_t tok = r.next();
    const uint32_t nc = tok & 3u;
    op.numComponents = nc == 0 ? 0 : nc == 1 ? 1 : 4;
    op.type = (tok >> 12) & 0xFFu;
    op.indexDim = (tok >> 20) & 3u;
    if (op.numComponents == 4) {
        op.selMode = (tok >> 2) & 3u;
        if (op.selMode == 0) {
            op.mask = (tok >> 4) & 0xFu;
        } else if (op.selMode == 1) {
            for (int i = 0; i < 4; ++i) op.swizzle[i] = static_cast<uint8_t>((tok >> (4 + 2 * i)) & 3u);
        } else {
            const uint8_t c = static_cast<uint8_t>((tok >> 4) & 3u);
            for (auto& s : op.swizzle) s = c;
        }
    } else if (op.numComponents == 1) {
        op.selMode = 2;
        op.mask = 1;
    }
    bool extended = (tok >> 31) != 0;
    while (extended) {
        const uint32_t ext = r.next();
        if ((ext & 0x3Fu) == 1) {  // modificador
            const uint32_t mod = (ext >> 6) & 0xFFu;
            op.neg = (mod & 1u) != 0;
            op.abs = (mod & 2u) != 0;
        }
        extended = (ext >> 31) != 0;
    }
    if (op.type == kOpImm32) {
        const int n = op.numComponents == 4 ? 4 : 1;
        for (int i = 0; i < n; ++i) op.imm[i] = r.next();
        if (n == 1) op.imm[1] = op.imm[2] = op.imm[3] = op.imm[0];
        return r.pos() <= end;
    }
    if (op.type == kOpImm64) {
        const int n = op.numComponents == 4 ? 4 : 2;
        for (int i = 0; i < n; ++i) r.next();
        return r.pos() <= end;
    }
    for (uint32_t d = 0; d < op.indexDim; ++d) {
        const uint32_t repr = (tok >> (22 + 3 * d)) & 7u;
        if (repr == 0 || repr == 3) {
            op.index[d] = r.next();
        } else if (repr == 1 || repr == 4) {
            op.index[d] = r.next();  // imm64: parte baja
            r.next();
        }
        if (repr == 2 || repr == 3 || repr == 4) {
            Operand rel;
            if (!parseOperand(r, prog, rel, end, depth + 1)) return false;
            op.relative[d] = static_cast<int>(prog.relOperands.size());
            prog.relOperands.push_back(rel);
        }
    }
    return r.pos() <= end;
}

inline bool isDeclaration(uint32_t opc) {
    return (opc >= kDclResource && opc <= kDclGlobalFlags) || opc == kCustomData ||
           (opc >= kDclStream && opc <= kDclResourceStructured);  // dcl_* de SM5 (142..161)
}

inline void parseProgram(const uint32_t* t, size_t n, Program& prog) {
    if (n < 2) {
        prog.error = "programa vacío";
        return;
    }
    const uint32_t ver = t[0];
    prog.type = ver >> 16;
    prog.major = (ver >> 4) & 0xFu;
    prog.minor = ver & 0xFu;
    const size_t total = std::min<size_t>(t[1], n);
    TokenReader r(t, total);
    r.seek(2);
    while (r.has()) {
        const size_t start = r.pos();
        const uint32_t tok = r.next();
        const uint32_t opc = tok & 0x7FFu;
        size_t len = (tok >> 24) & 0x7Fu;
        if (opc == kCustomData) {
            len = r.next();  // longitud total en dwords, incluido el token
            const uint32_t dataClass = tok >> 11;
            if (dataClass == 3 && len >= 2) {  // D3D10_SB_CUSTOMDATA_DCL_IMMEDIATE_CONSTANT_BUFFER
                for (size_t i = 2; i < len && r.has(); ++i) prog.immConstantBuffer.push_back(r.next());
            }
            r.seek(start + (len == 0 ? 2 : len));
            continue;
        }
        if (len == 0) {
            prog.error = "instrucción de longitud 0";
            return;
        }
        const size_t end = start + len;
        if (end > total) {
            prog.error = "instrucción truncada";
            return;
        }
        if (isDeclaration(opc)) {
            Operand op;
            switch (opc) {
                case kDclTemps:
                    prog.tempCount = r.next();
                    break;
                case kDclIndexableTemp: {
                    IndexableTempDecl d;
                    d.reg = r.next();
                    d.count = r.next();
                    d.components = r.next();
                    prog.indexableTemps.push_back(d);
                    break;
                }
                case kDclConstantBuffer:
                    if (parseOperand(r, prog, op, end)) prog.constantBuffers.push_back({op.index[0], op.index[1]});
                    break;
                case kDclSampler:
                    if (parseOperand(r, prog, op, end)) prog.samplers.push_back(op.index[0]);
                    break;
                case kDclResource:
                    if (parseOperand(r, prog, op, end)) {
                        prog.resources.push_back({op.index[0], (tok >> 11) & 0x1Fu, r.next()});
                    }
                    break;
                case kDclInput:
                case kDclInputSgv:
                case kDclInputSiv:
                case kDclInputPs:
                case kDclInputPsSgv:
                case kDclInputPsSiv:
                    if (parseOperand(r, prog, op, end)) {
                        InputDecl d;
                        d.reg = op.index[0];
                        d.mask = op.mask;
                        d.interpolation = (opc >= kDclInputPs) ? (tok >> 11) & 0xFu : 0;
                        if (opc == kDclInputSgv || opc == kDclInputSiv || opc == kDclInputPsSgv || opc == kDclInputPsSiv) {
                            d.systemValue = r.next();
                        }
                        if (op.type == kOpInput) prog.inputs.push_back(d);
                    }
                    break;
                case kDclOutput:
                case kDclOutputSgv:
                case kDclOutputSiv:
                    if (parseOperand(r, prog, op, end)) {
                        OutputDecl d;
                        d.reg = op.index[0];
                        d.mask = op.mask;
                        if (opc != kDclOutput) d.systemValue = r.next();
                        if (op.type == kOpOutput) prog.outputs.push_back(d);
                    }
                    break;
                default:
                    break;
            }
            r.seek(end);
            continue;
        }

        Instruction ins;
        ins.opcode = opc;
        ins.saturate = ((tok >> 13) & 1u) != 0;
        ins.testNonZero = ((tok >> 18) & 1u) != 0;
        ins.resourceDim = (tok >> 11) & 0x1Fu;
        bool extended = (tok >> 31) != 0;
        while (extended && r.pos() < end) {
            const uint32_t ext = r.next();
            if ((ext & 0x3Fu) == 1) {  // sample controls: desplazamiento de texel u, v, w (4 bits con signo)
                for (int i = 0; i < 3; ++i) {
                    int v = static_cast<int>((ext >> (9 + 4 * i)) & 0xFu);
                    if (v & 8) v -= 16;
                    ins.texelOffset[i] = static_cast<int8_t>(v);
                }
            }
            extended = (ext >> 31) != 0;
        }
        while (r.pos() < end) {
            Operand op;
            if (!parseOperand(r, prog, op, end)) {
                prog.error = "operando inválido en opcode " + std::to_string(opc);
                return;
            }
            ins.operands.push_back(op);
        }
        prog.code.push_back(std::move(ins));
        r.seek(end);
    }
}

}  // namespace detail

// Analiza un blob DXBC completo (lo que recibe ID3D11Device::Create*Shader).
inline Program parse(const uint8_t* data, size_t size) {
    Program prog;
    if (data == nullptr || size < 32 || std::memcmp(data, "DXBC", 4) != 0) {
        prog.error = "no es un contenedor DXBC";
        return prog;
    }
    const uint32_t chunkCount = detail::rd32(data + 28);
    if (32 + chunkCount * 4ull > size) {
        prog.error = "tabla de chunks truncada";
        return prog;
    }
    for (uint32_t i = 0; i < chunkCount; ++i) {
        const uint32_t off = detail::rd32(data + 32 + i * 4);
        if (off + 8ull > size) continue;
        const uint8_t* c = data + off;
        const uint32_t csize = detail::rd32(c + 4);
        if (off + 8ull + csize > size) continue;
        const uint8_t* body = c + 8;
        if (std::memcmp(c, "ISGN", 4) == 0) {
            detail::parseSignature(body, csize, prog.inputSig);
        } else if (std::memcmp(c, "OSGN", 4) == 0 || std::memcmp(c, "OSG5", 4) == 0) {
            detail::parseSignature(body, csize, prog.outputSig);
        } else if (std::memcmp(c, "SHDR", 4) == 0 || std::memcmp(c, "SHEX", 4) == 0) {
            detail::parseProgram(reinterpret_cast<const uint32_t*>(body), csize / 4, prog);
        }
    }
    if (prog.code.empty() && prog.error.empty()) prog.error = "sin chunk SHDR/SHEX";
    return prog;
}

}  // namespace hyperplay::dxbc
