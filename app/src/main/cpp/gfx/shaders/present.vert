#version 450
// Triángulo a pantalla completa para presentar el backbuffer del juego en el swapchain.
// La transformación de UV (rotación de la superficie, pre-transform) llega por push constants:
// uv = (m.x*q.x + m.y*q.y + o.x, m.z*q.x + m.w*q.y + o.y), con q en [0,1] sobre la imagen.
layout(push_constant) uniform Transform {
    vec4 m;
    vec4 o;
} pc;
layout(location = 0) out vec2 uv;

void main() {
    vec2 q = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(q * 2.0 - 1.0, 0.0, 1.0);
    uv = vec2(pc.m.x * q.x + pc.m.y * q.y + pc.o.x, pc.m.z * q.x + pc.m.w * q.y + pc.o.y);
}
