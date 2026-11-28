#include "Render.h"
#include "BVH.h"
#include "FileSystem.h"
#include "TextureCache.h"     // 👈 新增
#include "AnimationHelper.h"

#ifdef _MSC_VER
#include "SDL.h"
#else
#include <SDL2/SDL.h>
#endif

#include <cstdio>
#include <vector>
#include <string>

#include "shaders_generated.h"

void InitRender(RenderAssets& render, const MapData& mapData) {
    glGenVertexArrays(1, &render.mapVAO); glGenBuffers(1, &render.mapVBO); glGenBuffers(1, &render.mapEBO);
    glBindVertexArray(render.mapVAO);
    glBindBuffer(GL_ARRAY_BUFFER, render.mapVBO); glBufferData(GL_ARRAY_BUFFER, mapData.verts.size() * 4, mapData.verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, render.mapEBO); glBufferData(GL_ELEMENT_ARRAY_BUFFER, mapData.indices.size() * 4, mapData.indices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)(9 * sizeof(float)));

    // ================= 编译三个 Program =================
    auto compile_shader = [](GLenum type, const char* src) {
        GLuint s = glCreateShader(type); 
        glShaderSource(s, 1, &src, nullptr); 
        glCompileShader(s); 
        // 可以加个错误检查
        GLint success;
        glGetShaderiv(s, GL_COMPILE_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetShaderInfoLog(s, 512, nullptr, infoLog);
            printf("[Shader] Compile error: %s\n", infoLog);
        }
        return s;
    };
    auto link_program = [&](const char* vs_src, const char* fs_src) {
        GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
        GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
        GLuint prog = glCreateProgram();
        glAttachShader(prog, vs); glAttachShader(prog, fs); glLinkProgram(prog);
        glDeleteShader(vs); glDeleteShader(fs);
        return prog;
    };

    render.mapProgram    = link_program(Shaders::game_common_vert, Shaders::game_map_frag);
    render.entityProgram = link_program(Shaders::game_common_vert, Shaders::game_entity_frag);
    render.gunProgram    = link_program(Shaders::game_common_vert, Shaders::game_gun_frag);
    render.skyboxProgram = link_program(Shaders::skybox_vert, Shaders::skybox_frag);
    render.modelProgram = link_program(Shaders::entity_skinned_vert, Shaders::entity_model_frag);

    // Box Geometry
    std::vector<float> box_verts = {
        -1,-1,-1, 0,0,-1,   1,-1,-1, 0,0,-1,   1, 1,-1, 0,0,-1,  -1, 1,-1, 0,0,-1,
        -1,-1, 1, 0,0, 1,   1,-1, 1, 0,0, 1,   1, 1, 1, 0,0, 1,  -1, 1, 1, 0,0, 1,
        -1,-1,-1, -1,0,0,  -1,-1, 1, -1,0,0,  -1, 1, 1, -1,0,0,  -1, 1,-1, -1,0,0,
         1,-1,-1,  1,0,0,   1,-1, 1,  1,0,0,   1, 1, 1,  1,0,0,   1, 1,-1,  1,0,0,
        -1, 1,-1, 0,1,0,   1, 1,-1, 0,1,0,   1, 1, 1, 0,1,0,  -1, 1, 1, 0,1,0,
        -1,-1,-1, 0,-1,0,  -1,-1, 1, 0,-1,0,  1,-1, 1, 0,-1,0,  1,-1,-1, 0,-1,0,
    };
    std::vector<uint32_t> box_idx = {
        0,1,2, 0,2,3,  4,5,6, 4,6,7,  8,9,10, 8,10,11,  12,13,14, 12,14,15,  16,17,18, 16,18,19,  20,21,22, 20,22,23,
    };
    render.boxIndexCount = (GLuint)box_idx.size();
    glGenVertexArrays(1, &render.boxVAO); glGenBuffers(1, &render.boxVBO); glGenBuffers(1, &render.boxEBO);
    glBindVertexArray(render.boxVAO);
    glBindBuffer(GL_ARRAY_BUFFER, render.boxVBO); glBufferData(GL_ARRAY_BUFFER, box_verts.size() * 4, box_verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, render.boxEBO); glBufferData(GL_ELEMENT_ARRAY_BUFFER, box_idx.size() * 4, box_idx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));

    // Gun Geometry
    std::vector<float> gun_verts; std::vector<uint32_t> gun_idx;
    int segments = 16; float radius = 0.1f; float length = 1.0f;
    for (int i = 0; i <= segments; i++) {
        float angle = (float)i / segments * 2.0f * 3.14159265f;
        float x = cosf(angle) * radius; float y = sinf(angle) * radius;
        float nx = cosf(angle); float ny = sinf(angle);
        gun_verts.insert(gun_verts.end(), {x, y, -length, nx, ny, 0.0f, x, y, 0.0f, nx, ny, 0.0f});
    }
    for (int i = 0; i < segments; i++) {
        int base = i * 2;
        gun_idx.insert(gun_idx.end(), {(uint32_t)base, (uint32_t)base+1, (uint32_t)base+2, (uint32_t)base+1, (uint32_t)base+3, (uint32_t)base+2});
    }
    render.gunIndexCount = (GLuint)gun_idx.size();
    glGenVertexArrays(1, &render.gunVAO); glGenBuffers(1, &render.gunVBO); glGenBuffers(1, &render.gunEBO);
    glBindVertexArray(render.gunVAO);
    glBindBuffer(GL_ARRAY_BUFFER, render.gunVBO); glBufferData(GL_ARRAY_BUFFER, gun_verts.size() * 4, gun_verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, render.gunEBO); glBufferData(GL_ELEMENT_ARRAY_BUFFER, gun_idx.size() * 4, gun_idx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    
    // White Texture (兜底，防止非地图物体绑定空纹理变黑)
    uint8_t whitePixel[4] = {255,255,255,255};
    glGenTextures(1, &render.whiteTex);
    glBindTexture(GL_TEXTURE_2D, render.whiteTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // 加载贴图（统一走 TextureCache，去重 + 缓存 + fallback 都在里面）
    TextureCache::Init();
    render.materialTexIds.resize(mapData.materialGroups.size(), 0);
    for (size_t i = 0; i < mapData.materialGroups.size(); ++i) {
        render.materialTexIds[i] =
            TextureCache::GetTexture(mapData.materialGroups[i].name);
    }
    glBindVertexArray(0);  

    // 生成立方体顶点数据 (36 个顶点)
    float skyboxVertices[] = {
        -1.0f,  1.0f, -1.0f,  -1.0f, -1.0f, -1.0f,   1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,   1.0f,  1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,  -1.0f, -1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,  -1.0f,  1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f, -1.0f,   1.0f, -1.0f,  1.0f,   1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,   1.0f,  1.0f, -1.0f,   1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,   1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,   1.0f, -1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,
        -1.0f,  1.0f, -1.0f,   1.0f,  1.0f, -1.0f,   1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,  -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f, -1.0f,  -1.0f, -1.0f,  1.0f,   1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,  -1.0f, -1.0f,  1.0f,   1.0f, -1.0f,  1.0f
    };
    glGenVertexArrays(1, &render.skyboxVAO);
    glGenBuffers(1, &render.skyboxVBO);
    glBindVertexArray(render.skyboxVAO);
    glBindBuffer(GL_ARRAY_BUFFER, render.skyboxVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), &skyboxVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    std::string prefix = "skybox/" + mapData.skyName;
    std::vector<std::string> faces = {
        prefix + "rt", prefix + "lf",
        prefix + "up", prefix + "dn",
        prefix + "bk", prefix + "ft"
    };
    render.skyboxTex = TextureCache::GetCubemap(faces);

    // 加载丧尸模型
    std::string modelPath = "models/zombie/zombie.gltf";
    // 清空骨骼数据并加载
    render.boneCounter = 0;
    render.boneMap.clear();
    render.zombieAnims.clear();
    render.zombieModel = ModelLoader::LoadModel(modelPath, render.boneMap, render.boneCounter, render.zombieAnims);
    printf("[Render] Loaded model with %d bones.\n", render.boneCounter);

    render.weaponModel = LoadWeaponModel("models/weapon/scene.gltf");
}

void ShutdownRender(RenderAssets& render) {
    glDeleteProgram(render.mapProgram);
    glDeleteProgram(render.entityProgram);
    glDeleteProgram(render.gunProgram);
    glDeleteProgram(render.skyboxProgram);
    glDeleteVertexArrays(1, &render.mapVAO); glDeleteBuffers(1, &render.mapVBO); glDeleteBuffers(1, &render.mapEBO);
    glDeleteVertexArrays(1, &render.boxVAO); glDeleteBuffers(1, &render.boxVBO); glDeleteBuffers(1, &render.boxEBO);
    glDeleteVertexArrays(1, &render.gunVAO); glDeleteBuffers(1, &render.gunVBO); glDeleteBuffers(1, &render.gunEBO);
    glDeleteVertexArrays(1, &render.skyboxVAO); glDeleteBuffers(1, &render.skyboxVBO);
    if (render.whiteTex) glDeleteTextures(1, &render.whiteTex);
    if (render.skyboxTex) glDeleteTextures(1, &render.skyboxTex);  
    TextureCache::Shutdown();   // 👈 新增
    render.materialTexIds.clear();
}

void RenderFrame(RenderAssets& render, const PlayerState& player, const GameRenderSnapshot& snapshot,
                 const MapData& mapData, const std::vector<BulletHole>& bulletHoles, int scrW, int scrH) {
    
    glm::mat4 projection = glm::perspective(glm::radians(85.0f), (float)scrW / (float)scrH, 0.1f, 100000.0f);
    glm::vec3 eyePos = player.GetEyePos();
    glm::mat4 view = glm::lookAt(eyePos, eyePos + player.GetForward(), glm::vec3(0, 1, 0));
    glm::mat4 model = glm::mat4(1.0f);

    glViewport(0, 0, scrW, scrH);
    glClearColor(0.12f, 0.14f, 0.18f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);


    // 渲染天空盒
    glDepthFunc(GL_LEQUAL);
    glUseProgram(render.skyboxProgram);
    glDepthMask(GL_FALSE); 
    
    glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(view));
    glUniformMatrix4fv(glGetUniformLocation(render.skyboxProgram, "uProjection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniformMatrix4fv(glGetUniformLocation(render.skyboxProgram, "uView"), 1, GL_FALSE, glm::value_ptr(viewNoTranslation));
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, render.skyboxTex);
    glUniform1i(glGetUniformLocation(render.skyboxProgram, "uSkybox"), 0);
    
    glBindVertexArray(render.skyboxVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);

    // ================== 1. 渲染地图 ==================
    glUseProgram(render.mapProgram);
    glDisable(GL_CULL_FACE);
    
    glm::mat4 mvp = projection * view * model;
    glUniformMatrix4fv(glGetUniformLocation(render.mapProgram, "uMVP"), 1, GL_FALSE, glm::value_ptr(mvp));
    glUniformMatrix4fv(glGetUniformLocation(render.mapProgram, "uModel"), 1, GL_FALSE, glm::value_ptr(model));
    glUniform1i(glGetUniformLocation(render.mapProgram, "uTex"), 0);
    
    GLint alphaTestLoc = glGetUniformLocation(render.mapProgram, "uAlphaTest");

    glBindVertexArray(render.mapVAO);
    for (size_t i = 0; i != mapData.materialGroups.size(); ++i) {
        if (mapData.materialGroups[i].name.find("toolsskybox") != std::string::npos) continue;
        const auto& g = mapData.materialGroups[i];
        if (g.indexCount == 0) continue;

        std::string matName = g.name;
        std::transform(matName.begin(), matName.end(), matName.begin(), ::tolower);
        
        bool isAlpha = false;
        if (matName.find("glass") != std::string::npos ||
            matName.find("grate") != std::string::npos ||
            matName.find("fence") != std::string::npos ||
            matName.find("wire") != std::string::npos ||
            matName.find("tree") != std::string::npos ||
            matName.find("window") != std::string::npos) {
            isAlpha = true;
        }

        glUniform1i(alphaTestLoc, isAlpha ? 1 : 0);

        glActiveTexture(GL_TEXTURE0);
        GLuint tex = render.materialTexIds[i] ? render.materialTexIds[i] : render.whiteTex;
        glBindTexture(GL_TEXTURE_2D, tex);
        glDrawElements(GL_TRIANGLES, g.indexCount, GL_UNSIGNED_INT, (void*)(g.indexOffset * sizeof(uint32_t)));
    }

    // ================== 2. 渲染实体 (POD Snapshot) ==================
    glUseProgram(render.entityProgram);
    glDisable(GL_CULL_FACE);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, render.whiteTex);

    GLint entColorLoc = glGetUniformLocation(render.entityProgram, "uColor");
    GLint entMvpLoc   = glGetUniformLocation(render.entityProgram, "uMVP");
    GLint entModelLoc = glGetUniformLocation(render.entityProgram, "uModel");

    glBindVertexArray(render.boxVAO);

    for (int i = 0; i != snapshot.boxCount; ++i) {
        const GameRenderBox& box = snapshot.boxes[i];
        if (box.entityType == 3) continue; // 丧尸下面用骨骼模型渲染

        glUniform3f(entColorLoc, box.color.r, box.color.g, box.color.b);
        glm::mat4 box_model = glm::translate(glm::mat4(1.0f), box.center)
                            * glm::rotate(glm::mat4(1.0f), glm::radians(box.yaw), glm::vec3(0.0f, 1.0f, 0.0f))
                            * glm::scale(glm::mat4(1.0f), box.halfExtents);
        glUniformMatrix4fv(entMvpLoc, 1, GL_FALSE, glm::value_ptr(projection * view * box_model));
        glUniformMatrix4fv(entModelLoc, 1, GL_FALSE, glm::value_ptr(box_model));
        glDrawElements(GL_TRIANGLES, render.boxIndexCount, GL_UNSIGNED_INT, 0);
    }

    // 重力光束
    if (snapshot.holdingObject) {
        glm::vec3 boxPos = snapshot.heldCenter;
        glm::vec3 mid = (eyePos + boxPos) * 0.5f;
        float len = glm::length(boxPos - eyePos);
        if (len > 0.1f) {
            glm::vec3 dir = glm::normalize(boxPos - eyePos);
            glm::vec3 up(0, 1, 0);
            if (fabsf(glm::dot(up, dir)) > 0.99f) up = glm::vec3(1, 0, 0);
            glm::vec3 right = glm::normalize(glm::cross(up, dir));
            up = glm::cross(dir, right);
            glm::mat4 beam = glm::translate(glm::mat4(1.0f), mid);
            beam[0] = glm::vec4(right, 0); beam[1] = glm::vec4(up, 0); beam[2] = glm::vec4(dir, 0);
            beam = beam * glm::scale(glm::mat4(1.0f), glm::vec3(0.6f, 0.6f, len * 0.5f));
            glUniform3f(entColorLoc, 0.3f, 0.8f, 1.0f);
            glUniformMatrix4fv(entMvpLoc, 1, GL_FALSE, glm::value_ptr(projection * view * beam));
            glUniformMatrix4fv(entModelLoc, 1, GL_FALSE, glm::value_ptr(beam));
            glDrawElements(GL_TRIANGLES, render.boxIndexCount, GL_UNSIGNED_INT, 0);
        }
    }

    // 敌人（丧尸骨骼模型）
    glUseProgram(render.modelProgram); 
    glDisable(GL_CULL_FACE);

    glm::vec3 sunDir   = glm::normalize(glm::vec3(-0.3f, -1.0f, -0.5f));
    glm::vec3 sunColor = glm::vec3(0.55f, 0.65f, 0.85f);
    glm::vec3 ambient  = glm::vec3(0.18f, 0.20f, 0.25f);

    glUniform3fv(glGetUniformLocation(render.modelProgram, "uSunDir"),   1, &sunDir[0]);
    glUniform3fv(glGetUniformLocation(render.modelProgram, "uSunColor"), 1, &sunColor[0]);
    glUniform3fv(glGetUniformLocation(render.modelProgram, "uAmbient"),  1, &ambient[0]);
    glUniform3fv(glGetUniformLocation(render.modelProgram, "uCameraPos"),1, &eyePos[0]);

    float globalTime = (float)SDL_GetTicks() / 1000.0f;

    for (int i = 0; i != snapshot.boxCount; ++i) {
        const GameRenderBox& box = snapshot.boxes[i];
        if (box.entityType != 3) continue;
        
        glm::vec3 feetPos = box.center - glm::vec3(0.0f, box.halfExtents.y - 10.0f, 0.0f);
        glm::mat4 enemyModel = glm::mat4(1.0f);
        enemyModel = glm::translate(enemyModel, feetPos);
        
        // 你的朝向旋转
        enemyModel = glm::rotate(enemyModel, glm::radians(box.yaw), glm::vec3(0.0f, 1.0f, 0.0f));

        enemyModel = glm::scale(enemyModel, glm::vec3(1.0f)); 

        glm::mat4 enemyMvp = projection * view * enemyModel;
        glUniformMatrix4fv(glGetUniformLocation(render.modelProgram, "uMVP"),   1, GL_FALSE, glm::value_ptr(enemyMvp));
        glUniformMatrix4fv(glGetUniformLocation(render.modelProgram, "uModel"), 1, GL_FALSE, glm::value_ptr(enemyModel));

        std::vector<glm::mat4> boneMatrices(100, glm::mat4(1.0f));
        int animIdx = render.FindAnimIndex(box.animState);
        
        if (animIdx >= 0 && animIdx != (int)render.zombieAnims.size() && ModelLoader::s_scene) {
            const auto& anim = render.zombieAnims[animIdx];
            float animTime = globalTime;
            if (anim.duration > 0.0f) animTime = fmod(animTime, anim.duration);

            AnimHelper::BuildSkinningMatrices(
                ModelLoader::s_scene->mRootNode,
                anim, animTime,
                render.boneMap,
                boneMatrices
            );
        }

        glUniformMatrix4fv(glGetUniformLocation(render.modelProgram, "uBones"), 100, GL_FALSE, glm::value_ptr(boneMatrices[0]));

        for (const auto& mesh : render.zombieModel) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, mesh.textureID);
            glUniform1i(glGetUniformLocation(render.modelProgram, "uTex"), 0);

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, mesh.normalMapID ? mesh.normalMapID : render.whiteTex);
            glUniform1i(glGetUniformLocation(render.modelProgram, "uNormalMap"), 1);

            glBindVertexArray(mesh.VAO);
            glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, 0);
        }
    }

    // 切回实体 Shader 画弹孔
    glUseProgram(render.entityProgram);
    glBindVertexArray(render.boxVAO);

    glUniform3f(entColorLoc, 0.1f, 0.1f, 0.1f);
    for (auto& bh : bulletHoles) {
        glm::mat4 bulletModel = glm::translate(glm::mat4(1.0f), bh.pos) * glm::scale(glm::mat4(1.0f), glm::vec3(0.5f));
        glUniformMatrix4fv(entMvpLoc, 1, GL_FALSE, glm::value_ptr(projection * view * bulletModel));
        glUniformMatrix4fv(entModelLoc, 1, GL_FALSE, glm::value_ptr(bulletModel));
        glDrawElements(GL_TRIANGLES, render.boxIndexCount, GL_UNSIGNED_INT, 0);
    }

    // ================== 3. 渲染武器 ==================
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glUseProgram(render.gunProgram);
    glm::mat4 gunProj = glm::perspective(glm::radians(60.0f), (float)scrW / (float)scrH, 0.05f, 10.0f);
    
    float recoilOffset = 0.0f, recoilPitch = 0.0f;
    if (snapshot.recoilTimer > 0.0f && snapshot.recoilDuration > 0.0f) {
        float progress = snapshot.recoilTimer / snapshot.recoilDuration;
        recoilOffset = progress * -0.05f;
        recoilPitch = progress * 10.0f;
    }
    
    glm::mat4 gunBase = glm::translate(glm::mat4(1.0f), glm::vec3(0.2f, -0.3f, -0.8f + recoilOffset));
    gunBase = glm::rotate(gunBase, glm::radians(-80.0f), glm::vec3(0, 1, 0));
    gunBase = glm::rotate(gunBase, glm::radians(recoilPitch), glm::vec3(1, 0, 0));
    gunBase = glm::rotate(gunBase, glm::radians(-5.0f), glm::vec3(0, 1, 0));
    gunBase = glm::scale(gunBase, glm::vec3(0.08f)); 

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, render.whiteTex);
    glUniform1i(glGetUniformLocation(render.gunProgram, "uTex"), 0);
    glUniform3f(glGetUniformLocation(render.gunProgram, "uColor"), 1.0f, 1.0f, 1.0f);
    glUniform3fv(glGetUniformLocation(render.gunProgram, "uCameraPos"), 1, glm::value_ptr(eyePos));
    
    glm::vec3 lightDir = glm::normalize(glm::vec3(-0.3f, -1.0f, -0.5f));
    glUniform3fv(glGetUniformLocation(render.gunProgram, "uLightDir"), 1, glm::value_ptr(lightDir));
    glm::vec3 lightColor = glm::vec3(1.0f, 0.95f, 0.9f);
    glUniform3fv(glGetUniformLocation(render.gunProgram, "uLightColor"), 1, glm::value_ptr(lightColor));
    for (const auto& mesh : render.weaponModel.meshes) {
        glm::mat4 gunMvp = gunProj * gunBase;
        glUniformMatrix4fv(glGetUniformLocation(render.gunProgram, "uMVP"), 1, GL_FALSE, glm::value_ptr(gunMvp));
        glUniformMatrix4fv(glGetUniformLocation(render.gunProgram, "uModel"), 1, GL_FALSE, glm::value_ptr(gunBase));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, mesh.textureID ? mesh.textureID : render.whiteTex);
        glBindVertexArray(mesh.VAO);
        glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, 0);
    }
}