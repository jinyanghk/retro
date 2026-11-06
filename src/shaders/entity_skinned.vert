#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aBoneIDs;   // 👈 受哪几根骨骼影响
layout(location = 3) in vec4 aWeights;   // 👈 影响的权重

uniform mat4 uMVP;
uniform mat4 uBones[100]; // 👈 最多支持 100 根骨骼的矩阵数组

out vec2 vUV;

void main() {
    // 计算蒙皮后的顶点位置
    vec4 skinnedPos = vec4(0.0);
    for (int i = 0; i < 4; i++) {
        int boneID = int(aBoneIDs[i]);
        if (boneID >= 0) {
            skinnedPos += uBones[boneID] * vec4(aPos, 1.0) * aWeights[i];
        }
    }
    
    // 如果权重总和为 0（没有骨骼绑定），就用原本的顶点
    if (skinnedPos.w == 0.0) {
        skinnedPos = vec4(aPos, 1.0);
    }

    gl_Position = uMVP * skinnedPos;
    vUV = aUV;
}