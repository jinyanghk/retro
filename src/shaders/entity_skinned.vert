#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in ivec4 aBoneIDs;
layout(location = 3) in vec4 aWeights;
layout(location = 4) in vec3 aNormal;
layout(location = 5) in vec3 aTangent;
layout(location = 6) in vec3 aBitangent;

uniform mat4 uMVP;
uniform mat4 uModel;        // 👈 世界变换（丧尸位置 + 缩放）
uniform mat4 uBones[100];

out vec2 vUV;
out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vTangent;
out vec3 vBitangent;

void main() {
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
    if (totalWeight < 0.0001) {
        skinMatrix = mat4(1.0);
    } else {
        skinMatrix /= totalWeight;
    }

    vec4 localPos = skinMatrix * vec4(aPos, 1.0);
    vec4 worldPos = uModel * localPos;
    vWorldPos = worldPos.xyz;

    // 法线/切线用蒙皮矩阵的旋转部分变换
    mat3 normalMatrix = mat3(skinMatrix);
    vNormal    = normalize(normalMatrix * aNormal);
    vTangent   = normalize(normalMatrix * aTangent);
    vBitangent = normalize(normalMatrix * aBitangent);

    gl_Position = uMVP * localPos;
    vUV = aUV;
}