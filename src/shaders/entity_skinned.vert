#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in ivec4 aBoneIDs;   // 👈 受哪几根骨骼影响（需要转为 int）
layout(location = 3) in vec4 aWeights;   // 👈 影响的权重

uniform mat4 uMVP;
uniform mat4 uBones[100];

out vec2 vUV;

void main() {
    // 👈 关键修复 1：先构造蒙皮矩阵，再乘顶点
    mat4 skinMatrix = mat4(0.0);
    float totalWeight = 0.0;
    
    for (int i = 0; i < 4; i++) {
        int boneID = aBoneIDs[i];
        float weight = aWeights[i];
        if (boneID >= 0 && weight > 0.0) {
            skinMatrix += uBones[boneID] * weight;
            totalWeight += weight;
        }
    }

    vec4 skinnedPos;
    if (totalWeight < 0.0001) {
        // 没有绑定任何骨骼，直接用原始坐标（兜底）
        skinnedPos = vec4(aPos, 1.0);
    } else {
        // 归一化权重（防止 >1 或 <1 的误差，参考 HL2 常见权重偏差）
        skinMatrix /= totalWeight;
        skinnedPos = skinMatrix * vec4(aPos, 1.0);
    }

    gl_Position = uMVP * skinnedPos;
    vUV = aUV;
}