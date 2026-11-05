#version 330 core
in vec3 vNormal;
in vec3 vWorldPos;
in vec3 vColor;
in vec2 vUV;
out vec4 FragColor;
uniform vec3 uColor;

void main() {
    FragColor = vec4(uColor, 1.0);
}