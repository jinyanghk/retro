#version 330 core
in vec3 vNormal;
in vec3 vWorldPos;
in vec3 vColor;
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uTex;

void main() {
    vec3 tex = texture(uTex, vUV).rgb;
    // 强行给个基础环境光，防止全黑
    vec3 bakedLight = max(vColor, vec3(0.3)); 
    FragColor = vec4(tex * bakedLight, 1.0);
}