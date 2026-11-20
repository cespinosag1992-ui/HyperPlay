#pragma once

// Constructor mínimo de módulos SPIR-V 1.0 (shaders gráficos para Vulkan 1.1).
//
// Mantiene las secciones en el orden lógico que exige la especificación (capacidades, import de
// GLSL.std.450, modelo de memoria, puntos de entrada, modos, decoraciones, tipos/constantes/
// variables globales y funciones) y deduplica tipos y constantes. Sin dependencias externas.

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace hyperplay::spv {

// Opcodes (SPIR-V 1.0) usados por el traductor.
enum Op : uint32_t {
    OpUndef = 1, OpExtInstImport = 11, OpExtInst = 12, OpMemoryModel = 14, OpEntryPoint = 15,
    OpExecutionMode = 16, OpCapability = 17, OpTypeVoid = 19, OpTypeBool = 20, OpTypeInt = 21,
    OpTypeFloat = 22, OpTypeVector = 23, OpTypeImage = 25, OpTypeSampler = 26, OpTypeSampledImage = 27,
    OpTypeArray = 28, OpTypeStruct = 30, OpTypePointer = 32, OpTypeFunction = 33, OpConstantTrue = 41,
    OpConstantFalse = 42, OpConstant = 43, OpConstantComposite = 44, OpFunction = 54, OpFunctionEnd = 56,
    OpVariable = 59, OpLoad = 61, OpStore = 62, OpAccessChain = 65, OpDecorate = 71, OpMemberDecorate = 72,
    OpVectorShuffle = 79, OpCompositeConstruct = 80, OpCompositeExtract = 81, OpSampledImage = 86,
    OpImageSampleImplicitLod = 87, OpImageSampleExplicitLod = 88, OpImageSampleDrefImplicitLod = 89,
    OpImageSampleDrefExplicitLod = 90, OpImageFetch = 95, OpImageGather = 96, OpImageDrefGather = 97,
    OpImage = 100, OpImageQuerySizeLod = 103, OpImageQueryLod = 105, OpImageQueryLevels = 106,
    OpConvertFToU = 109, OpConvertFToS = 110, OpConvertSToF = 111, OpConvertUToF = 112, OpBitcast = 124,
    OpSNegate = 126, OpFNegate = 127, OpIAdd = 128, OpFAdd = 129, OpISub = 130, OpFSub = 131, OpIMul = 132,
    OpFMul = 133, OpUDiv = 134, OpFDiv = 136, OpUMod = 137, OpDot = 148, OpUMulExtended = 151,
    OpSMulExtended = 152, OpSelect = 169, OpIEqual = 170, OpINotEqual = 171, OpSGreaterThanEqual = 175,
    OpUGreaterThanEqual = 174, OpULessThan = 176, OpSLessThan = 177, OpFOrdEqual = 180,
    OpFUnordNotEqual = 183, OpFOrdLessThan = 184, OpFOrdGreaterThanEqual = 190, OpShiftRightLogical = 194,
    OpShiftRightArithmetic = 195, OpShiftLeftLogical = 196, OpBitwiseOr = 197, OpBitwiseXor = 198,
    OpBitwiseAnd = 199, OpNot = 200, OpBitFieldInsert = 201, OpBitFieldSExtract = 202,
    OpBitFieldUExtract = 203, OpBitReverse = 204, OpBitCount = 205, OpDPdx = 207, OpDPdy = 208,
    OpDPdxFine = 210, OpDPdyFine = 211, OpDPdxCoarse = 213, OpDPdyCoarse = 214, OpLoopMerge = 246,
    OpSelectionMerge = 247, OpLabel = 248, OpBranch = 249, OpBranchConditional = 250, OpSwitch = 251,
    OpKill = 252, OpReturn = 253,
};

// GLSL.std.450
enum Glsl : uint32_t {
    GlslRoundEven = 2, GlslTrunc = 3, GlslFAbs = 4, GlslSAbs = 5, GlslFloor = 8, GlslCeil = 9, GlslFract = 10,
    GlslSin = 13, GlslCos = 14, GlslExp2 = 29, GlslLog2 = 30, GlslSqrt = 31, GlslInverseSqrt = 32,
    GlslUMin = 38, GlslSMin = 39, GlslUMax = 41, GlslSMax = 42, GlslPackHalf2x16 = 58,
    GlslUnpackHalf2x16 = 62, GlslFindILsb = 73, GlslFindSMsb = 74, GlslFindUMsb = 75, GlslNMin = 79,
    GlslNMax = 80, GlslNClamp = 81,
};

enum StorageClass : uint32_t { ScUniformConstant = 0, ScInput = 1, ScUniform = 2, ScOutput = 3, ScPrivate = 6, ScFunction = 7 };
enum Decoration : uint32_t {
    DecBlock = 2, DecArrayStride = 6, DecBuiltIn = 11, DecNoPerspective = 13, DecFlat = 14, DecCentroid = 16,
    DecSample = 17, DecLocation = 30, DecBinding = 33, DecDescriptorSet = 34, DecOffset = 35,
};
enum BuiltIn : uint32_t {
    BiPosition = 0, BiFragCoord = 15, BiFrontFacing = 17, BiSampleId = 18, BiFragDepth = 22,
    BiVertexIndex = 42, BiInstanceIndex = 43,
};
enum Dim : uint32_t { Dim1D = 0, Dim2D = 1, Dim3D = 2, DimCube = 3 };
enum ImageOperands : uint32_t { ImBias = 0x1, ImLod = 0x2, ImGrad = 0x4, ImConstOffset = 0x8 };

class Builder {
public:
    uint32_t newId() { return bound_++; }

    // --- tipos (deduplicados) -------------------------------------------------------------
    uint32_t typeVoid() { return type({OpTypeVoid}); }
    uint32_t typeBool() { return type({OpTypeBool}); }
    uint32_t typeInt(bool isSigned) { return type({OpTypeInt, 32, isSigned ? 1u : 0u}); }
    uint32_t typeFloat() { return type({OpTypeFloat, 32}); }
    uint32_t typeVector(uint32_t comp, uint32_t n) { return type({OpTypeVector, comp, n}); }
    uint32_t typePointer(uint32_t sc, uint32_t t) { return type({OpTypePointer, sc, t}); }
    uint32_t typeFunction(uint32_t ret) { return type({OpTypeFunction, ret}); }
    uint32_t typeArray(uint32_t elem, uint32_t length) { return type({OpTypeArray, elem, constU(length)}); }
    // Array no deduplicado (para tipos con decoraciones de layout propias, p. ej. ArrayStride).
    uint32_t typeArrayUnique(uint32_t elem, uint32_t length) {
        const uint32_t len = constU(length);
        const uint32_t id = newId();
        emit(types_, OpTypeArray, {id, elem, len});
        return id;
    }
    uint32_t typeSampler() { return type({OpTypeSampler}); }
    uint32_t typeImage(uint32_t sampledType, uint32_t dim, uint32_t depth, bool arrayed, uint32_t sampled) {
        return type({OpTypeImage, sampledType, dim, depth, arrayed ? 1u : 0u, 0u, sampled, 0u});
    }
    uint32_t typeSampledImage(uint32_t image) { return type({OpTypeSampledImage, image}); }
    // Structs no se deduplican (cada uno lleva sus propias decoraciones).
    uint32_t typeStruct(const std::vector<uint32_t>& members) {
        const uint32_t id = newId();
        std::vector<uint32_t> w{id};
        w.insert(w.end(), members.begin(), members.end());
        emit(types_, OpTypeStruct, w);
        return id;
    }

    // --- constantes --------------------------------------------------------------------
    uint32_t constU(uint32_t v) { return constant(typeInt(false), v); }
    uint32_t constI(int32_t v) { return constant(typeInt(true), static_cast<uint32_t>(v)); }
    uint32_t constF(float f) {
        uint32_t v;
        std::memcpy(&v, &f, 4);
        return constant(typeFloat(), v);
    }
    uint32_t constBool(bool b) {
        const uint32_t t = typeBool();
        const std::vector<uint32_t> key{b ? OpConstantTrue : OpConstantFalse, t};
        auto it = dedup_.find(key);
        if (it != dedup_.end()) return it->second;
        const uint32_t id = newId();
        emit(types_, b ? OpConstantTrue : OpConstantFalse, {t, id});
        dedup_[key] = id;
        return id;
    }
    uint32_t constComposite(uint32_t t, const std::vector<uint32_t>& parts) {
        std::vector<uint32_t> key{OpConstantComposite, t};
        key.insert(key.end(), parts.begin(), parts.end());
        auto it = dedup_.find(key);
        if (it != dedup_.end()) return it->second;
        const uint32_t id = newId();
        std::vector<uint32_t> w{t, id};
        w.insert(w.end(), parts.begin(), parts.end());
        emit(types_, OpConstantComposite, w);
        dedup_[key] = id;
        return id;
    }
    uint32_t constUVec4(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
        return constComposite(typeVector(typeInt(false), 4), {constU(a), constU(b), constU(c), constU(d)});
    }

    // --- globales / decoraciones / cabecera -------------------------------------------------
    uint32_t globalVar(uint32_t ptrType, uint32_t sc) {
        const uint32_t id = newId();
        emit(types_, OpVariable, {ptrType, id, sc});
        if (sc == ScInput || sc == ScOutput) interface_.push_back(id);
        return id;
    }
    uint32_t globalVarInit(uint32_t ptrType, uint32_t sc, uint32_t init) {
        const uint32_t id = newId();
        emit(types_, OpVariable, {ptrType, id, sc, init});
        return id;
    }
    void decorate(uint32_t id, uint32_t dec, std::initializer_list<uint32_t> args = {}) {
        std::vector<uint32_t> w{id, dec};
        w.insert(w.end(), args.begin(), args.end());
        emit(annotations_, OpDecorate, w);
    }
    void memberDecorate(uint32_t id, uint32_t member, uint32_t dec, std::initializer_list<uint32_t> args = {}) {
        std::vector<uint32_t> w{id, member, dec};
        w.insert(w.end(), args.begin(), args.end());
        emit(annotations_, OpMemberDecorate, w);
    }
    void capability(uint32_t cap) {
        for (uint32_t c : caps_) if (c == cap) return;
        caps_.push_back(cap);
    }
    void executionMode(uint32_t mode) { modes_.push_back(mode); }
    uint32_t glsl() {
        if (glslId_ == 0) glslId_ = newId();
        return glslId_;
    }

    // --- código (dentro de la función main) -------------------------------------------------
    // Variables de función: deben ir al principio del primer bloque.
    uint32_t localVar(uint32_t ptrType) {
        const uint32_t id = newId();
        emit(locals_, OpVariable, {ptrType, id, ScFunction});
        return id;
    }
    void op(uint32_t opcode, const std::vector<uint32_t>& operands) { emit(code_, opcode, operands); }
    uint32_t value(uint32_t opcode, uint32_t resultType, const std::vector<uint32_t>& operands) {
        const uint32_t id = newId();
        std::vector<uint32_t> w{resultType, id};
        w.insert(w.end(), operands.begin(), operands.end());
        emit(code_, opcode, w);
        return id;
    }
    uint32_t ext(uint32_t resultType, uint32_t inst, const std::vector<uint32_t>& args) {
        std::vector<uint32_t> w{glsl(), inst};
        w.insert(w.end(), args.begin(), args.end());
        return value(OpExtInst, resultType, w);
    }
    void label(uint32_t id) {
        emit(code_, OpLabel, {id});
        blockOpen_ = true;
    }
    // Terminadores: cierran el bloque actual.
    void branch(uint32_t target) { terminate(OpBranch, {target}); }
    void branchCond(uint32_t c, uint32_t t, uint32_t f) { terminate(OpBranchConditional, {c, t, f}); }
    void terminate(uint32_t opcode, const std::vector<uint32_t>& operands) {
        emit(code_, opcode, operands);
        blockOpen_ = false;
    }
    bool blockOpen() const { return blockOpen_; }

    // Módulo completo. executionModel: 0 vértice, 4 fragmento.
    std::vector<uint32_t> finish(uint32_t executionModel, uint32_t mainId, uint32_t mainType) {
        std::vector<uint32_t> out{0x07230203u, 0x00010000u, 0u, 0u, 0u};
        for (uint32_t c : caps_) emitTo(out, OpCapability, {c});
        if (glslId_ != 0) {
            std::vector<uint32_t> w{glslId_};
            appendString(w, "GLSL.std.450");
            emitTo(out, OpExtInstImport, w);
        }
        emitTo(out, OpMemoryModel, {0u, 1u});
        std::vector<uint32_t> ep{executionModel, mainId};
        appendString(ep, "main");
        ep.insert(ep.end(), interface_.begin(), interface_.end());
        emitTo(out, OpEntryPoint, ep);
        for (uint32_t m : modes_) emitTo(out, OpExecutionMode, {mainId, m});
        out.insert(out.end(), annotations_.begin(), annotations_.end());
        out.insert(out.end(), types_.begin(), types_.end());
        emitTo(out, OpFunction, {typeVoid(), mainId, 0u, mainType});
        // Primer bloque: etiqueta de entrada + variables locales + código.
        emitTo(out, OpLabel, {entryLabel_});
        out.insert(out.end(), locals_.begin(), locals_.end());
        out.insert(out.end(), code_.begin(), code_.end());
        emitTo(out, OpFunctionEnd, {});
        out[3] = bound_;
        return out;
    }
    // La etiqueta de entrada la emite finish(); el código empieza "dentro" de ella.
    void beginMain() {
        entryLabel_ = newId();
        blockOpen_ = true;
    }

private:
    uint32_t bound_ = 1;
    uint32_t glslId_ = 0;
    uint32_t entryLabel_ = 0;
    bool blockOpen_ = false;
    std::vector<uint32_t> caps_, modes_, interface_;
    std::vector<uint32_t> annotations_, types_, locals_, code_;
    std::map<std::vector<uint32_t>, uint32_t> dedup_;

    static void emitTo(std::vector<uint32_t>& out, uint32_t opcode, const std::vector<uint32_t>& operands) {
        out.push_back((static_cast<uint32_t>(operands.size() + 1) << 16) | opcode);
        out.insert(out.end(), operands.begin(), operands.end());
    }
    void emit(std::vector<uint32_t>& out, uint32_t opcode, const std::vector<uint32_t>& operands) {
        emitTo(out, opcode, operands);
    }
    static void appendString(std::vector<uint32_t>& w, const char* s) {
        const size_t n = std::strlen(s) + 1;
        const size_t words = (n + 3) / 4;
        const size_t base = w.size();
        w.resize(base + words, 0);
        std::memcpy(w.data() + base, s, n);
    }
    uint32_t type(const std::vector<uint32_t>& key) {
        auto it = dedup_.find(key);
        if (it != dedup_.end()) return it->second;
        const uint32_t id = newId();
        std::vector<uint32_t> w{id};
        w.insert(w.end(), key.begin() + 1, key.end());
        emit(types_, key[0], w);
        dedup_[key] = id;
        return id;
    }
    uint32_t constant(uint32_t t, uint32_t v) {
        const std::vector<uint32_t> key{OpConstant, t, v};
        auto it = dedup_.find(key);
        if (it != dedup_.end()) return it->second;
        const uint32_t id = newId();
        emit(types_, OpConstant, {t, id, v});
        dedup_[key] = id;
        return id;
    }
};

}  // namespace hyperplay::spv
