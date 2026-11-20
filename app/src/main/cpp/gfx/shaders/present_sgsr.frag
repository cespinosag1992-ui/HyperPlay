#version 450
// Snapdragon Game Super Resolution v1 (SGSR, Qualcomm, BSD-3-Clause), modo 1 (canal verde como
// luminancia), en el pase de presentación: 1 solo pase, sin texturas intermedias. Comparte el
// vértice de present.vert (triángulo a pantalla completa + rotación pre-transform en el push
// constant del vértice, offset 0..31); este shader usa offset 32..63.
//
// Algoritmo: si el píxel está en un borde (voto de bordes sobre 4 vecinos con textureGather),
// reconstruye la luminancia con 12 taps ponderados por una Lanczos-2 rápida estirada según la
// varianza local, la limita al rango de los vecinos (sin anillos) y suma la diferencia al color
// bilineal, con un tope de ±23/255 (sin halos en bordes de alto contraste). Zonas planas: bilineal.
layout(set = 0, binding = 0) uniform sampler2D frame;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;

layout(push_constant) uniform Sgsr {
    layout(offset = 32) vec4 viewport;  // (1/srcW, 1/srcH, srcW, srcH) del backbuffer del juego
    vec4 params;                        // x = nitidez (0.30 = SGSR original), yzw libres
} pc;

const float kEdgeThreshold = 8.0 / 255.0;
const float kEdgeSharpness = 2.0;

float fastLanczos2(float x) {
    float wA = x - 4.0;
    float wB = x * wA - wA;
    wA *= wA;
    return wB * wA;
}

vec2 weightY(float dx, float dy, float c, float stdv) {
    float x = ((dx * dx) + (dy * dy)) * 0.55 + clamp(abs(c) * stdv, 0.0, 1.0);
    float w = fastLanczos2(x);
    return vec2(w, w * c);
}

void main() {
    vec4 pix = vec4(textureLod(frame, uv, 0.0).rgb, 0.0);
    vec4 con1 = pc.viewport;
    vec2 imgCoord = uv * con1.zw + vec2(-0.5, 0.5);
    vec2 imgCoordPixel = floor(imgCoord);
    vec2 coord = imgCoordPixel * con1.xy;
    vec2 pl = imgCoord - imgCoordPixel;
    vec4 left = textureGather(frame, coord, 1);

    float edgeVote = abs(left.z - left.y) + abs(pix.g - left.y) + abs(pix.g - left.z);
    if (edgeVote > kEdgeThreshold) {
        coord.x += con1.x;
        vec4 right = textureGather(frame, coord + vec2(con1.x, 0.0), 1);
        vec4 upDown;
        upDown.xy = textureGather(frame, coord + vec2(0.0, -con1.y), 1).wz;
        upDown.zw = textureGather(frame, coord + vec2(0.0, con1.y), 1).yx;

        float mean = (left.y + left.z + right.x + right.w) * 0.25;
        left -= vec4(mean);
        right -= vec4(mean);
        upDown -= vec4(mean);
        pix.w = pix.g - mean;

        float sum = abs(left.x) + abs(left.y) + abs(left.z) + abs(left.w) +
                    abs(right.x) + abs(right.y) + abs(right.z) + abs(right.w) +
                    abs(upDown.x) + abs(upDown.y) + abs(upDown.z) + abs(upDown.w);
        float sumMean = 1.014185e+01 / max(sum, 1e-6);
        float stdv = sumMean * sumMean;

        vec2 aWY = weightY(pl.x, pl.y + 1.0, upDown.x, stdv);
        aWY += weightY(pl.x - 1.0, pl.y + 1.0, upDown.y, stdv);
        aWY += weightY(pl.x - 1.0, pl.y - 2.0, upDown.z, stdv);
        aWY += weightY(pl.x, pl.y - 2.0, upDown.w, stdv);
        aWY += weightY(pl.x + 1.0, pl.y - 1.0, left.x, stdv);
        aWY += weightY(pl.x, pl.y - 1.0, left.y, stdv);
        aWY += weightY(pl.x, pl.y, left.z, stdv);
        aWY += weightY(pl.x + 1.0, pl.y, left.w, stdv);
        aWY += weightY(pl.x - 1.0, pl.y - 1.0, right.x, stdv);
        aWY += weightY(pl.x - 2.0, pl.y - 1.0, right.y, stdv);
        aWY += weightY(pl.x - 2.0, pl.y, right.z, stdv);
        aWY += weightY(pl.x - 1.0, pl.y, right.w, stdv);

        float finalY = aWY.y / aWY.x;
        float maxY = max(max(left.y, left.z), max(right.x, right.w));
        float minY = min(min(left.y, left.z), min(right.x, right.w));
        finalY = clamp(kEdgeSharpness * finalY, minY, maxY);
        float deltaY = finalY - pix.w;
        deltaY = clamp(deltaY, -23.0 / 255.0, 23.0 / 255.0);  // suaviza entradas de alto contraste
        deltaY *= pc.params.x / 0.30;                          // nitidez configurable (0.30 = original)
        pix.rgb = clamp(pix.rgb + vec3(deltaY), 0.0, 1.0);
    }
    color = vec4(pix.rgb, 1.0);
}
