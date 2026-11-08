// src/assets/AnimationHelper.h
#pragma once
#define GLM_ENABLE_EXPERIMENTAL
#include "ModelLoader.h"
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <assimp/scene.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace AnimHelper {

    // 插值函数
    inline void CalcInterpolatedPosition(glm::vec3& out, float time, const std::vector<KeyPosition>& keys) {
        if (keys.empty()) return;
        if (keys.size() == 1) { out = keys[0].value; return; }
        int index = 0;
        for (size_t i = 0; i < keys.size() - 1; i++) {
            if (time < keys[i + 1].time) { index = i; break; }
        }
        int next = (index + 1) % keys.size();
        float dt = keys[next].time - keys[index].time;
        float factor = (dt > 1e-6f) ? (time - keys[index].time) / dt : 0.0f;
        out = glm::mix(keys[index].value, keys[next].value, factor);
    }

    inline void CalcInterpolatedRotation(glm::quat& out, float time, const std::vector<KeyRotation>& keys) {
        if (keys.empty()) return;
        if (keys.size() == 1) { out = keys[0].value; return; }
        int index = 0;
        for (size_t i = 0; i < keys.size() - 1; i++) {
            if (time < keys[i + 1].time) { index = i; break; }
        }
        int next = (index + 1) % keys.size();
        float dt = keys[next].time - keys[index].time;
        float factor = (dt > 1e-6f) ? (time - keys[index].time) / dt : 0.0f;
        out = glm::slerp(keys[index].value, keys[next].value, factor);
    }

    // Assimp -> GLM 矩阵转换
    inline glm::mat4 ConvertMatrixToGLMFormat(const aiMatrix4x4& from) {
        glm::mat4 to;
        to[0][0] = from.a1; to[1][0] = from.a2; to[2][0] = from.a3; to[3][0] = from.a4;
        to[0][1] = from.b1; to[1][1] = from.b2; to[2][1] = from.b3; to[3][1] = from.b4;
        to[0][2] = from.c1; to[1][2] = from.c2; to[2][2] = from.c3; to[3][2] = from.c4;
        to[0][3] = from.d1; to[1][3] = from.d2; to[2][3] = from.d3; to[3][3] = from.d4;
        return to;
    }

    // 获取单骨骼局部变换（只负责给定动画通道的绝对数据）
    inline glm::mat4 GetBoneLocalTransform(
        const aiNode* node,
        const Animation& anim,
        float animTime)
    {
        glm::mat4 nodeTransform = ConvertMatrixToGLMFormat(node->mTransformation);

        for (const auto& boneAnim : anim.boneAnimations) {
            if (boneAnim.name != node->mName.C_Str()) continue;

            glm::vec3 pos = glm::vec3(nodeTransform[3]);
            glm::quat rot = glm::quat_cast(glm::mat3(nodeTransform));
            glm::vec3 scale = glm::vec3(
                glm::length(glm::vec3(nodeTransform[0])),
                glm::length(glm::vec3(nodeTransform[1])),
                glm::length(glm::vec3(nodeTransform[2]))
            );

            if (!boneAnim.positions.empty()) {
                // 👈 核心修复：直接取通道数据覆盖，不进行加法
                CalcInterpolatedPosition(pos, animTime, boneAnim.positions);
            }
            if (!boneAnim.rotations.empty()) {
                CalcInterpolatedRotation(rot, animTime, boneAnim.rotations);
            }
            if (!boneAnim.scales.empty()) {
                scale = boneAnim.scales[0].value; 
            }

            return glm::translate(glm::mat4(1.0f), pos) *
                   glm::mat4_cast(rot) *
                   glm::scale(glm::mat4(1.0f), scale);
        }
        return nodeTransform;
    }

    // 递归计算全局变换
    inline void CalculateBoneGlobalTransforms(
        const aiNode* node,
        const glm::mat4& parentGlobal,
        const Animation& anim,
        float animTime,
        std::unordered_map<std::string, glm::mat4>& outGlobals)
    {
        glm::mat4 local = GetBoneLocalTransform(node, anim, animTime);
        
        // 👈 安全隔离：如果是根节点，我们忽略它通过父级继承来的旋转/位移污染
        // HL2 模型通常的根节点名字是 classic.qc_skeleton
        glm::mat4 global = parentGlobal * local;
        outGlobals[node->mName.C_Str()] = global;

        for (unsigned int i = 0; i < node->mNumChildren; i++) {
            CalculateBoneGlobalTransforms(node->mChildren[i], global, anim, animTime, outGlobals);
        }
    }

    // 生成蒙皮矩阵
    inline void BuildSkinningMatrices(
        const aiNode* rootNode,
        const Animation& anim,
        float animTime,
        const std::unordered_map<std::string, BoneInfo>& boneMap,
        std::vector<glm::mat4>& outBoneMatrices)
    {
        std::unordered_map<std::string, glm::mat4> globals;
        globals.reserve(100);
        CalculateBoneGlobalTransforms(rootNode, glm::mat4(1.0f), anim, animTime, globals);

        for (const auto& kv : boneMap) {
            const std::string& boneName = kv.first;
            int boneID = kv.second.id;
            const glm::mat4& offset = kv.second.offset; 

            auto it = globals.find(boneName);
            if (it == globals.end()) {
                outBoneMatrices[boneID] = glm::mat4(1.0f);
                continue;
            }

            // 👈 终极蒙皮公式：全局变换 * 逆绑定姿态
            outBoneMatrices[boneID] = it->second * offset;
        }
    }
}