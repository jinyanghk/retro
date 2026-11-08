#include "ModelLoader.h"
#include "TextureCache.h"

#include <GL/glew.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <iostream>

Assimp::Importer ModelLoader::s_importer;
const aiScene* ModelLoader::s_scene = nullptr;

// 辅助函数：递归提取骨骼信息
static void ExtractBoneWeightForVertices(std::vector<Vertex>& vertices,
                                         aiMesh* mesh,
                                         const aiScene* scene,
                                         std::unordered_map<std::string, BoneInfo>& boneMap,
                                         int& boneCounter) {
    for (unsigned int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex) {
        int boneID = -1;
        std::string boneName = mesh->mBones[boneIndex]->mName.C_Str();
        
        if (boneMap.find(boneName) == boneMap.end()) {
            BoneInfo newBoneInfo;
            newBoneInfo.id = boneCounter;
            newBoneInfo.offset = glm::mat4(1.0f); // 这里先初始化，后面动画用
            // 转换 Assimp 矩阵到 glm 矩阵
            aiMatrix4x4 m = mesh->mBones[boneIndex]->mOffsetMatrix;
            newBoneInfo.offset = glm::mat4(
                m.a1, m.b1, m.c1, m.d1,
                m.a2, m.b2, m.c2, m.d2,
                m.a3, m.b3, m.c3, m.d3,
                m.a4, m.b4, m.c4, m.d4
            );
            boneMap[boneName] = newBoneInfo;
            boneID = boneCounter++;
        } else {
            boneID = boneMap[boneName].id;
        }

        // 提取当前骨骼影响的顶点权重
        auto weights = mesh->mBones[boneIndex]->mWeights;
        for (unsigned int weightIndex = 0; weightIndex < mesh->mBones[boneIndex]->mNumWeights; ++weightIndex) {
            int vertexId = weights[weightIndex].mVertexId;
            float weight = weights[weightIndex].mWeight;
            
            // 填入顶点数据
            for (int i = 0; i < 4; ++i) {
                if (vertices[vertexId].weights[i] == 0.0f) {
                    vertices[vertexId].boneIDs[i] = boneID;
                    vertices[vertexId].weights[i] = weight;
                    break;
                }
            }
        }
    }
}

std::vector<Mesh> ModelLoader::LoadModel(const std::string& path, 
                                         std::unordered_map<std::string, BoneInfo>& boneMap, 
                                         int& boneCounter,
                                         std::vector<Animation>& outAnimations) {
    std::vector<Mesh> meshes;
    
    // 注意：这里把 aiProcess_FlipUVs 关掉，因为在骨骼动画里有时候会导致上下颠倒
    s_scene = s_importer.ReadFile(path, 
        aiProcess_Triangulate 
        | aiProcess_GenSmoothNormals
        | aiProcess_CalcTangentSpace);  // 👈 新增
    
    if (!s_scene || s_scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !s_scene->mRootNode) {
        std::cerr << "[ModelLoader] Assimp error: " << s_importer.GetErrorString() << std::endl;
        return meshes;
    }

    std::string dir = path.substr(0, path.find_last_of('/') + 1);

    for (unsigned int i = 0; i < s_scene->mNumMeshes; i++) {
        aiMesh* aMesh = s_scene->mMeshes[i];
        Mesh mesh;
        
        std::vector<Vertex> vertices;
        std::vector<unsigned int> indices;

        for (unsigned int j = 0; j < aMesh->mNumVertices; j++) {
            Vertex v;
            v.position = glm::vec3(aMesh->mVertices[j].x, aMesh->mVertices[j].y, aMesh->mVertices[j].z);
            
            if (aMesh->mTextureCoords[0]) {
                v.uv = glm::vec2(aMesh->mTextureCoords[0][j].x, aMesh->mTextureCoords[0][j].y);
            } else {
                v.uv = glm::vec2(0.0f);
            }

            // 👈 新增：法线、切线、副切线
            if (aMesh->HasNormals()) {
                v.normal = glm::vec3(aMesh->mNormals[j].x, aMesh->mNormals[j].y, aMesh->mNormals[j].z);
            } else {
                v.normal = glm::vec3(0, 1, 0);
            }
            if (aMesh->HasTangentsAndBitangents()) {
                v.tangent   = glm::vec3(aMesh->mTangents[j].x,   aMesh->mTangents[j].y,   aMesh->mTangents[j].z);
                v.bitangent = glm::vec3(aMesh->mBitangents[j].x, aMesh->mBitangents[j].y, aMesh->mBitangents[j].z);
            } else {
                v.tangent   = glm::vec3(1, 0, 0);
                v.bitangent = glm::vec3(0, 0, 1);
            }            
            
            // 默认没有任何骨骼影响
            v.boneIDs = glm::ivec4(-1);
            v.weights = glm::vec4(0.0f);
            
            vertices.push_back(v);
        }

        // 提取骨骼权重
        ExtractBoneWeightForVertices(vertices, aMesh, s_scene, boneMap, boneCounter);

        for (unsigned int j = 0; j < aMesh->mNumFaces; j++) {
            aiFace face = aMesh->mFaces[j];
            for (unsigned int k = 0; k < face.mNumIndices; k++) {
                indices.push_back(face.mIndices[k]);
            }
        }

        if (aMesh->mMaterialIndex >= 0) {
            std::string texPath = dir + "Zombie_Classic_sheet.png"; 
            mesh.textureID = TextureCache::GetTextureByPath(texPath);

            // 👈 新增：法线贴图
            std::string normalPath = dir + "Zombie_Classic_sheet_normal.png";
            mesh.normalMapID = TextureCache::GetTextureByPath(normalPath);            
        }

        glGenVertexArrays(1, &mesh.VAO);
        glGenBuffers(1, &mesh.VBO);
        glGenBuffers(1, &mesh.EBO);

        glBindVertexArray(mesh.VAO);
        
        glBindBuffer(GL_ARRAY_BUFFER, mesh.VBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
        
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));
        
        glEnableVertexAttribArray(2); // Bone IDs（整数）
        glVertexAttribIPointer(2, 4, GL_INT, sizeof(Vertex), (void*)offsetof(Vertex, boneIDs));
        
        glEnableVertexAttribArray(3); // Weights
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, weights));

        // 👈 新增：法线/切线/副切线
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
        
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, tangent));
        
        glEnableVertexAttribArray(6);
        glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, bitangent));

        mesh.indexCount = indices.size();
        meshes.push_back(mesh);
    }

    // ================= 解析动画 =================
    for (unsigned int i = 0; i < s_scene->mNumAnimations; i++) {
        aiAnimation* aiAnim = s_scene->mAnimations[i];
        Animation anim;
        anim.name = aiAnim->mName.C_Str();
        // mTicksPerSecond 有时候是 0，要做除法保护
        double ticksPerSecond = aiAnim->mTicksPerSecond != 0.0 ? aiAnim->mTicksPerSecond : 25.0;
        anim.duration = aiAnim->mDuration / ticksPerSecond;

        for (unsigned int j = 0; j < aiAnim->mNumChannels; j++) {
            aiNodeAnim* channel = aiAnim->mChannels[j];
            BoneAnimation boneAnim;
            boneAnim.name = channel->mNodeName.C_Str();

            for (unsigned int k = 0; k < channel->mNumPositionKeys; k++) {
                auto& key = channel->mPositionKeys[k];
                boneAnim.positions.push_back({ (float)(key.mTime / ticksPerSecond), glm::vec3(key.mValue.x, key.mValue.y, key.mValue.z) });
            }
            for (unsigned int k = 0; k < channel->mNumRotationKeys; k++) {
                auto& key = channel->mRotationKeys[k];
                boneAnim.rotations.push_back({ (float)(key.mTime / ticksPerSecond), glm::quat(key.mValue.w, key.mValue.x, key.mValue.y, key.mValue.z) });
            }
            for (unsigned int k = 0; k < channel->mNumScalingKeys; k++) {
                auto& key = channel->mScalingKeys[k];
                boneAnim.scales.push_back({ (float)(key.mTime / ticksPerSecond), glm::vec3(key.mValue.x, key.mValue.y, key.mValue.z) });
            }
            anim.boneAnimations.push_back(boneAnim);
        }
        outAnimations.push_back(anim);
        //printf("[ModelLoader] Loaded animation: %s (duration: %.2fs)\n", anim.name.c_str(), anim.duration);
    }

    return meshes;
}