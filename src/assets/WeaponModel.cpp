#include "WeaponModel.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <iostream>
#include "stb_image.h" // 确保没有定义 STB_IMAGE_IMPLEMENTATION

void ProcessNode(aiNode* node, const aiScene* scene, std::vector<WeaponMesh>& meshes) {
    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        
        std::vector<float> vertices;
        std::vector<unsigned int> indices;

        // --- 1. 填充顶点数据 (顺序: Pos3, Normal3, UV2) ---
        for (unsigned int j = 0; j < mesh->mNumVertices; j++) {
            // Pos
            vertices.push_back(mesh->mVertices[j].x);
            vertices.push_back(mesh->mVertices[j].y);
            vertices.push_back(mesh->mVertices[j].z);
            // Normal
            if (mesh->HasNormals()) {
                vertices.push_back(mesh->mNormals[j].x);
                vertices.push_back(mesh->mNormals[j].y);
                vertices.push_back(mesh->mNormals[j].z);
            } else {
                vertices.push_back(0.0f); vertices.push_back(1.0f); vertices.push_back(0.0f);
            }
            // UV
            if (mesh->mTextureCoords[0]) {
                vertices.push_back(mesh->mTextureCoords[0][j].x);
                vertices.push_back(mesh->mTextureCoords[0][j].y);
            } else {
                vertices.push_back(0.0f); vertices.push_back(0.0f);
            }
        }

        // --- 2. 填充索引 ---
        for (unsigned int j = 0; j < mesh->mNumFaces; j++) {
            aiFace face = mesh->mFaces[j];
            for (unsigned int k = 0; k < face.mNumIndices; k++) {
                indices.push_back(face.mIndices[k]);
            }
        }

        // --- 3. 创建 OpenGL 缓冲区 ---
        WeaponMesh wm;
        wm.indexCount = indices.size();
        wm.textureID = 0;

        glGenVertexArrays(1, &wm.VAO);
        glGenBuffers(1, &wm.VBO);
        glGenBuffers(1, &wm.EBO);

        glBindVertexArray(wm.VAO);
        glBindBuffer(GL_ARRAY_BUFFER, wm.VBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, wm.EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

        // --- 4. 绑定顶点属性 (对齐 game_common.vert) ---
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
        
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
        
        glEnableVertexAttribArray(2);
        glVertexAttrib3f(2, 1.0f, 1.0f, 1.0f); // 强制 aColor 为白
        
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
        
        glBindVertexArray(0);

        // --- 5. 加载材质贴图 (硬编码 BaseColor) ---
        // 因为我们用了 gltf，并且贴图是独立文件，直接强行加载颜色贴图
        std::string texPath = "models/weapon/textures/Material_baseColor.png";
        
        int w, h, n;
        // glTF 的 UV 通常需要上下翻转
        stbi_set_flip_vertically_on_load(true); 
        unsigned char* data = stbi_load(texPath.c_str(), &w, &h, &n, 4);
        
        if (data) {
            glGenTextures(1, &wm.textureID);
            glBindTexture(GL_TEXTURE_2D, wm.textureID);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            stbi_image_free(data);
            printf("[WeaponModel] Loaded BaseColor for Mesh %u\n", i);
        } else {
            printf("[WeaponModel] ERROR: Failed to load %s\n", texPath.c_str());
        }

        meshes.push_back(wm);
    }

    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        ProcessNode(node->mChildren[i], scene, meshes);
    }
}

WeaponModelData LoadWeaponModel(const std::string& path) {
    WeaponModelData result;
    Assimp::Importer importer;
    // 这里不需要 aiProcess_FlipUVs，因为我们在 stb_image 里处理了翻转
    const aiScene* scene = importer.ReadFile(path, 
        aiProcess_Triangulate | aiProcess_GenSmoothNormals);
    
    if (!scene || !scene->mRootNode) {
        std::cerr << "[WeaponModel] Failed to load: " << path << std::endl;
        return result;
    }

    ProcessNode(scene->mRootNode, scene, result.meshes);
    std::cout << "[WeaponModel] Loaded " << result.meshes.size() << " meshes from " << path << std::endl;
    
    return result;
}