#include "Render.h"
#include "BVH.h"

void InitRender(RenderAssets& render, const MapData& mapData) {
    glGenVertexArrays(1, &render.mapVAO); glGenBuffers(1, &render.mapVBO); glGenBuffers(1, &render.mapEBO);
    glBindVertexArray(render.mapVAO);
    glBindBuffer(GL_ARRAY_BUFFER, render.mapVBO); glBufferData(GL_ARRAY_BUFFER, mapData.verts.size() * 4, mapData.verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, render.mapEBO); glBufferData(GL_ELEMENT_ARRAY_BUFFER, mapData.indices.size() * 4, mapData.indices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(6 * sizeof(float)));

    const char* vs_src = R"(
        #version 330 core
        layout(location=0) in vec3 aPos;
        layout(location=1) in vec3 aNormal;
        layout(location=2) in vec3 aColor;
        uniform mat4 uMVP;
        uniform mat4 uModel;
        out vec3 vNormal;
        out vec3 vWorldPos;
        out vec3 vColor;
        void main() {
            gl_Position = uMVP * vec4(aPos, 1.0);
            vNormal   = mat3(uModel) * aNormal;
            vWorldPos = vec3(uModel * vec4(aPos, 1.0));
            vColor    = aColor;
        }
    )";
    const char* fs_src = R"(
        #version 330 core
        in vec3 vNormal;
        in vec3 vWorldPos;
        in vec3 vColor;
        out vec4 FragColor;
        uniform vec3 uColor;
        uniform int  uMode;
        uniform int  uUseVColor;

        void main() {
            vec3 baseColor = uColor;

            if (uMode == 1) {
                // 棋盘格纹理
                vec3 blend = abs(vNormal); blend /= (blend.x + blend.y + blend.z);
                vec3 p = vWorldPos * 0.05;
                float checkerX = mod(floor(p.y) + floor(p.z), 2.0);
                float checkerY = mod(floor(p.x) + floor(p.z), 2.0);
                float checkerZ = mod(floor(p.x) + floor(p.y), 2.0);
                float checker = checkerX * blend.x + checkerY * blend.y + checkerZ * blend.z;
                baseColor = mix(vec3(0.6, 0.6, 0.65), vec3(0.4, 0.4, 0.45), checker);
            } else if (uMode == 2) {
                baseColor = vec3(0.25, 0.25, 0.28);
            }

            // 👈 如果是地图，直接输出 基础色 * 烘焙的顶点光照
            if (uUseVColor == 1) {
                FragColor = vec4(baseColor * vColor, 1.0); 
            } else {
                // 如果是箱子、敌人、枪，保留一点点简单的方向光，免得完全没立体感
                vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
                float diff = max(dot(vNormal, lightDir), 0.0) * 0.5 + 0.5;
                FragColor = vec4(baseColor * diff, 1.0);
            }
        }
    )";
    auto compile_shader = [](GLenum type, const char* src) {
        GLuint s = glCreateShader(type); glShaderSource(s, 1, &src, nullptr); glCompileShader(s); return s;
    };
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    render.program = glCreateProgram();
    glAttachShader(render.program, vs); glAttachShader(render.program, fs); glLinkProgram(render.program);
    glDeleteShader(vs); glDeleteShader(fs);

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
    glBindVertexArray(0);
}

void ShutdownRender(RenderAssets& render) {
    glDeleteProgram(render.program);
    glDeleteVertexArrays(1, &render.mapVAO); glDeleteBuffers(1, &render.mapVBO); glDeleteBuffers(1, &render.mapEBO);
    glDeleteVertexArrays(1, &render.boxVAO); glDeleteBuffers(1, &render.boxVBO); glDeleteBuffers(1, &render.boxEBO);
    glDeleteVertexArrays(1, &render.gunVAO); glDeleteBuffers(1, &render.gunVBO); glDeleteBuffers(1, &render.gunEBO);
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
    
    glUseProgram(render.program);
    GLint uUseVColorLoc = glGetUniformLocation(render.program, "uUseVColor");
    GLint uModeLoc = glGetUniformLocation(render.program, "uMode");
    GLint uColorLoc = glGetUniformLocation(render.program, "uColor");

    glm::mat4 projection = glm::perspective(glm::radians(85.0f), (float)scrW / (float)scrH, 1.0f, 10000.0f);
    glm::vec3 eyePos = player.GetEyePos();
    glm::mat4 view = glm::lookAt(eyePos, eyePos + player.GetForward(), glm::vec3(0, 1, 0));
    glm::mat4 model = glm::mat4(1.0f);
    glm::mat4 mvp = projection * view * model;
    
    // 渲染地图
    glEnable(GL_CULL_FACE); glCullFace(GL_BACK);        
    glUniformMatrix4fv(glGetUniformLocation(render.program, "uMVP"), 1, GL_FALSE, glm::value_ptr(mvp));
    glUniformMatrix4fv(glGetUniformLocation(render.program, "uModel"), 1, GL_FALSE, glm::value_ptr(model));
    glUniform1i(uUseVColorLoc, 1);
    glUniform1i(uModeLoc, 1);
    glUniform3f(uColorLoc, 0.6f, 0.6f, 0.65f);
    glBindVertexArray(render.mapVAO);
    glDrawElements(GL_TRIANGLES, mapData.ni, GL_UNSIGNED_INT, 0);

    // 立刻关掉，之后所有 box/barrel/door/beam/enemy/bullet/gun 都不受影响
    glUniform1i(uUseVColorLoc, 0);

    glDisable(GL_CULL_FACE);
    
    // 渲染物理箱子
    glUniform1i(uModeLoc, 0);
    glUniform3f(uColorLoc, 0.6f, 0.4f, 0.2f);
    for (size_t i = 0; i < physboxIds.size(); i++) {
        JPH::RMat44 t = body_interface.GetWorldTransform(physboxIds[i]);
        JPH::Vec3 pos = t.GetTranslation(); JPH::Quat q = t.GetQuaternion();
        glm::vec3 halfSize = (mapData.physboxMaxs[i] - mapData.physboxMins[i]) * 0.5f;
        glm::mat4 box_model = glm::translate(glm::mat4(1.0f), glm::vec3(pos.GetX(), pos.GetY(), pos.GetZ())) 
                            * glm::mat4_cast(glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ())) 
                            * glm::scale(glm::mat4(1.0f), halfSize);
        glm::mat4 box_mvp = projection * view * box_model;
        glUniformMatrix4fv(glGetUniformLocation(render.program, "uMVP"), 1, GL_FALSE, glm::value_ptr(box_mvp));
        glUniformMatrix4fv(glGetUniformLocation(render.program, "uModel"), 1, GL_FALSE, glm::value_ptr(box_model));
        glBindVertexArray(render.boxVAO);
        glDrawElements(GL_TRIANGLES, render.boxIndexCount, GL_UNSIGNED_INT, 0);
    }

    // 渲染桶
    glUniform3f(uColorLoc, 0.8f, 0.15f, 0.1f);
    for (auto& b : barrels) {
        if (b.exploded) continue;
        JPH::RMat44 t = body_interface.GetWorldTransform(b.bodyId);
        JPH::Vec3 pos = t.GetTranslation(); JPH::Quat q = t.GetQuaternion();
        glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(pos.GetX(), pos.GetY(), pos.GetZ())) 
                    * glm::mat4_cast(glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ())) 
                    * glm::scale(glm::mat4(1.0f), glm::vec3(12.0f, 24.0f, 12.0f));
        glm::mat4 mvp = projection * view * m;
        glUniformMatrix4fv(glGetUniformLocation(render.program, "uMVP"), 1, GL_FALSE, glm::value_ptr(mvp));
        glUniformMatrix4fv(glGetUniformLocation(render.program, "uModel"), 1, GL_FALSE, glm::value_ptr(m));
        glBindVertexArray(render.boxVAO);
        glDrawElements(GL_TRIANGLES, render.boxIndexCount, GL_UNSIGNED_INT, 0);
    }

    // 渲染门
    glUniform3f(uColorLoc, 0.2f, 0.6f, 0.8f);
    for (auto& d : mapData.doors) {
        glm::vec3 center = (d.mins + d.maxs) * 0.5f + d.openDir * (d.openDist * d.openAmount);
        glm::vec3 halfSize = (d.maxs - d.mins) * 0.5f;
        glm::mat4 doorModel = glm::translate(glm::mat4(1.0f), center) * glm::scale(glm::mat4(1.0f), halfSize);
        glm::mat4 doorMvp = projection * view * doorModel;
        glUniformMatrix4fv(glGetUniformLocation(render.program, "uMVP"), 1, GL_FALSE, glm::value_ptr(doorMvp));
        glUniformMatrix4fv(glGetUniformLocation(render.program, "uModel"), 1, GL_FALSE, glm::value_ptr(doorModel));
        glBindVertexArray(render.boxVAO);
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
        glm::mat4 beamMvp = projection * view * beam;
        glUniform3f(uColorLoc, 0.3f, 0.8f, 1.0f);
        glUniformMatrix4fv(glGetUniformLocation(render.program, "uMVP"), 1, GL_FALSE, glm::value_ptr(beamMvp));
        glUniformMatrix4fv(glGetUniformLocation(render.program, "uModel"), 1, GL_FALSE, glm::value_ptr(beam));
        glBindVertexArray(render.boxVAO);
        glDrawElements(GL_TRIANGLES, render.boxIndexCount, GL_UNSIGNED_INT, 0);
    }

    // 敌人
    for (auto& enemy : enemies) {
        if (enemy.state == EnemyState::DEAD && enemy.deathTimer > 10.0f) continue;

        // 👈 根据状态变色
        if (enemy.state == EnemyState::DEAD) {
            glUniform3f(uColorLoc, 0.2f, 0.1f, 0.1f); 
        } else if (enemy.state == EnemyState::STUNNED) {
            glUniform3f(uColorLoc, 1.0f, 1.0f, 0.0f); // 被打晕时变黄
        } else if (enemy.state == EnemyState::ATTACK) {
            glUniform3f(uColorLoc, 1.0f, 0.0f, 0.0f); // 攻击时变红
        } else {
            glUniform3f(uColorLoc, 0.8f, 0.2f, 0.2f); // 正常追击
        }

        glm::mat4 enemyModel = glm::translate(glm::mat4(1.0f), enemy.pos);
        if (enemy.state == EnemyState::DEAD) {
            enemyModel = glm::rotate(enemyModel, glm::radians(90.0f), glm::vec3(1,0,0)) * glm::scale(glm::mat4(1.0f), glm::vec3(enemy.radius, enemy.radius, enemy.height * 0.5f));
        } else {
            enemyModel = enemyModel * glm::scale(glm::mat4(1.0f), glm::vec3(enemy.radius, enemy.height * 0.5f, enemy.radius));
        }
        glm::mat4 enemyMvp = projection * view * enemyModel;
        glUniformMatrix4fv(glGetUniformLocation(render.program, "uMVP"), 1, GL_FALSE, glm::value_ptr(enemyMvp));
        glUniformMatrix4fv(glGetUniformLocation(render.program, "uModel"), 1, GL_FALSE, glm::value_ptr(enemyModel));
        glBindVertexArray(render.boxVAO);
        glDrawElements(GL_TRIANGLES, render.boxIndexCount, GL_UNSIGNED_INT, 0);
    }

    // 弹孔
    glUniform3f(uColorLoc, 0.1f, 0.1f, 0.1f);
    for (auto& bh : bulletHoles) {
        glm::mat4 bulletModel = glm::translate(glm::mat4(1.0f), bh.pos) * glm::scale(glm::mat4(1.0f), glm::vec3(0.5f));
        glm::mat4 bulletMvp = projection * view * bulletModel;
        glUniformMatrix4fv(glGetUniformLocation(render.program, "uMVP"), 1, GL_FALSE, glm::value_ptr(bulletMvp));
        glUniformMatrix4fv(glGetUniformLocation(render.program, "uModel"), 1, GL_FALSE, glm::value_ptr(bulletModel));
        glBindVertexArray(render.boxVAO);
        glDrawElements(GL_TRIANGLES, render.boxIndexCount, GL_UNSIGNED_INT, 0);
    }

    // 武器（2D 视角）
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

    glUniform1i(uModeLoc, 2);
    auto DrawGunPart = [&](glm::vec3 offset, glm::vec3 scale) {
        glm::mat4 partModel = glm::translate(gunBase, offset) * glm::scale(glm::mat4(1.0f), scale);
        glm::mat4 partMvp = gunProj * glm::mat4(1.0f) * partModel;
        glUniformMatrix4fv(glGetUniformLocation(render.program, "uMVP"), 1, GL_FALSE, glm::value_ptr(partMvp));
        glUniformMatrix4fv(glGetUniformLocation(render.program, "uModel"), 1, GL_FALSE, glm::value_ptr(partModel));
        glBindVertexArray(render.boxVAO);
        glDrawElements(GL_TRIANGLES, render.boxIndexCount, GL_UNSIGNED_INT, 0);
    };

    glm::mat4 barrelModel = glm::translate(gunBase, glm::vec3(0.0f, 0.02f, -0.3f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.008f, 0.008f, 0.4f));
    glm::mat4 barrelMvp = gunProj * barrelModel;
    glUniformMatrix4fv(glGetUniformLocation(render.program, "uMVP"), 1, GL_FALSE, glm::value_ptr(barrelMvp));
    glUniformMatrix4fv(glGetUniformLocation(render.program, "uModel"), 1, GL_FALSE, glm::value_ptr(barrelModel));
    glBindVertexArray(render.gunVAO);
    glDrawElements(GL_TRIANGLES, render.gunIndexCount, GL_UNSIGNED_INT, 0);

    glBindVertexArray(render.boxVAO);
    DrawGunPart(glm::vec3(0.0f, 0.0f, -0.05f), glm::vec3(0.035f, 0.05f, 0.25f));
    DrawGunPart(glm::vec3(0.0f, -0.08f, 0.02f), glm::vec3(0.02f, 0.08f, 0.04f));
    DrawGunPart(glm::vec3(0.0f, -0.06f, 0.1f), glm::vec3(0.025f, 0.06f, 0.03f));
    DrawGunPart(glm::vec3(0.0f, 0.05f, -0.3f), glm::vec3(0.005f, 0.015f, 0.02f));
    
    if (weapon.recoilTimer > 0.05f) { 
        glUniform1i(uModeLoc, 0); glUniform3f(uColorLoc, 1.0f, 0.8f, 0.2f); 
        DrawGunPart(glm::vec3(0.0f, 0.02f, -0.6f), glm::vec3(0.04f, 0.04f, 0.04f));
    }
}