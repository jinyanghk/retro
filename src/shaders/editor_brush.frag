#version 330 core
in vec3 vNormal;
in vec3 vWorldPos;  // 👈 需要顶点着色器传世界坐标
out vec4 FragColor;

uniform sampler2D uTex;

void main() {
    // 简单的三平面投影（根据法线选择投影面）
    vec3 blend = abs(normalize(vNormal));
    blend = blend / (blend.x + blend.y + blend.z);  // 归一化权重

    // 采样贴图（根据世界坐标）
    vec3 colX = texture(uTex, vWorldPos.zy * 0.01).rgb;
    vec3 colY = texture(uTex, vWorldPos.xz * 0.01).rgb;
    vec3 colZ = texture(uTex, vWorldPos.xy * 0.01).rgb;

    vec3 texColor = colX * blend.x + colY * blend.y + colZ * blend.z;

    // 光照
    float shade = 0.5 + 0.5 * max(dot(vNormal, normalize(vec3(0.3, 0.5, 0.8))), 0.0);
    FragColor = vec4(texColor * shade, 1.0);
}