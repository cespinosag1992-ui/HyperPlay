#pragma once

// Traductor DXBC (SM4/SM5, vértice y píxel) -> SPIR-V 1.0 para Vulkan 1.1 (Adreno 830).
//
// Modelo: cada registro DXBC (r#, x#[], o#) es un uvec4 con los bits crudos; cada instrucción
// carga sus fuentes con el tipo que necesita (bitcast a float/int), aplica swizzle y
// modificadores, opera vector a vector y escribe con la máscara del destino. Es el mismo modelo
// del intérprete `dxbc::Vm`, así que ambos dan el mismo resultado; el driver optimiza los
// bitcasts y las variables.
//
// Interfaz con el pipeline (ver DxbcPipeline / DirectVulkanBridge):
// - set 0; binding = base de etapa (VS 0, PS 64) + {cb#: slot, t#: 16 + slot, s#: 48 + slot}.
// - VS: entradas v# en Location = registro; salidas o# (salvo SV_Position) en Location = registro.
// - PS: entradas enlazadas por semántica con las salidas del VS (Location del registro del VS);
//   SV_Target n en Location n; oDepth -> FragDepth.
// - Clip space: D3D y Vulkan comparten z en [0,1] y centro de píxel; la Y se invierte con un
//   viewport de altura negativa (VK_KHR_maintenance1, núcleo en 1.1), sin tocar el shader.

#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../loader/DxbcShader.h"
#include "SpirvBuilder.h"

namespace hyperplay::dxbc2spv {

struct Result {
    std::vector<uint32_t> spirv;
    std::string error;          // vacío si la traducción tuvo éxito
    uint32_t stageBinding = 0;  // base de binding de la etapa (0 VS, 64 PS)
    std::vector<uint32_t> cbSlots, texSlots, samplerSlots;
    bool ok() const { return error.empty() && !spirv.empty(); }
};

constexpr uint32_t kBindingBaseVs = 0, kBindingBasePs = 64;
constexpr uint32_t kBindingTex = 16, kBindingSampler = 48;

namespace detail {

using namespace hyperplay::spv;
using dxbc::Instruction;
using dxbc::Operand;
using dxbc::Program;

class Translator {
public:
    Translator(const Program& p, const Program* vsLink) : p_(p), vsLink_(vsLink) {}

    Result run() {
        Result res;
        isPs_ = p_.type == dxbc::kPixelShader;
        if (!isPs_ && p_.type != dxbc::kVertexShader) {
            res.error = "solo vértice y píxel";
            return res;
        }
        res.stageBinding = isPs_ ? kBindingBasePs : kBindingBaseVs;
        b_.capability(1);  // Shader
        f32_ = b_.typeFloat();
        u32_ = b_.typeInt(false);
        i32_ = b_.typeInt(true);
        bool_ = b_.typeBool();
        v4f_ = b_.typeVector(f32_, 4);
        v4u_ = b_.typeVector(u32_, 4);
        v4i_ = b_.typeVector(i32_, 4);
        v4b_ = b_.typeVector(bool_, 4);
        prescan();
        declareResources(res);
        declareInputs();
        declareOutputs();
        const uint32_t mainType = b_.typeFunction(b_.typeVoid());
        const uint32_t mainId = b_.newId();
        b_.beginMain();
        declareTemps();
        for (pc_ = 0; pc_ < p_.code.size() && error_.empty(); ++pc_) instruction(p_.code[pc_]);
        if (error_.empty() && b_.blockOpen()) {
            epilogue();
            b_.terminate(OpReturn, {});
        }
        if (!error_.empty()) {
            res.error = error_;
            return res;
        }
        if (isPs_) b_.executionMode(7);  // OriginUpperLeft
        if (writesDepth_) b_.executionMode(12);  // DepthReplacing
        res.spirv = b_.finish(isPs_ ? 4u : 0u, mainId, mainType);
        return res;
    }

private:
    const Program& p_;
    const Program* vsLink_;
    Builder b_;
    bool isPs_ = false;
    size_t pc_ = 0;
    std::string error_;
    uint32_t f32_ = 0, u32_ = 0, i32_ = 0, bool_ = 0, v4f_ = 0, v4u_ = 0, v4i_ = 0, v4b_ = 0;
    uint32_t mulStruct_ = 0;
    std::set<uint32_t> stridedArrays_;

    struct InputVar {
        uint32_t var = 0;
        uint32_t kind = 0;  // 0 vec4 float, 1 uvec4, 2 ivec4, 3 FragCoord, 4 VertexIndex/InstanceIndex, 5 FrontFacing, 6 SampleId
    };
    std::map<uint32_t, InputVar> inputs_;
    struct OutputVar {
        uint32_t priv = 0;  // uvec4 privado (lo que escribe el shader)
        uint32_t var = 0;   // variable de salida real
        uint32_t kind = 0;  // 0 float, 1 uint, 2 int, 3 Position
    };
    std::map<uint32_t, OutputVar> outputs_;
    uint32_t depthPriv_ = 0, depthVar_ = 0;
    bool writesDepth_ = false;
    std::vector<uint32_t> temps_;
    std::map<uint32_t, std::pair<uint32_t, uint32_t>> indexable_;  // reg -> {var, count}
    std::map<uint32_t, uint32_t> cbVars_;
    uint32_t icbVar_ = 0, icbCount_ = 0;
    struct TexVar {
        uint32_t var = 0, imageType = 0, dim = 0, coordDims = 0, offsetDims = 0, sampledType = 0;
        bool arrayed = false;
    };
    std::map<uint32_t, TexVar> texVars_, cmpTexVars_;
    std::map<uint32_t, uint32_t> samplerVars_;
    std::vector<bool> compareSlot_ = std::vector<bool>(128, false);
    std::vector<bool> plainSlot_ = std::vector<bool>(128, false);
    bool needQuery_ = false, needDerivCtrl_ = false;

    struct Cf {
        int kind = 0;  // 0 if, 1 loop, 2 switch
        uint32_t merge = 0, elseLabel = 0, cont = 0, header = 0;
        bool elseSeen = false;
        std::map<size_t, uint32_t> caseLabels;  // índice de case/default -> etiqueta
    };
    std::vector<Cf> cf_;

    // ---------------------------------------------------------------- declaraciones
    void prescan() {
        for (const Instruction& ins : p_.code) {
            const uint32_t op = ins.opcode;
            if ((op == dxbc::kSampleC || op == dxbc::kSampleCLz || op == dxbc::kGather4C) && ins.operands.size() > 2) {
                const uint32_t s = ins.operands[2].index[0];
                if (s < compareSlot_.size()) compareSlot_[s] = true;
            } else if (ins.operands.size() > 2 && ins.operands[2].type == dxbc::kOpResource) {
                const uint32_t s = ins.operands[2].index[0];
                if (s < plainSlot_.size()) plainSlot_[s] = true;
            }
            if (op == dxbc::kResinfo || op == dxbc::kLod) needQuery_ = true;
            if (op >= dxbc::kDerivRtxCoarse && op <= dxbc::kDerivRtyFine) needDerivCtrl_ = true;
        }
        if (needQuery_) b_.capability(50);       // ImageQuery
        if (needDerivCtrl_ && isPs_) b_.capability(51);  // DerivativeControl
    }

    void declareResources(Result& res) {
        const uint32_t base = isPs_ ? kBindingBasePs : kBindingBaseVs;
        for (const auto& cb : p_.constantBuffers) {
            const uint32_t n = cb.size == 0 ? 1 : cb.size;
            // Tipo propio por CB: el ArrayStride no debe recaer en arrays Private/Function (ICB,
            // x#[]) ni repetirse en un tipo compartido.
            const uint32_t arr = b_.typeArrayUnique(v4u_, n);
            if (stridedArrays_.insert(arr).second) b_.decorate(arr, DecArrayStride, {16});
            const uint32_t st = b_.typeStruct({arr});
            b_.decorate(st, DecBlock);
            b_.memberDecorate(st, 0, DecOffset, {0});
            const uint32_t var = b_.globalVar(b_.typePointer(ScUniform, st), ScUniform);
            b_.decorate(var, DecDescriptorSet, {0});
            b_.decorate(var, DecBinding, {base + cb.slot});
            cbVars_[cb.slot] = var;
            res.cbSlots.push_back(cb.slot);
        }
        if (!p_.immConstantBuffer.empty()) {
            icbCount_ = static_cast<uint32_t>(p_.immConstantBuffer.size() / 4);
            if (icbCount_ == 0) icbCount_ = 1;
            std::vector<uint32_t> elems;
            for (uint32_t i = 0; i < icbCount_; ++i) {
                const uint32_t* d = p_.immConstantBuffer.data() + i * 4;
                elems.push_back(b_.constUVec4(d[0], d[1], d[2], d[3]));
            }
            const uint32_t arr = b_.typeArray(v4u_, icbCount_);
            const uint32_t init = b_.constComposite(arr, elems);
            icbVar_ = b_.globalVarInit(b_.typePointer(ScPrivate, arr), ScPrivate, init);
        }
        for (const auto& r : p_.resources) {
            // D3D10_SB_RESOURCE_DIMENSION: 2 1D, 3 2D, 5 3D, 6 cube, 7 1D array, 8 2D array, 10 cube array
            TexVar tv;
            uint32_t dim = Dim2D;
            switch (r.dim) {
                case 2: dim = Dim1D; tv.coordDims = 1; tv.offsetDims = 1; break;
                case 3: dim = Dim2D; tv.coordDims = 2; tv.offsetDims = 2; break;
                case 5: dim = Dim3D; tv.coordDims = 3; tv.offsetDims = 3; break;
                case 6: dim = DimCube; tv.coordDims = 3; tv.offsetDims = 0; break;
                case 7: dim = Dim1D; tv.coordDims = 2; tv.offsetDims = 1; tv.arrayed = true; break;
                case 8: dim = Dim2D; tv.coordDims = 3; tv.offsetDims = 2; tv.arrayed = true; break;
                case 10: dim = DimCube; tv.coordDims = 4; tv.offsetDims = 0; tv.arrayed = true; break;
                default:
                    continue;  // buffers / MS: no soportados por ahora (no se declaran)
            }
            if (dim == Dim1D) b_.capability(43);  // Sampled1D
            if (dim == DimCube && tv.arrayed) b_.capability(45);  // SampledCubeArray
            const uint32_t rt = r.returnType & 0xF;  // componente x: 1 unorm 2 snorm 3 sint 4 uint 5 float
            tv.sampledType = rt == 3 ? i32_ : (rt == 4 ? u32_ : f32_);
            tv.dim = dim;
            auto declare = [&](uint32_t depth) {
                TexVar t = tv;
                t.imageType = b_.typeImage(t.sampledType, dim, depth, t.arrayed, 1);
                t.var = b_.globalVar(b_.typePointer(ScUniformConstant, t.imageType), ScUniformConstant);
                b_.decorate(t.var, DecDescriptorSet, {0});
                b_.decorate(t.var, DecBinding, {base + kBindingTex + r.slot});
                return t;
            };
            const bool cmp = r.slot < compareSlot_.size() && compareSlot_[r.slot];
            const bool plain = !cmp || (r.slot < plainSlot_.size() && plainSlot_[r.slot]);
            if (plain) texVars_[r.slot] = declare(0);
            if (cmp) cmpTexVars_[r.slot] = declare(1);
            res.texSlots.push_back(r.slot);
        }
        for (uint32_t s : p_.samplers) {
            const uint32_t var = b_.globalVar(b_.typePointer(ScUniformConstant, b_.typeSampler()), ScUniformConstant);
            b_.decorate(var, DecDescriptorSet, {0});
            b_.decorate(var, DecBinding, {base + kBindingSampler + s});
            samplerVars_[s] = var;
            res.samplerSlots.push_back(s);
        }
    }

    const dxbc::SignatureElement* sigFor(const std::vector<dxbc::SignatureElement>& sig, uint32_t reg) const {
        for (const auto& e : sig) if (e.reg == reg) return &e;
        return nullptr;
    }

    uint32_t linkedLocation(uint32_t reg) const {
        if (!isPs_ || vsLink_ == nullptr) return reg;
        const dxbc::SignatureElement* in = sigFor(p_.inputSig, reg);
        if (in == nullptr) return reg;
        for (const auto& o : vsLink_->outputSig) {
            if (o.semanticIndex != in->semanticIndex || o.name.size() != in->name.size()) continue;
            bool same = true;
            for (size_t i = 0; i < o.name.size() && same; ++i) same = (o.name[i] | 0x20) == (in->name[i] | 0x20);
            if (same) return o.reg;
        }
        return 30;  // sin pareja en el VS: lectura indefinida (ubicación libre)
    }

    void declareInputs() {
        for (const auto& d : p_.inputs) {
            if (inputs_.count(d.reg)) continue;
            InputVar iv;
            const dxbc::SignatureElement* sig = sigFor(p_.inputSig, d.reg);
            const uint32_t sv = d.systemValue != 0 ? d.systemValue : (sig ? sig->systemValue : 0);
            if (isPs_ && sv == 1) {  // SV_Position
                iv.kind = 3;
                iv.var = b_.globalVar(b_.typePointer(ScInput, v4f_), ScInput);
                b_.decorate(iv.var, DecBuiltIn, {BiFragCoord});
            } else if (!isPs_ && (sv == 6 || sv == 8)) {  // SV_VertexID / SV_InstanceID
                iv.kind = 4;
                iv.var = b_.globalVar(b_.typePointer(ScInput, i32_), ScInput);
                b_.decorate(iv.var, DecBuiltIn, {sv == 6 ? BiVertexIndex : BiInstanceIndex});
            } else if (isPs_ && sv == 9) {  // SV_IsFrontFace
                iv.kind = 5;
                iv.var = b_.globalVar(b_.typePointer(ScInput, bool_), ScInput);
                b_.decorate(iv.var, DecBuiltIn, {BiFrontFacing});
            } else if (isPs_ && sv == 10) {  // SV_SampleIndex
                iv.kind = 6;
                b_.capability(35);  // SampleRateShading
                iv.var = b_.globalVar(b_.typePointer(ScInput, i32_), ScInput);
                b_.decorate(iv.var, DecBuiltIn, {BiSampleId});
            } else {
                const uint32_t ct = sig ? sig->componentType : 3;
                iv.kind = ct == 1 ? 1 : (ct == 2 ? 2 : 0);
                const uint32_t t = iv.kind == 1 ? v4u_ : (iv.kind == 2 ? v4i_ : v4f_);
                iv.var = b_.globalVar(b_.typePointer(ScInput, t), ScInput);
                b_.decorate(iv.var, DecLocation, {linkedLocation(d.reg)});
                if (isPs_) {
                    const uint32_t m = d.interpolation;
                    if (iv.kind != 0 || m == 1) {
                        b_.decorate(iv.var, DecFlat);
                    } else {
                        if (m == 4 || m == 5 || m == 7) b_.decorate(iv.var, DecNoPerspective);
                        if (m == 3 || m == 5) b_.decorate(iv.var, DecCentroid);
                        if (m == 6 || m == 7) {
                            b_.capability(35);
                            b_.decorate(iv.var, DecSample);
                        }
                    }
                }
            }
            inputs_[d.reg] = iv;
        }
    }

    void declareOutputs() {
        for (const auto& d : p_.outputs) {
            if (outputs_.count(d.reg)) continue;
            OutputVar ov;
            const dxbc::SignatureElement* sig = sigFor(p_.outputSig, d.reg);
            const uint32_t sv = d.systemValue != 0 ? d.systemValue : (sig ? sig->systemValue : 0);
            ov.priv = b_.globalVar(b_.typePointer(ScPrivate, v4u_), ScPrivate);
            if (!isPs_ && sv == 1) {
                ov.kind = 3;
                ov.var = b_.globalVar(b_.typePointer(ScOutput, v4f_), ScOutput);
                b_.decorate(ov.var, DecBuiltIn, {BiPosition});
            } else {
                const uint32_t ct = sig ? sig->componentType : 3;
                ov.kind = ct == 1 ? 1 : (ct == 2 ? 2 : 0);
                const uint32_t t = ov.kind == 1 ? v4u_ : (ov.kind == 2 ? v4i_ : v4f_);
                ov.var = b_.globalVar(b_.typePointer(ScOutput, t), ScOutput);
                b_.decorate(ov.var, DecLocation, {d.reg});
            }
            outputs_[d.reg] = ov;
        }
        // Salidas escritas sin dcl_output (no debería pasar, pero algunos compiladores lo omiten)
        for (const Instruction& ins : p_.code) {
            for (const Operand& op : ins.operands) {
                if (op.type == dxbc::kOpOutputDepth || op.type == dxbc::kOpOutputDepthGe ||
                    op.type == dxbc::kOpOutputDepthLe) {
                    writesDepth_ = isPs_;
                }
            }
        }
        if (writesDepth_) {
            depthPriv_ = b_.globalVar(b_.typePointer(ScPrivate, f32_), ScPrivate);
            depthVar_ = b_.globalVar(b_.typePointer(ScOutput, f32_), ScOutput);
            b_.decorate(depthVar_, DecBuiltIn, {BiFragDepth});
        }
    }

    void declareTemps() {
        const uint32_t ptr = b_.typePointer(ScFunction, v4u_);
        temps_.resize(p_.tempCount);
        for (uint32_t i = 0; i < p_.tempCount; ++i) {
            temps_[i] = b_.localVar(ptr);
            b_.op(OpStore, {temps_[i], b_.constUVec4(0, 0, 0, 0)});
        }
        for (const auto& it : p_.indexableTemps) {
            const uint32_t n = it.count == 0 ? 1 : it.count;
            const uint32_t arr = b_.typeArray(v4u_, n);
            indexable_[it.reg] = {b_.localVar(b_.typePointer(ScFunction, arr)), n};
        }
        for (auto& [reg, ov] : outputs_) {
            (void)reg;
            b_.op(OpStore, {ov.priv, b_.constUVec4(0, 0, 0, 0)});
        }
        if (writesDepth_) b_.op(OpStore, {depthPriv_, b_.constF(0.0f)});
    }

    // Copia las salidas privadas a las variables de salida reales (antes de cada return).
    void epilogue() {
        for (auto& [reg, ov] : outputs_) {
            (void)reg;
            const uint32_t raw = b_.value(OpLoad, v4u_, {ov.priv});
            uint32_t v = raw;
            if (ov.kind == 0 || ov.kind == 3) v = b_.value(OpBitcast, v4f_, {raw});
            else if (ov.kind == 2) v = b_.value(OpBitcast, v4i_, {raw});
            b_.op(OpStore, {ov.var, v});
        }
        if (writesDepth_) b_.op(OpStore, {depthVar_, b_.value(OpLoad, f32_, {depthPriv_})});
    }

    // ---------------------------------------------------------------- operandos
    uint32_t vecU(uint32_t a, uint32_t bb, uint32_t c, uint32_t d) { return b_.constUVec4(a, bb, c, d); }
    uint32_t splatU(uint32_t v) { return vecU(v, v, v, v); }
    uint32_t splatF(float f) {
        const uint32_t c = b_.constF(f);
        return b_.constComposite(v4f_, {c, c, c, c});
    }
    uint32_t bitcast(uint32_t t, uint32_t v) { return b_.value(OpBitcast, t, {v}); }
    uint32_t toF(uint32_t u) { return bitcast(v4f_, u); }
    uint32_t toI(uint32_t u) { return bitcast(v4i_, u); }
    uint32_t fromF(uint32_t f) { return bitcast(v4u_, f); }
    uint32_t fromI(uint32_t i) { return bitcast(v4u_, i); }
    uint32_t shuffle(uint32_t t, uint32_t a, uint32_t c, std::initializer_list<uint32_t> idx) {
        std::vector<uint32_t> w{a, c};
        w.insert(w.end(), idx.begin(), idx.end());
        return b_.value(OpVectorShuffle, t, w);
    }
    uint32_t extract(uint32_t t, uint32_t v, uint32_t i) { return b_.value(OpCompositeExtract, t, {v, i}); }
    uint32_t splatScalar(uint32_t t, uint32_t s) { return b_.value(OpCompositeConstruct, t, {s, s, s, s}); }

    // Índice dinámico (registro relativo + inmediato) como u32.
    uint32_t indexValue(const Operand& op, int d) {
        uint32_t idx = b_.constU(op.index[d]);
        if (op.relative[d] >= 0) {
            const Operand& rel = p_.relOperands[static_cast<size_t>(op.relative[d])];
            const uint32_t rv = extract(u32_, swizzled(rel), 0);
            idx = op.index[d] == 0 ? rv : b_.value(OpIAdd, u32_, {rv, b_.constU(op.index[d])});
        }
        return idx;
    }

    uint32_t inputRaw(uint32_t reg) {
        auto it = inputs_.find(reg);
        if (it == inputs_.end()) return splatU(0);
        const InputVar& iv = it->second;
        switch (iv.kind) {
            case 0: return fromF(b_.value(OpLoad, v4f_, {iv.var}));
            case 1: return b_.value(OpLoad, v4u_, {iv.var});
            case 2: return fromI(b_.value(OpLoad, v4i_, {iv.var}));
            case 3: {  // FragCoord: D3D usa w = w de recorte; Vulkan da 1/w
                const uint32_t fc = b_.value(OpLoad, v4f_, {iv.var});
                const uint32_t w = extract(f32_, fc, 3);
                const uint32_t rw = b_.value(OpFDiv, f32_, {b_.constF(1.0f), w});
                const uint32_t v = b_.value(OpCompositeConstruct, v4f_,
                                            {extract(f32_, fc, 0), extract(f32_, fc, 1), extract(f32_, fc, 2), rw});
                return fromF(v);
            }
            case 4:
            case 6: {
                const uint32_t s = bitcast(u32_, b_.value(OpLoad, i32_, {iv.var}));
                return b_.value(OpCompositeConstruct, v4u_, {s, s, s, s});
            }
            case 5: {
                const uint32_t f = b_.value(OpLoad, bool_, {iv.var});
                const uint32_t s = b_.value(OpSelect, u32_, {f, b_.constU(0xFFFFFFFFu), b_.constU(0)});
                return b_.value(OpCompositeConstruct, v4u_, {s, s, s, s});
            }
            default: return splatU(0);
        }
    }

    // Valor crudo (uvec4) del registro, sin swizzle ni modificadores.
    uint32_t raw(const Operand& op) {
        switch (op.type) {
            case dxbc::kOpTemp:
                if (op.index[0] < temps_.size()) return b_.value(OpLoad, v4u_, {temps_[op.index[0]]});
                return splatU(0);
            case dxbc::kOpInput:
                return inputRaw(op.index[0]);
            case dxbc::kOpOutput: {
                auto it = outputs_.find(op.index[0]);
                return it == outputs_.end() ? splatU(0) : b_.value(OpLoad, v4u_, {it->second.priv});
            }
            case dxbc::kOpIndexableTemp: {
                auto it = indexable_.find(op.index[0]);
                if (it == indexable_.end()) return splatU(0);
                const uint32_t ptr = b_.value(OpAccessChain, b_.typePointer(ScFunction, v4u_),
                                              {it->second.first, indexValue(op, 1)});
                return b_.value(OpLoad, v4u_, {ptr});
            }
            case dxbc::kOpImm32:
                if (op.numComponents == 1) return splatU(op.imm[0]);
                return vecU(op.imm[0], op.imm[1], op.imm[2], op.imm[3]);
            case dxbc::kOpConstantBuffer: {
                auto it = cbVars_.find(op.index[0]);
                if (it == cbVars_.end()) return splatU(0);
                const uint32_t ptr = b_.value(OpAccessChain, b_.typePointer(ScUniform, v4u_),
                                              {it->second, b_.constU(0), indexValue(op, 1)});
                return b_.value(OpLoad, v4u_, {ptr});
            }
            case dxbc::kOpImmConstantBuffer: {
                if (icbVar_ == 0) return splatU(0);
                const uint32_t ptr = b_.value(OpAccessChain, b_.typePointer(ScPrivate, v4u_), {icbVar_, indexValue(op, 0)});
                return b_.value(OpLoad, v4u_, {ptr});
            }
            default:
                return splatU(0);
        }
    }

    uint32_t swizzled(const Operand& op) {
        const uint32_t r = raw(op);
        if (op.type == dxbc::kOpImm32) return r;
        const uint8_t* s = op.swizzle;
        if (s[0] == 0 && s[1] == 1 && s[2] == 2 && s[3] == 3) return r;
        return shuffle(v4u_, r, r, {s[0] & 3u, s[1] & 3u, s[2] & 3u, s[3] & 3u});
    }

    // Fuente tipada con modificadores: kind 0 float, 1 int (neg/abs enteros), 2 uint (sin mods).
    uint32_t srcF(const Operand& op) {
        uint32_t v = toF(swizzled(op));
        if (op.abs) v = b_.ext(v4f_, GlslFAbs, {v});
        if (op.neg) v = b_.value(OpFNegate, v4f_, {v});
        return v;
    }
    uint32_t srcI(const Operand& op) {
        uint32_t v = toI(swizzled(op));
        if (op.abs) v = b_.ext(v4i_, GlslSAbs, {v});
        if (op.neg) v = b_.value(OpSNegate, v4i_, {v});
        return v;
    }
    uint32_t srcU(const Operand& op) {
        if (op.abs || op.neg) return fromI(srcI(op));
        return swizzled(op);
    }

    uint32_t destMask(const Operand& op) const {
        if (op.numComponents == 4) return op.selMode == 0 ? (op.mask & 0xF) : 0xF;
        return 1;
    }

    // Escribe un uvec4 en el destino respetando la máscara.
    void store(const Operand& dst, uint32_t valueU) {
        if (dst.type == dxbc::kOpNull) return;
        if (dst.type == dxbc::kOpOutputDepth || dst.type == dxbc::kOpOutputDepthGe ||
            dst.type == dxbc::kOpOutputDepthLe) {
            if (depthPriv_ != 0) b_.op(OpStore, {depthPriv_, extract(f32_, toF(valueU), 0)});
            return;
        }
        uint32_t ptr = 0;
        switch (dst.type) {
            case dxbc::kOpTemp:
                if (dst.index[0] < temps_.size()) ptr = temps_[dst.index[0]];
                break;
            case dxbc::kOpOutput: {
                auto it = outputs_.find(dst.index[0]);
                if (it != outputs_.end()) ptr = it->second.priv;
                break;
            }
            case dxbc::kOpIndexableTemp: {
                auto it = indexable_.find(dst.index[0]);
                if (it != indexable_.end()) {
                    ptr = b_.value(OpAccessChain, b_.typePointer(ScFunction, v4u_),
                                   {it->second.first, indexValue(dst, 1)});
                }
                break;
            }
            default:
                break;
        }
        if (ptr == 0) return;
        const uint32_t mask = destMask(dst);
        if (mask == 0xF) {
            b_.op(OpStore, {ptr, valueU});
            return;
        }
        const uint32_t old = b_.value(OpLoad, v4u_, {ptr});
        auto pick = [&](uint32_t c) { return ((mask >> c) & 1u) ? 4 + c : c; };
        b_.op(OpStore, {ptr, shuffle(v4u_, old, valueU, {pick(0), pick(1), pick(2), pick(3)})});
    }
    void storeF(const Instruction& ins, const Operand& dst, uint32_t vf) {
        if (ins.saturate) vf = b_.ext(v4f_, GlslNClamp, {vf, splatF(0.0f), splatF(1.0f)});
        store(dst, fromF(vf));
    }
    void storeI(const Operand& dst, uint32_t vi) { store(dst, fromI(vi)); }
    uint32_t boolToMask(uint32_t vb) { return b_.value(OpSelect, v4u_, {vb, splatU(0xFFFFFFFFu), splatU(0)}); }

    uint32_t condBool(const Instruction& ins) {
        const uint32_t x = extract(u32_, swizzled(ins.operands[0]), 0);
        return b_.value(ins.testNonZero ? OpINotEqual : OpIEqual, bool_, {x, b_.constU(0)});
    }

    // ---------------------------------------------------------------- control de flujo
    Cf* innermost(bool loopOnly) {
        for (size_t i = cf_.size(); i-- > 0;) {
            if (cf_[i].kind == 1 || (!loopOnly && cf_[i].kind == 2)) return &cf_[i];
        }
        return nullptr;
    }
    // Bloque inalcanzable tras un terminador (break/continue/ret/discard incondicional).
    void newBlock() {
        b_.label(b_.newId());
        dead_ = true;
    }
    void lab(uint32_t id) {
        b_.label(id);
        dead_ = false;
    }
    bool dead_ = false;

    // Rama condicional a `target` (break/continue/return/discard condicionales).
    template <typename F>
    void conditional(const Instruction& ins, F onTrue) {
        const uint32_t c = condBool(ins);
        const uint32_t yes = b_.newId(), cont = b_.newId();
        b_.op(OpSelectionMerge, {cont, 0});
        b_.branchCond(c, yes, cont);
        lab(yes);
        onTrue();
        lab(cont);
    }

    void beginSwitch(const Instruction& ins) {
        Cf c;
        c.kind = 2;
        c.merge = b_.newId();
        // Casos al nivel de este switch; casos vacíos consecutivos comparten etiqueta.
        int depth = 0;
        std::vector<std::pair<uint32_t, uint32_t>> targets;
        uint32_t defaultLabel = c.merge;
        uint32_t prevLabel = 0;
        bool prevWasCase = false;
        for (size_t i = pc_ + 1; i < p_.code.size(); ++i) {
            const uint32_t opc = p_.code[i].opcode;
            if (opc == dxbc::kSwitch) ++depth;
            if (opc == dxbc::kEndswitch) {
                if (depth == 0) break;
                --depth;
            }
            if (depth != 0) {
                prevWasCase = false;
                continue;
            }
            if (opc == dxbc::kCase || opc == dxbc::kDefault) {
                const uint32_t l = prevWasCase ? prevLabel : b_.newId();
                c.caseLabels[i] = l;
                if (opc == dxbc::kCase && !p_.code[i].operands.empty()) {
                    targets.emplace_back(p_.code[i].operands[0].imm[0], l);
                } else {
                    defaultLabel = l;
                }
                prevLabel = l;
                prevWasCase = true;
            } else {
                prevWasCase = false;
            }
        }
        const uint32_t sel = extract(u32_, swizzled(ins.operands[0]), 0);
        b_.op(OpSelectionMerge, {c.merge, 0});
        std::vector<uint32_t> w{sel, defaultLabel};
        for (const auto& [v, l] : targets) {
            w.push_back(v);
            w.push_back(l);
        }
        b_.terminate(OpSwitch, w);
        cf_.push_back(c);
    }

    // ---------------------------------------------------------------- texturas
    uint32_t coordVec(uint32_t vf, uint32_t n, uint32_t compType) {
        const uint32_t t = n == 1 ? compType : b_.typeVector(compType, n);
        if (n == 1) return extract(compType, vf, 0);
        if (n == 4) return vf;
        if (n == 2) return shuffle(t, vf, vf, {0, 1});
        return shuffle(t, vf, vf, {0, 1, 2});
    }
    uint32_t offsetConst(const Instruction& ins, uint32_t n) {
        if (n == 0) return 0;
        if (ins.texelOffset[0] == 0 && ins.texelOffset[1] == 0 && ins.texelOffset[2] == 0) return 0;
        if (n == 1) return b_.constI(ins.texelOffset[0]);
        std::vector<uint32_t> parts;
        for (uint32_t i = 0; i < n; ++i) parts.push_back(b_.constI(ins.texelOffset[i]));
        return b_.constComposite(b_.typeVector(i32_, n), parts);
    }
    // Resultado del muestreo (vec4 del tipo de la textura) -> uvec4 con el swizzle del recurso.
    void storeTexel(const Instruction& ins, const Operand& dst, const Operand& tex, const TexVar& tv, uint32_t texel) {
        uint32_t u = texel;
        if (tv.sampledType == f32_) u = fromF(texel);
        else if (tv.sampledType == i32_) u = fromI(texel);
        const uint8_t* s = tex.swizzle;
        if (!(s[0] == 0 && s[1] == 1 && s[2] == 2 && s[3] == 3)) {
            u = shuffle(v4u_, u, u, {s[0] & 3u, s[1] & 3u, s[2] & 3u, s[3] & 3u});
        }
        if (ins.saturate && tv.sampledType == f32_) {
            storeF(ins, dst, toF(u));
        } else {
            store(dst, u);
        }
    }
    uint32_t sampledImage(const TexVar& tv, uint32_t samplerSlot) {
        auto sit = samplerVars_.find(samplerSlot);
        if (sit == samplerVars_.end()) return 0;
        const uint32_t img = b_.value(OpLoad, tv.imageType, {tv.var});
        const uint32_t smp = b_.value(OpLoad, b_.typeSampler(), {sit->second});
        return b_.value(OpSampledImage, b_.typeSampledImage(tv.imageType), {img, smp});
    }
    uint32_t texResultType(const TexVar& tv) {
        return tv.sampledType == f32_ ? v4f_ : (tv.sampledType == i32_ ? v4i_ : v4u_);
    }

    void sample(const Instruction& ins) {
        const auto& ops = ins.operands;
        const uint32_t op = ins.opcode;
        const bool cmp = op == dxbc::kSampleC || op == dxbc::kSampleCLz;
        const Operand& tex = ops[2];
        auto& map = cmp ? cmpTexVars_ : texVars_;
        auto it = map.find(tex.index[0]);
        if (it == map.end()) {
            store(ops[0], splatU(0));
            return;
        }
        const TexVar& tv = it->second;
        const uint32_t si = sampledImage(tv, ops[3].index[0]);
        if (si == 0) {
            store(ops[0], splatU(0));
            return;
        }
        const uint32_t coord = coordVec(srcF(ops[1]), tv.coordDims, f32_);
        const uint32_t off = offsetConst(ins, tv.offsetDims);
        std::vector<uint32_t> w{si, coord};
        if (cmp) {
            w.push_back(extract(f32_, srcF(ops[4]), 0));
            const bool explicitLod = op == dxbc::kSampleCLz || !isPs_;
            uint32_t mask = explicitLod ? ImLod : 0u;
            if (off) mask |= ImConstOffset;
            if (mask) w.push_back(mask);
            if (explicitLod) w.push_back(b_.constF(0.0f));
            if (off) w.push_back(off);
            const uint32_t r = b_.value(explicitLod ? OpImageSampleDrefExplicitLod : OpImageSampleDrefImplicitLod, f32_, w);
            storeF(ins, ops[0], splatScalar(v4f_, r));
            return;
        }
        uint32_t mask = 0;
        std::vector<uint32_t> extra;
        bool explicitLod = false;
        if (op == dxbc::kSampleL) {
            explicitLod = true;
            mask |= ImLod;
            extra.push_back(extract(f32_, srcF(ops[4]), 0));
        } else if (op == dxbc::kSampleD) {
            explicitLod = true;
            mask |= ImGrad;
            const uint32_t gd = tv.coordDims - (tv.arrayed ? 1 : 0);
            extra.push_back(coordVec(srcF(ops[4]), gd, f32_));
            extra.push_back(coordVec(srcF(ops[5]), gd, f32_));
        } else if (op == dxbc::kSampleB && isPs_) {
            mask |= ImBias;
            extra.push_back(extract(f32_, srcF(ops[4]), 0));
        } else if (!isPs_) {
            explicitLod = true;
            mask |= ImLod;
            extra.push_back(b_.constF(0.0f));
        }
        if (off) {
            mask |= ImConstOffset;
            extra.push_back(off);
        }
        if (mask) {
            w.push_back(mask);
            w.insert(w.end(), extra.begin(), extra.end());
        }
        const uint32_t r = b_.value(explicitLod ? OpImageSampleExplicitLod : OpImageSampleImplicitLod, texResultType(tv), w);
        storeTexel(ins, ops[0], tex, tv, r);
    }

    void load(const Instruction& ins) {
        const auto& ops = ins.operands;
        const Operand& tex = ops[2];
        auto it = texVars_.find(tex.index[0]);
        if (it == texVars_.end()) {
            store(ops[0], splatU(0));
            return;
        }
        const TexVar& tv = it->second;
        const uint32_t ci = srcI(ops[1]);
        const uint32_t coord = coordVec(ci, tv.coordDims, i32_);
        const uint32_t lod = extract(i32_, ci, 3);
        const uint32_t img = b_.value(OpLoad, tv.imageType, {tv.var});
        std::vector<uint32_t> w{img, coord};
        uint32_t mask = 0;
        std::vector<uint32_t> extra;
        if (tv.dim != DimCube) {
            mask |= ImLod;
            extra.push_back(lod);
        }
        const uint32_t off = offsetConst(ins, tv.offsetDims);
        if (off) {
            mask |= ImConstOffset;
            extra.push_back(off);
        }
        if (mask) {
            w.push_back(mask);
            w.insert(w.end(), extra.begin(), extra.end());
        }
        storeTexel(ins, ops[0], tex, tv, b_.value(OpImageFetch, texResultType(tv), w));
    }

    void resinfo(const Instruction& ins) {
        const auto& ops = ins.operands;
        const Operand& tex = ops[2];
        auto it = texVars_.find(tex.index[0]);
        if (it == texVars_.end()) {
            store(ops[0], splatU(0));
            return;
        }
        const TexVar& tv = it->second;
        const uint32_t img = b_.value(OpLoad, tv.imageType, {tv.var});
        const uint32_t lod = extract(i32_, srcI(ops[1]), 0);
        const uint32_t n = tv.coordDims == 4 ? 3 : tv.coordDims;  // cube array: (w, h, capas)
        const uint32_t sizeT = n == 1 ? i32_ : b_.typeVector(i32_, n);
        const uint32_t size = b_.value(OpImageQuerySizeLod, sizeT, {img, lod});
        const uint32_t levels = b_.value(OpImageQueryLevels, i32_, {img});
        uint32_t c[4] = {b_.constI(0), b_.constI(0), b_.constI(0), levels};
        for (uint32_t i = 0; i < n && i < 3; ++i) c[i] = n == 1 ? size : extract(i32_, size, i);
        const uint32_t vi = b_.value(OpCompositeConstruct, v4i_, {c[0], c[1], c[2], c[3]});
        const uint32_t mode = ins.resourceDim & 3u;  // 0 float, 1 rcpFloat, 2 uint
        uint32_t u = 0;
        if (mode == 2) {
            u = fromI(vi);
        } else {
            uint32_t vf = b_.value(OpConvertSToF, v4f_, {vi});
            if (mode == 1) {
                const uint32_t r = b_.value(OpFDiv, v4f_, {splatF(1.0f), vf});
                vf = shuffle(v4f_, r, vf, {0, 1, 2, 7});
            }
            u = fromF(vf);
        }
        const uint8_t* s = tex.swizzle;
        u = shuffle(v4u_, u, u, {s[0] & 3u, s[1] & 3u, s[2] & 3u, s[3] & 3u});
        store(ops[0], u);
    }

    void gather(const Instruction& ins) {
        const auto& ops = ins.operands;
        const Operand& tex = ops[2];
        auto it = texVars_.find(tex.index[0]);
        if (it == texVars_.end()) {
            store(ops[0], splatU(0));
            return;
        }
        const TexVar& tv = it->second;
        const uint32_t si = sampledImage(tv, ops[3].index[0]);
        if (si == 0 || tv.dim == Dim3D || tv.dim == Dim1D) {
            store(ops[0], splatU(0));
            return;
        }
        const uint32_t coord = coordVec(srcF(ops[1]), tv.coordDims, f32_);
        std::vector<uint32_t> w{si, coord, b_.constU(ops[3].swizzle[0] & 3u)};
        const uint32_t off = offsetConst(ins, tv.offsetDims);
        if (off) {
            w.push_back(ImConstOffset);
            w.push_back(off);
        }
        storeTexel(ins, ops[0], tex, tv, b_.value(OpImageGather, texResultType(tv), w));
    }

    // ---------------------------------------------------------------- instrucciones
    template <typename F>
    void binF(const Instruction& ins, F fn) {
        storeF(ins, ins.operands[0], fn(srcF(ins.operands[1]), srcF(ins.operands[2])));
    }
    void binFop(const Instruction& ins, uint32_t opcode) {
        binF(ins, [&](uint32_t a, uint32_t c) { return b_.value(opcode, v4f_, {a, c}); });
    }
    void binFext(const Instruction& ins, uint32_t inst) {
        binF(ins, [&](uint32_t a, uint32_t c) { return b_.ext(v4f_, inst, {a, c}); });
    }
    void unFext(const Instruction& ins, uint32_t inst) {
        storeF(ins, ins.operands[0], b_.ext(v4f_, inst, {srcF(ins.operands[1])}));
    }
    void binU(const Instruction& ins, uint32_t opcode) {
        store(ins.operands[0], b_.value(opcode, v4u_, {srcU(ins.operands[1]), srcU(ins.operands[2])}));
    }
    void binUext(const Instruction& ins, uint32_t inst, bool asSigned) {
        if (asSigned) {
            storeI(ins.operands[0], b_.ext(v4i_, inst, {srcI(ins.operands[1]), srcI(ins.operands[2])}));
        } else {
            store(ins.operands[0], b_.ext(v4u_, inst, {srcU(ins.operands[1]), srcU(ins.operands[2])}));
        }
    }
    void shift(const Instruction& ins, uint32_t opcode) {
        const uint32_t cnt = b_.value(OpBitwiseAnd, v4u_, {srcU(ins.operands[2]), splatU(31)});
        store(ins.operands[0], b_.value(opcode, v4u_, {srcU(ins.operands[1]), cnt}));
    }
    void cmpF(const Instruction& ins, uint32_t opcode) {
        store(ins.operands[0], boolToMask(b_.value(opcode, v4b_, {srcF(ins.operands[1]), srcF(ins.operands[2])})));
    }
    void cmpI(const Instruction& ins, uint32_t opcode, bool asSigned) {
        const uint32_t a = asSigned ? srcI(ins.operands[1]) : srcU(ins.operands[1]);
        const uint32_t c = asSigned ? srcI(ins.operands[2]) : srcU(ins.operands[2]);
        store(ins.operands[0], boolToMask(b_.value(opcode, v4b_, {a, c})));
    }
    void dot(const Instruction& ins, uint32_t n) {
        uint32_t a = srcF(ins.operands[1]), c = srcF(ins.operands[2]);
        if (n != 4) {
            const uint32_t t = b_.typeVector(f32_, n);
            a = n == 2 ? shuffle(t, a, a, {0, 1}) : shuffle(t, a, a, {0, 1, 2});
            c = n == 2 ? shuffle(t, c, c, {0, 1}) : shuffle(t, c, c, {0, 1, 2});
        }
        storeF(ins, ins.operands[0], splatScalar(v4f_, b_.value(OpDot, f32_, {a, c})));
    }
    uint32_t mulStruct() {
        if (mulStruct_ == 0) mulStruct_ = b_.typeStruct({v4u_, v4u_});
        return mulStruct_;
    }

    void instruction(const Instruction& ins) {
        namespace D = dxbc;
        const auto& ops = ins.operands;
        switch (ins.opcode) {
            // --- control de flujo
            case D::kIf: {
                Cf c;
                c.kind = 0;
                c.merge = b_.newId();
                c.elseLabel = b_.newId();
                const uint32_t then = b_.newId();
                const uint32_t cond = condBool(ins);
                b_.op(OpSelectionMerge, {c.merge, 0});
                b_.branchCond(cond, then, c.elseLabel);
                lab(then);
                cf_.push_back(c);
                break;
            }
            case D::kElse: {
                if (cf_.empty() || cf_.back().kind != 0) return fail("else sin if");
                Cf& c = cf_.back();
                if (b_.blockOpen()) b_.branch(c.merge);
                lab(c.elseLabel);
                c.elseSeen = true;
                break;
            }
            case D::kEndif: {
                if (cf_.empty() || cf_.back().kind != 0) return fail("endif sin if");
                Cf c = cf_.back();
                cf_.pop_back();
                if (b_.blockOpen()) b_.branch(c.merge);
                if (!c.elseSeen) {
                    lab(c.elseLabel);
                    b_.branch(c.merge);
                }
                lab(c.merge);
                break;
            }
            case D::kLoop: {
                Cf c;
                c.kind = 1;
                c.header = b_.newId();
                c.cont = b_.newId();
                c.merge = b_.newId();
                const uint32_t body = b_.newId();
                b_.branch(c.header);
                lab(c.header);
                b_.op(OpLoopMerge, {c.merge, c.cont, 0});
                b_.branch(body);
                lab(body);
                cf_.push_back(c);
                break;
            }
            case D::kEndloop: {
                if (cf_.empty() || cf_.back().kind != 1) return fail("endloop sin loop");
                Cf c = cf_.back();
                cf_.pop_back();
                if (b_.blockOpen()) b_.branch(c.cont);
                lab(c.cont);
                b_.branch(c.header);
                lab(c.merge);
                break;
            }
            case D::kBreak: {
                Cf* c = innermost(false);
                if (c == nullptr) return fail("break fuera de bucle/switch");
                b_.branch(c->merge);
                newBlock();
                break;
            }
            case D::kBreakc: {
                Cf* c = innermost(false);
                if (c == nullptr) return fail("breakc fuera de bucle/switch");
                const uint32_t target = c->merge;
                conditional(ins, [&] { b_.branch(target); });
                break;
            }
            case D::kContinue: {
                Cf* c = innermost(true);
                if (c == nullptr) return fail("continue fuera de bucle");
                b_.branch(c->cont);
                newBlock();
                break;
            }
            case D::kContinuec: {
                Cf* c = innermost(true);
                if (c == nullptr) return fail("continuec fuera de bucle");
                const uint32_t target = c->cont;
                conditional(ins, [&] { b_.branch(target); });
                break;
            }
            case D::kSwitch:
                beginSwitch(ins);
                break;
            case D::kCase:
            case D::kDefault: {
                if (cf_.empty() || cf_.back().kind != 2) return fail("case fuera de switch");
                auto it = cf_.back().caseLabels.find(pc_);
                if (it == cf_.back().caseLabels.end()) return fail("case sin etiqueta");
                // Casos vacíos consecutivos comparten etiqueta: solo se abre la primera.
                const bool sharedWithPrev = pc_ > 0 && cf_.back().caseLabels.count(pc_ - 1) &&
                                            cf_.back().caseLabels[pc_ - 1] == it->second;
                if (!sharedWithPrev) {
                    // Caída al siguiente case; desde un bloque muerto (tras break) se va al merge.
                    if (b_.blockOpen()) b_.branch(dead_ ? cf_.back().merge : it->second);
                    lab(it->second);
                }
                break;
            }
            case D::kEndswitch: {
                if (cf_.empty() || cf_.back().kind != 2) return fail("endswitch sin switch");
                Cf c = cf_.back();
                cf_.pop_back();
                if (b_.blockOpen()) b_.branch(c.merge);
                lab(c.merge);
                break;
            }
            case D::kRet:
                epilogue();
                b_.terminate(OpReturn, {});
                newBlock();
                break;
            case D::kRetc:
                conditional(ins, [&] {
                    epilogue();
                    b_.terminate(OpReturn, {});
                });
                break;
            case D::kDiscard:
                if (!isPs_) break;
                conditional(ins, [&] { b_.terminate(OpKill, {}); });
                break;
            case D::kNop:
            case D::kLabel:
            case D::kCustomData:
                break;

            // --- aritmética float
            case D::kMov:
                if (ins.saturate || ops[1].abs || ops[1].neg) storeF(ins, ops[0], srcF(ops[1]));
                else store(ops[0], swizzled(ops[1]));
                break;
            case D::kMovc: {
                const uint32_t c = b_.value(OpINotEqual, v4b_, {srcU(ops[1]), splatU(0)});
                const uint32_t a = (ops[2].abs || ops[2].neg) ? fromF(srcF(ops[2])) : swizzled(ops[2]);
                const uint32_t d = (ops[3].abs || ops[3].neg) ? fromF(srcF(ops[3])) : swizzled(ops[3]);
                const uint32_t r = b_.value(OpSelect, v4u_, {c, a, d});
                if (ins.saturate) storeF(ins, ops[0], toF(r));
                else store(ops[0], r);
                break;
            }
            case D::kAdd: binFop(ins, OpFAdd); break;
            case D::kMul: binFop(ins, OpFMul); break;
            case D::kDiv: binFop(ins, OpFDiv); break;
            case D::kMin: binFext(ins, GlslNMin); break;
            case D::kMax: binFext(ins, GlslNMax); break;
            case D::kMad: {
                const uint32_t m = b_.value(OpFMul, v4f_, {srcF(ops[1]), srcF(ops[2])});
                storeF(ins, ops[0], b_.value(OpFAdd, v4f_, {m, srcF(ops[3])}));
                break;
            }
            case D::kDp2: dot(ins, 2); break;
            case D::kDp3: dot(ins, 3); break;
            case D::kDp4: dot(ins, 4); break;
            case D::kRsq: unFext(ins, GlslInverseSqrt); break;
            case D::kSqrt: unFext(ins, GlslSqrt); break;
            case D::kRcp: storeF(ins, ops[0], b_.value(OpFDiv, v4f_, {splatF(1.0f), srcF(ops[1])})); break;
            case D::kExp: unFext(ins, GlslExp2); break;
            case D::kLog: unFext(ins, GlslLog2); break;
            case D::kFrc: unFext(ins, GlslFract); break;
            case D::kRoundNe: unFext(ins, GlslRoundEven); break;
            case D::kRoundNi: unFext(ins, GlslFloor); break;
            case D::kRoundPi: unFext(ins, GlslCeil); break;
            case D::kRoundZ: unFext(ins, GlslTrunc); break;
            case D::kSincos: {
                const uint32_t a = srcF(ops[2]);
                if (ops[0].type != D::kOpNull) storeF(ins, ops[0], b_.ext(v4f_, GlslSin, {a}));
                if (ops[1].type != D::kOpNull) storeF(ins, ops[1], b_.ext(v4f_, GlslCos, {a}));
                break;
            }
            case D::kDerivRtx:
            case D::kDerivRty:
            case D::kDerivRtxCoarse:
            case D::kDerivRtxFine:
            case D::kDerivRtyCoarse:
            case D::kDerivRtyFine: {
                if (!isPs_) {
                    store(ops[0], splatU(0));
                    break;
                }
                uint32_t o = OpDPdx;
                switch (ins.opcode) {
                    case D::kDerivRty: o = OpDPdy; break;
                    case D::kDerivRtxCoarse: o = OpDPdxCoarse; break;
                    case D::kDerivRtxFine: o = OpDPdxFine; break;
                    case D::kDerivRtyCoarse: o = OpDPdyCoarse; break;
                    case D::kDerivRtyFine: o = OpDPdyFine; break;
                    default: break;
                }
                storeF(ins, ops[0], b_.value(o, v4f_, {srcF(ops[1])}));
                break;
            }
            case D::kEq: cmpF(ins, OpFOrdEqual); break;
            case D::kNe: cmpF(ins, OpFUnordNotEqual); break;
            case D::kLt: cmpF(ins, OpFOrdLessThan); break;
            case D::kGe: cmpF(ins, OpFOrdGreaterThanEqual); break;

            // --- enteros
            case D::kIeq: cmpI(ins, OpIEqual, false); break;
            case D::kIne: cmpI(ins, OpINotEqual, false); break;
            case D::kIlt: cmpI(ins, OpSLessThan, true); break;
            case D::kIge: cmpI(ins, OpSGreaterThanEqual, true); break;
            case D::kUlt: cmpI(ins, OpULessThan, false); break;
            case D::kUge: cmpI(ins, OpUGreaterThanEqual, false); break;
            case D::kAnd: binU(ins, OpBitwiseAnd); break;
            case D::kOr: binU(ins, OpBitwiseOr); break;
            case D::kXor: binU(ins, OpBitwiseXor); break;
            case D::kIadd: binU(ins, OpIAdd); break;
            case D::kIshl: shift(ins, OpShiftLeftLogical); break;
            case D::kUshr: shift(ins, OpShiftRightLogical); break;
            case D::kIshr: shift(ins, OpShiftRightArithmetic); break;
            case D::kImin: binUext(ins, GlslSMin, true); break;
            case D::kImax: binUext(ins, GlslSMax, true); break;
            case D::kUmin: binUext(ins, GlslUMin, false); break;
            case D::kUmax: binUext(ins, GlslUMax, false); break;
            case D::kUdiv: {
                const uint32_t a = srcU(ops[2]), d = srcU(ops[3]);
                if (ops[0].type != D::kOpNull) store(ops[0], b_.value(OpUDiv, v4u_, {a, d}));
                if (ops[1].type != D::kOpNull) store(ops[1], b_.value(OpUMod, v4u_, {a, d}));
                break;
            }
            case D::kImul:
            case D::kUmul: {
                const uint32_t a = srcU(ops[2]), d = srcU(ops[3]);
                if (ops[0].type == D::kOpNull) {  // solo la parte baja: IMul
                    store(ops[1], b_.value(OpIMul, v4u_, {a, d}));
                    break;
                }
                const uint32_t st = ins.opcode == D::kImul
                                        ? b_.value(OpSMulExtended, mulStruct(), {a, d})
                                        : b_.value(OpUMulExtended, mulStruct(), {a, d});
                store(ops[0], extract(v4u_, st, 1));
                if (ops[1].type != D::kOpNull) store(ops[1], extract(v4u_, st, 0));
                break;
            }
            case D::kImad:
            case D::kUmad: {
                const uint32_t m = b_.value(OpIMul, v4u_, {srcU(ops[1]), srcU(ops[2])});
                store(ops[0], b_.value(OpIAdd, v4u_, {m, srcU(ops[3])}));
                break;
            }
            case D::kIneg: storeI(ops[0], b_.value(OpSNegate, v4i_, {srcI(ops[1])})); break;
            case D::kNot: store(ops[0], b_.value(OpNot, v4u_, {srcU(ops[1])})); break;
            case D::kItof: storeF(ins, ops[0], b_.value(OpConvertSToF, v4f_, {srcI(ops[1])})); break;
            case D::kUtof: storeF(ins, ops[0], b_.value(OpConvertUToF, v4f_, {srcU(ops[1])})); break;
            case D::kFtoi: storeI(ops[0], b_.value(OpConvertFToS, v4i_, {srcF(ops[1])})); break;
            case D::kFtou: store(ops[0], b_.value(OpConvertFToU, v4u_, {srcF(ops[1])})); break;
            case D::kCountbits: store(ops[0], b_.value(OpBitCount, v4u_, {srcU(ops[1])})); break;
            case D::kFirstbitLo: storeI(ops[0], b_.ext(v4i_, GlslFindILsb, {srcI(ops[1])})); break;
            case D::kFirstbitHi:
            case D::kFirstbitShi: {
                // D3D cuenta desde el bit más alto: 31 - findMSB (o -1 si no hay bits).
                const uint32_t m = ins.opcode == D::kFirstbitHi ? b_.ext(v4i_, GlslFindUMsb, {srcU(ops[1])})
                                                               : b_.ext(v4i_, GlslFindSMsb, {srcI(ops[1])});
                const uint32_t neg = b_.value(OpSLessThan, v4b_, {m, toI(splatU(0))});
                const uint32_t r = b_.value(OpISub, v4i_, {toI(splatU(31)), m});
                storeI(ops[0], b_.value(OpSelect, v4i_, {neg, m, r}));
                break;
            }
            case D::kUbfe:
            case D::kIbfe: {
                // ubfe dst, ancho, desplazamiento, valor: SPIR-V trabaja por componente con escalares.
                const uint32_t w = b_.value(OpBitwiseAnd, v4u_, {srcU(ops[1]), splatU(31)});
                const uint32_t o = b_.value(OpBitwiseAnd, v4u_, {srcU(ops[2]), splatU(31)});
                const uint32_t v = srcU(ops[3]);
                uint32_t comp[4];
                for (uint32_t c = 0; c < 4; ++c) {
                    const uint32_t vc = extract(u32_, v, c), oc = extract(u32_, o, c), wc = extract(u32_, w, c);
                    if (ins.opcode == D::kUbfe) {
                        comp[c] = b_.value(OpBitFieldUExtract, u32_, {vc, oc, wc});
                    } else {
                        comp[c] = bitcast(u32_, b_.value(OpBitFieldSExtract, i32_, {bitcast(i32_, vc), oc, wc}));
                    }
                }
                store(ops[0], b_.value(OpCompositeConstruct, v4u_, {comp[0], comp[1], comp[2], comp[3]}));
                break;
            }
            case D::kBfi: {
                const uint32_t w = b_.value(OpBitwiseAnd, v4u_, {srcU(ops[1]), splatU(31)});
                const uint32_t o = b_.value(OpBitwiseAnd, v4u_, {srcU(ops[2]), splatU(31)});
                const uint32_t ins_ = srcU(ops[3]), base = srcU(ops[4]);
                uint32_t comp[4];
                for (uint32_t c = 0; c < 4; ++c) {
                    comp[c] = b_.value(OpBitFieldInsert, u32_,
                                       {extract(u32_, base, c), extract(u32_, ins_, c), extract(u32_, o, c),
                                        extract(u32_, w, c)});
                }
                store(ops[0], b_.value(OpCompositeConstruct, v4u_, {comp[0], comp[1], comp[2], comp[3]}));
                break;
            }
            case D::kBfrev: store(ops[0], b_.value(OpBitReverse, v4u_, {srcU(ops[1])})); break;
            case D::kF32ToF16: {
                const uint32_t v = srcF(ops[1]);
                const uint32_t v2 = b_.typeVector(f32_, 2);
                uint32_t comp[4];
                for (uint32_t c = 0; c < 4; ++c) {
                    const uint32_t pair = b_.value(OpCompositeConstruct, v2, {extract(f32_, v, c), b_.constF(0.0f)});
                    comp[c] = b_.ext(u32_, GlslPackHalf2x16, {pair});
                }
                store(ops[0], b_.value(OpCompositeConstruct, v4u_, {comp[0], comp[1], comp[2], comp[3]}));
                break;
            }
            case D::kF16ToF32: {
                const uint32_t v = srcU(ops[1]);
                const uint32_t v2 = b_.typeVector(f32_, 2);
                uint32_t comp[4];
                for (uint32_t c = 0; c < 4; ++c) {
                    comp[c] = extract(f32_, b_.ext(v2, GlslUnpackHalf2x16, {extract(u32_, v, c)}), 0);
                }
                storeF(ins, ops[0], b_.value(OpCompositeConstruct, v4f_, {comp[0], comp[1], comp[2], comp[3]}));
                break;
            }
            case D::kSwapc: {  // swapc d0, d1, cond, a, b: d0 = c ? b : a; d1 = c ? a : b
                const uint32_t c = b_.value(OpINotEqual, v4b_, {srcU(ops[2]), splatU(0)});
                const uint32_t a = swizzled(ops[3]), d = swizzled(ops[4]);
                const uint32_t r0 = b_.value(OpSelect, v4u_, {c, d, a});
                const uint32_t r1 = b_.value(OpSelect, v4u_, {c, a, d});
                store(ops[0], r0);
                store(ops[1], r1);
                break;
            }

            // --- texturas
            case D::kSample:
            case D::kSampleL:
            case D::kSampleB:
            case D::kSampleC:
            case D::kSampleCLz:
            case D::kSampleD:
                sample(ins);
                break;
            case D::kLd:
                load(ins);
                break;
            case D::kResinfo:
                resinfo(ins);
                break;
            case D::kGather4:
                gather(ins);
                break;

            default: {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "opcode DXBC %u no soportado", ins.opcode);
                return fail(buf);
            }
        }
    }

    void fail(const std::string& e) {
        if (error_.empty()) error_ = e + " (instrucción " + std::to_string(pc_) + ")";
    }
};

}  // namespace detail

// Traduce un programa DXBC. Para un pixel shader, `vsLink` (opcional) es el vertex shader con el
// que se enlazará: las entradas se ubican en la Location de la salida del VS con la misma semántica.
inline Result translate(const dxbc::Program& program, const dxbc::Program* vsLink = nullptr) {
    detail::Translator t(program, vsLink);
    return t.run();
}

}  // namespace hyperplay::dxbc2spv
