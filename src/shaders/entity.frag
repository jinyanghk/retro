#version 330 core
in vec3 vNormal;
in vec3 vWorldPos;
in vec3 vColor;
in vec2 vUV;
out vec4 FragColor;
uniform vec3 uColor;

void main() {
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    float diff = max(dot(vNormal, lightDir), 0.0) * 0.5 + 0.5;
    FragColor = vec4(uColor * diff, 1.0);
}