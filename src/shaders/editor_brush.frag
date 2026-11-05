#version 330 core
in vec3 vNormal;
in vec2 vUV;
out vec4 FragColor;
void main() {
    vec3 base = vec3(0.6, 0.6, 0.65);
    float shade = 0.5 + 0.5 * max(dot(vNormal, normalize(vec3(0.3, 0.5, 0.8))), 0.0);
    FragColor = vec4(base * shade, 1.0);
}