#ifndef MATH_COMPAT_H
#define MATH_COMPAT_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>

// retro 是 Z-up。QAngle = (pitch, yaw, roll)，单位度。
// Source 约定：pitch 绕 Y，yaw 绕 Z，roll 绕 X。
// 这里用 glm 组合出 4x4。
inline glm::mat4 AngleMatrix(const glm::vec3& anglesDeg)
{
    float p = glm::radians(anglesDeg.x);
    float y = glm::radians(anglesDeg.y);
    float r = glm::radians(anglesDeg.z);
    glm::mat4 m(1.0f);
    m = glm::rotate(m, y, glm::vec3(0, 0, 1)); // yaw   Z
    m = glm::rotate(m, p, glm::vec3(0, 1, 0)); // pitch Y
    m = glm::rotate(m, r, glm::vec3(1, 0, 0)); // roll  X
    return m;
}

// 从矩阵反解 angles（度），用于 property panel 显示
inline glm::vec3 MatrixAngles(const glm::mat4& m)
{
    // 提取 yaw / pitch / roll（ZYX 顺序）
    float yaw = std::atan2(m[0][1], m[0][0]);
    float pitch = std::asin(-m[0][2]);
    float roll = std::atan2(m[1][2], m[2][2]);
    return glm::vec3(glm::degrees(pitch), glm::degrees(yaw), glm::degrees(roll));
}

// 绕任意中心旋转一个点
inline glm::vec3 RotatePointAround(const glm::vec3& p,
                                   const glm::vec3& center,
                                   const glm::mat4& R)
{
    glm::vec3 local = p - center;
    glm::vec3 rotated = glm::vec3(R * glm::vec4(local, 0.0f));
    return center + rotated;
}

// Z-up <-> Y-up 交换（给 ImGuizmo 用）
inline glm::mat4 ZUpToYUp(const glm::mat4& m)
{
    // swap Y and Z 行/列
    glm::mat4 r = m;
    for (int i = 0; i < 4; i++) std::swap(r[i][1], r[i][2]);
    for (int i = 0; i < 4; i++) std::swap(r[1][i], r[2][i]);
    return r;
}
inline glm::vec3 ZUpToYUp(const glm::vec3& v) { return glm::vec3(v.x, v.z, v.y); }
inline glm::vec3 YUpToZUp(const glm::vec3& v) { return glm::vec3(v.x, v.z, v.y); }

// 相机基向量 → view 矩阵（Z-up）
inline glm::mat4 LookAtZUp(const glm::vec3& eye,
                           const glm::vec3& target,
                           const glm::vec3& up = glm::vec3(0, 0, 1))
{
    return glm::lookAt(eye, target, up);
}

// 屏幕坐标 → NDC
inline void ScreenToNDC(float mx, float my, int w, int h, float& nx, float& ny)
{
    nx = (mx / (float)w) * 2.0f - 1.0f;
    ny = 1.0f - (my / (float)h) * 2.0f;
}

// 世界坐标 → 屏幕坐标。返回 false 表示在相机背后。
inline bool WorldToScreen(const glm::vec3& world,
                          const glm::mat4& view,
                          const glm::mat4& proj,
                          int w, int h,
                          float& sx, float& sy)
{
    glm::vec4 clip = proj * view * glm::vec4(world, 1.0f);
    if (clip.w <= 0.0001f) return false;
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    sx = (ndc.x * 0.5f + 0.5f) * (float)w;
    sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * (float)h;
    return true;
}

#endif