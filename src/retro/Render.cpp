#include "Render.h"
#include "BVH.h"
#include "FileSystem.h"
#include <cstdio>
#include <vector>
#include <string>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

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

    // 加载贴图
    render.materialTexIds.resize(mapData.materialGroups.size(), 0);
    for (size_t i = 0; i < mapData.materialGroups.size(); ++i) {
        const std::string& name = mapData.materialGroups[i].name;
        GLuint tex = 0;
        bool loaded = false;

        // 尝试加载真实图片 (依赖 FileSystem::ResolveMaterial 返回全路径)
        // 注意：这里的路径一定要确保你放对了文件！
        std::string texPath = FileSystem::ResolveMaterial(name);
        int w, h, channels;
        unsigned char* pixels = stbi_load(texPath.c_str(), &w, &h, &channels, 4);
        if (pixels) {
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            stbi_image_free(pixels);
            loaded = true;
            printf("[Render] Loaded real texture: %s (%dx%d)\n", texPath.c_str(), w, h);
        }

        // 如果没有图，或者加载失败，生成棋盘格占位
        if (!loaded) {
            uint32_t hash = 0;
            for (char c : name) hash = hash * 131 + (uint8_t)c;
            glm::vec3 base(0.55f, 0.50f, 0.42f); // 默认石墙色
            
            const int SZ = 64;
            std::vector<uint8_t> pix(SZ * SZ * 4);
            for (int y = 0; y < SZ; ++y) {
                for (int x = 0; x < SZ; ++x) {
                    bool checker = ((x / 16) + (y / 16)) & 1;
                    float k = checker ? 1.0f : 0.82f;
                    int idx = (y * SZ + x) * 4;
                    pix[idx+0] = (uint8_t)(base.r * k * 255);
                    pix[idx+1] = (uint8_t)(base.g * k * 255);
                    pix[idx+2] = (uint8_t)(base.b * k * 255);
                    pix[idx+3] = 255;
                }
            }
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, SZ, SZ, 0, GL_RGBA, GL_UNSIGNED_BYTE, pix.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        }
        render.materialTexIds[i] = tex;
    }
    glBindVertexArray(0);
}

void ShutdownRender(RenderAssets& render) {
    glDeleteProgram(render.mapProgram);
    glDeleteProgram(render.entityProgram);
    glDeleteProgram(render.gunProgram);
    glDeleteVertexArrays(1, &render.mapVAO); glDeleteBuffers(1, &render.mapVBO); glDeleteBuffers(1, &render.mapEBO);
    glDeleteVertexArrays(1, &render.boxVAO); glDeleteBuffers(1, &render.boxVBO); glDeleteBuffers(1, &render.boxEBO);
    glDeleteVertexArrays(1, &render.gunVAO); glDeleteBuffers(1, &render.gunVBO); glDeleteBuffers(1, &render.gunEBO);
    if (render.whiteTex) glDeleteTextures(1, &render.whiteTex);
    for (GLuint t : render.materialTexIds) if (t) glDeleteTextures(1, &t);
}

void RenderFrame(RenderAssets& render, const PlayerState& player, const WeaponState& weapon, const GravityGunState& gravityGun,
                 JPH::BodyInterface& body_interface,
                 const MapData& mapData,
                 const std::vector<JPH::BodyID>& physboxIds,
                 const std::vector<ExplosiveBarrel>& barrels,
                 const std::vector<JPH::BodyID>& doorBodies,
                 const std::vector<EnemyInfo>& enemies,
                 const std::vector<BulletHole>& bulletHoles,
                 int scrW, int scrH) {
    
    glm::mat4 projection = glm::perspective(glm::radians(85.0f), (float)scrW / (float)scrH, 0.1f, 100000.0f);
    glm::vec3 eyePos = player.GetEyePos();
    glm::mat4 view = glm::lookAt(eyePos, eyePos + player.GetForward(), glm::vec3(0, 1, 0));
    glm::mat4 model = glm::mat4(1.0f);

    // ================== 1. 渲染地图 ==================
    glUseProgram(render.mapProgram);
    glDisable(GL_CULL_FACE);
    
    glm::mat4 mvp = projection * view * model;
    glUniformMatrix4fv(glGetUniformLocation(render.mapProgram, "uMVP"), 1, GL_FALSE, glm::value_ptr(mvp));
    glUniformMatrix4fv(glGetUniformLocation(render.mapProgram, "uModel"), 1, GL_FALSE, glm::value_ptr(model));
    glUniform1i(glGetUniformLocation(render.mapProgram, "uTex"), 0);

    glBindVertexArray(render.mapVAO);
    for (size_t i = 0; i < mapData.materialGroups.size(); ++i) {
        const auto& g = mapData.materialGroups[i];
        if (g.indexCount == 0) continue;
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, render.materialTexIds[i]);
        glDrawElements(GL_TRIANGLES, g.indexCount, GL_UNSIGNED_INT, (void*)(g.indexOffset * sizeof(uint32_t)));
    }

    // ================== 2. 渲染实体 ==================
    glUseProgram(render.entityProgram);
    glDisable(GL_CULL_FACE);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, render.whiteTex);

    GLint entColorLoc = glGetUniformLocation(render.entityProgram, "uColor");
    GLint entMvpLoc   = glGetUniformLocation(render.entityProgram, "uMVP");
    GLint entModelLoc = glGetUniformLocation(render.entityProgram, "uModel");

    glBindVertexArray(render.boxVAO);

    // 物理箱子
    glUniform3f(entColorLoc, 0.6f, 0.4f, 0.2f);
    for (size_t i = 0; i < physboxIds.size(); i++) {
        JPH::RMat44 t = body_interface.GetWorldTransform(physboxIds[i]);
        JPH::Vec3 pos = t.GetTranslation(); JPH::Quat q = t.GetQuaternion();
        glm::vec3 halfSize = (mapData.physboxMaxs[i] - mapData.physboxMins[i]) * 0.5f;
        glm::mat4 box_model = glm::translate(glm::mat4(1.0f), glm::vec3(pos.GetX(), pos.GetY(), pos.GetZ())) 
                            * glm::mat4_cast(glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ())) 
                            * glm::scale(glm::mat4(1.0f), halfSize);
        glUniformMatrix4fv(entMvpLoc, 1, GL_FALSE, glm::value_ptr(projection * view * box_model));
        glUniformMatrix4fv(entModelLoc, 1, GL_FALSE, glm::value_ptr(box_model));
        glDrawElements(GL_TRIANGLES, render.boxIndexCount, GL_UNSIGNED_INT, 0);
    }

    // 桶
    glUniform3f(entColorLoc, 0.8f, 0.15f, 0.1f);
    for (auto& b : barrels) {
        if (b.exploded) continue;
        JPH::RMat44 t = body_interface.GetWorldTransform(b.bodyId);
        JPH::Vec3 pos = t.GetTranslation(); JPH::Quat q = t.GetQuaternion();
        glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(pos.GetX(), pos.GetY(), pos.GetZ())) 
                    * glm::mat4_cast(glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ())) 
                    * glm::scale(glm::mat4(1.0f), glm::vec3(12.0f, 24.0f, 12.0f));
        glUniformMatrix4fv(entMvpLoc, 1, GL_FALSE, glm::value_ptr(projection * view * m));
        glUniformMatrix4fv(entModelLoc, 1, GL_FALSE, glm::value_ptr(m));
        glDrawElements(GL_TRIANGLES, render.boxIndexCount, GL_UNSIGNED_INT, 0);
    }

    // 门
    glUniform3f(entColorLoc, 0.2f, 0.6f, 0.8f);
    for (auto& d : mapData.doors) {
        glm::vec3 center = (d.mins + d.maxs) * 0.5f + d.openDir * (d.openDist * d.openAmount);
        glm::vec3 halfSize = (d.maxs - d.mins) * 0.5f;
        glm::mat4 doorModel = glm::translate(glm::mat4(1.0f), center) * glm::scale(glm::mat4(1.0f), halfSize);
        glUniformMatrix4fv(entMvpLoc, 1, GL_FALSE, glm::value_ptr(projection * view * doorModel));
        glUniformMatrix4fv(entModelLoc, 1, GL_FALSE, glm::value_ptr(doorModel));
        glDrawElements(GL_TRIANGLES, render.boxIndexCount, GL_UNSIGNED_INT, 0);
    }

    // 重力光束
    if (gravityGun.holding) {
        JPH::RVec3 bp = body_interface.GetCenterOfMassPosition(gravityGun.heldBody);
        glm::vec3 boxPos((float)bp.GetX(), (float)bp.GetY(), (float)bp.GetZ());
        glm::vec3 mid = (eyePos + boxPos) * 0.5f;
        float len = glm::length(boxPos - eyePos);
        glm::vec3 dir = glm::normalize(boxPos - eyePos);
        glm::vec3 up(0,1,0);
        if (fabsf(glm::dot(up, dir)) > 0.99f) up = glm::vec3(1,0,0);
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

    // 敌人
    for (auto& enemy : enemies) {
        if (enemy.state == EnemyState::DEAD && enemy.deathTimer > 10.0f) continue;
        if (enemy.state == EnemyState::DEAD) {
            glUniform3f(entColorLoc, 0.2f, 0.1f, 0.1f); 
        } else if (enemy.state == EnemyState::STUNNED) {
            glUniform3f(entColorLoc, 1.0f, 1.0f, 0.0f);
        } else if (enemy.state == EnemyState::ATTACK) {
            glUniform3f(entColorLoc, 1.0f, 0.0f, 0.0f);
        } else {
            glUniform3f(entColorLoc, 0.8f, 0.2f, 0.2f);
        }

        glm::mat4 enemyModel = glm::translate(glm::mat4(1.0f), enemy.pos);
        if (enemy.state == EnemyState::DEAD) {
            if (enemy.bodyId.IsInvalid()) continue;
            enemyModel = glm::rotate(enemyModel, glm::radians(90.0f), glm::vec3(1,0,0)) * glm::scale(glm::mat4(1.0f), glm::vec3(enemy.radius, enemy.radius, enemy.height * 0.5f));
        } else {
            enemyModel = enemyModel * glm::scale(glm::mat4(1.0f), glm::vec3(enemy.radius, enemy.height * 0.5f, enemy.radius));
        }
        glUniformMatrix4fv(entMvpLoc, 1, GL_FALSE, glm::value_ptr(projection * view * enemyModel));
        glUniformMatrix4fv(entModelLoc, 1, GL_FALSE, glm::value_ptr(enemyModel));
        glDrawElements(GL_TRIANGLES, render.boxIndexCount, GL_UNSIGNED_INT, 0);
    }

    // 弹孔
    glUniform3f(entColorLoc, 0.1f, 0.1f, 0.1f);
    for (auto& bh : bulletHoles) {
        glm::mat4 bulletModel = glm::translate(glm::mat4(1.0f), bh.pos) * glm::scale(glm::mat4(1.0f), glm::vec3(0.5f));
        glUniformMatrix4fv(entMvpLoc, 1, GL_FALSE, glm::value_ptr(projection * view * bulletModel));
        glUniformMatrix4fv(entModelLoc, 1, GL_FALSE, glm::value_ptr(bulletModel));
        glDrawElements(GL_TRIANGLES, render.boxIndexCount, GL_UNSIGNED_INT, 0);
    }

    // ================== 3. 渲染武器 ==================
    glUseProgram(render.gunProgram);
    glm::mat4 gunProj = glm::perspective(glm::radians(60.0f), (float)scrW / (float)scrH, 0.05f, 10.0f);
    
    float recoilOffset = 0.0f, recoilPitch = 0.0f;
    if (weapon.recoilTimer > 0.0f) {
        float progress = weapon.recoilTimer / weapon.recoilDuration;
        recoilOffset = progress * 0.08f;
        recoilPitch = progress * 10.0f;
    }
    glm::mat4 gunBase = glm::translate(glm::mat4(1.0f), glm::vec3(0.25f, -0.25f, -0.7f + recoilOffset));
    gunBase = glm::rotate(gunBase, glm::radians(recoilPitch), glm::vec3(1, 0, 0));
    gunBase = glm::rotate(gunBase, glm::radians(-5.0f), glm::vec3(0, 1, 0));

    GLint gunColorLoc = glGetUniformLocation(render.gunProgram, "uColor");
    GLint gunMvpLoc   = glGetUniformLocation(render.gunProgram, "uMVP");
    GLint gunModelLoc = glGetUniformLocation(render.gunProgram, "uModel");

    auto DrawGunPart = [&](glm::vec3 offset, glm::vec3 scale) {
        glm::mat4 partModel = glm::translate(gunBase, offset) * glm::scale(glm::mat4(1.0f), scale);
        glm::mat4 partMvp = gunProj * partModel;
        glUniformMatrix4fv(gunMvpLoc, 1, GL_FALSE, glm::value_ptr(partMvp));
        glUniformMatrix4fv(gunModelLoc, 1, GL_FALSE, glm::value_ptr(partModel));
        glBindVertexArray(render.boxVAO);
        glDrawElements(GL_TRIANGLES, render.boxIndexCount, GL_UNSIGNED_INT, 0);
    };

    glUniform3f(gunColorLoc, 0.25f, 0.25f, 0.28f);

    // 枪管（圆柱体）
    glm::mat4 barrelModel = glm::translate(gunBase, glm::vec3(0.0f, 0.02f, -0.3f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.008f, 0.008f, 0.4f));
    glUniformMatrix4fv(gunMvpLoc, 1, GL_FALSE, glm::value_ptr(gunProj * barrelModel));
    glUniformMatrix4fv(gunModelLoc, 1, GL_FALSE, glm::value_ptr(barrelModel));
    glBindVertexArray(render.gunVAO);
    glDrawElements(GL_TRIANGLES, render.gunIndexCount, GL_UNSIGNED_INT, 0);

    glBindVertexArray(render.boxVAO);
    DrawGunPart(glm::vec3(0.0f, 0.0f, -0.05f), glm::vec3(0.035f, 0.05f, 0.25f));
    DrawGunPart(glm::vec3(0.0f, -0.08f, 0.02f), glm::vec3(0.02f, 0.08f, 0.04f));
    DrawGunPart(glm::vec3(0.0f, -0.06f, 0.1f), glm::vec3(0.025f, 0.06f, 0.03f));
    DrawGunPart(glm::vec3(0.0f, 0.05f, -0.3f), glm::vec3(0.005f, 0.015f, 0.02f));
    
    // 枪口火焰
    if (weapon.recoilTimer > 0.05f) { 
        glUniform3f(gunColorLoc, 1.0f, 0.8f, 0.2f); 
        DrawGunPart(glm::vec3(0.0f, 0.02f, -0.6f), glm::vec3(0.04f, 0.04f, 0.04f));
    }
}