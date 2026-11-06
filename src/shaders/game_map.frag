#version 330 core
in vec3 vNormal;
in vec3 vWorldPos;
in vec3 vColor;
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uTex;
uniform int uAlphaTest; // 👈 0: 不透明, 1: 开启透明测试 (discard)

void main() {
    vec4 texColor = texture(uTex, vUV);
    
    // 👈 如果是透明材质（如铁丝网、树叶、玻璃），alpha 低于 0.1 直接丢弃像素
    if (uAlphaTest == 1) {
        if (texColor.a < 0.1) {
            discard;
        }
    }

    // 强行给个基础环境光，防止全黑
    vec3 bakedLight = max(vColor, vec3(0.3)); 
    FragColor = vec4(texColor.rgb * bakedLight, 1.0);
}