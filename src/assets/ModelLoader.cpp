#include "ModelLoader.h"
#include "TextureCache.h"
#include <GL/glew.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <iostream>

std::vector<Mesh> ModelLoader::LoadModel(const std::string& path) {
    std::vector<Mesh> meshes;
    Assimp::Importer importer;
    
    const aiScene* scene = importer.ReadFile(path, 
        aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenSmoothNormals);
    
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cerr << "[ModelLoader] Assimp error: " << importer.GetErrorString() << std::endl;
        return meshes;
    }

    // 提取模型所在目录（比如 "assets/models/zombie/"）
    std::string dir = path.substr(0, path.find_last_of('/') + 1);

    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        aiMesh* aMesh = scene->mMeshes[i];
        Mesh mesh;
        
        std::vector<float> vertices;
        std::vector<unsigned int> indices;

        for (unsigned int j = 0; j < aMesh->mNumVertices; j++) {
            vertices.push_back(aMesh->mVertices[j].x);
            vertices.push_back(aMesh->mVertices[j].y);
            vertices.push_back(aMesh->mVertices[j].z);
            
            if (aMesh->mTextureCoords[0]) {
                vertices.push_back(aMesh->mTextureCoords[0][j].x);
                vertices.push_back(aMesh->mTextureCoords[0][j].y);
            } else {
                vertices.push_back(0.0f);
                vertices.push_back(0.0f);
            }
        }

        for (unsigned int j = 0; j < aMesh->mNumFaces; j++) {
            aiFace face = aMesh->mFaces[j];
            for (unsigned int k = 0; k < face.mNumIndices; k++) {
                indices.push_back(face.mIndices[k]);
            }
        }

       // 贴图处理：无视 FBX 里的空路径，直接读取你解压出来的贴图
        if (aMesh->mMaterialIndex >= 0) {
            // 直接指定我们之前放的贴图名字，拼接在模型同级目录下
            std::string texPath = dir + "Zombie_Classic_sheet.png"; 
            mesh.textureID = TextureCache::GetTextureByPath(texPath);
            printf("[ModelLoader] Manually bound texture: %s\n", texPath.c_str());
        }

        glGenVertexArrays(1, &mesh.VAO);
        glGenBuffers(1, &mesh.VBO);
        glGenBuffers(1, &mesh.EBO);

        glBindVertexArray(mesh.VAO);
        
        glBindBuffer(GL_ARRAY_BUFFER, mesh.VBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));

        mesh.indexCount = indices.size();
        meshes.push_back(mesh);
        
        std::cout << "[ModelLoader] Loaded mesh " << i << " with " << mesh.indexCount << " indices." << std::endl;
    }
    return meshes;
}