// src/assets/AnimationHelper.h
#pragma once
#define GLM_ENABLE_EXPERIMENTAL // 👈 关键！放在所有 glm 头文件之前
#include "ModelLoader.h"
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <assimp/scene.h>

namespace AnimHelper {

    inline float GetAnimationTime(const Animation& anim, float currentTime) {
        if (anim.duration <= 0.0f) return 0.0f;
        return fmod(currentTime, anim.duration);
    }

    inline void CalcInterpolatedPosition(glm::vec3& out, float time, const std::vector<KeyPosition>& keys) {
        if (keys.empty()) return;
        if (keys.size() == 1) { out = keys[0].value; return; }
        
        int index = 0;
        for (size_t i = 0; i < keys.size() - 1; i++) {
            if (time < keys[i + 1].time) { index = i; break; }
        }
        int next = (index + 1) % keys.size();
        float factor = (time - keys[index].time) / (keys[next].time - keys[index].time);
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
        float factor = (time - keys[index].time) / (keys[next].time - keys[index].time);
        out = glm::slerp(keys[index].value, keys[next].value, factor);
    }

    // 提取 Assimp 的节点矩阵
    inline glm::mat4 ConvertMatrixToGLMFormat(const aiMatrix4x4& from) {
        glm::mat4 to;
        to[0][0] = from.a1; to[1][0] = from.a2; to[2][0] = from.a3; to[3][0] = from.a4;
        to[0][1] = from.b1; to[1][1] = from.b2; to[2][1] = from.b3; to[3][1] = from.b4;
        to[0][2] = from.c1; to[1][2] = from.c2; to[2][2] = from.c3; to[3][2] = from.c4;
        to[0][3] = from.d1; to[1][3] = from.d2; to[2][3] = from.d3; to[3][3] = from.d4;
        return to;
    }

    // 计算所有骨骼的最终矩阵
    inline void CalculateBoneTransforms(
        const aiNode* node, 
        const glm::mat4& parentTransform, 
        const Animation& anim, 
        float animTime, 
        const std::unordered_map<std::string, BoneInfo>& boneMap, 
        std::vector<glm::mat4>& outBoneMatrices)
    {
        std::string nodeName = node->mName.C_Str();
        glm::mat4 nodeTransform = ConvertMatrixToGLMFormat(node->mTransformation);

        // 如果这个节点有动画通道，替换矩阵
        for (const auto& boneAnim : anim.boneAnimations) {
            if (boneAnim.name == nodeName) {
                glm::vec3 pos(0.0f);
                glm::quat rot(1, 0, 0, 0);
                glm::vec3 scale(1.0f);
                
                CalcInterpolatedPosition(pos, animTime, boneAnim.positions);
                CalcInterpolatedRotation(rot, animTime, boneAnim.rotations);
                // Scale 简化处理，这里通常不动
                
                glm::mat4 T = glm::translate(glm::mat4(1.0f), pos);
                glm::mat4 R = glm::toMat4(rot);
                glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
                nodeTransform = T * R * S;
                break;
            }
        }

        glm::mat4 globalTransform = parentTransform * nodeTransform;

        // 如果这个节点是骨骼，保存它的矩阵
        if (boneMap.find(nodeName) != boneMap.end()) {
            int boneID = boneMap.at(nodeName).id;
            glm::mat4 offset = boneMap.at(nodeName).offset;
            // 最终蒙皮矩阵 = 全局变换 * 逆绑定矩阵
            outBoneMatrices[boneID] = globalTransform * offset;
        }

        // 递归子节点
        for (unsigned int i = 0; i < node->mNumChildren; i++) {
            CalculateBoneTransforms(node->mChildren[i], globalTransform, anim, animTime, boneMap, outBoneMatrices);
        }
    }
}