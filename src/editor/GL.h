#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <utility>
#include "Globals.h"

struct MapData;

void InitGL();
void ResizeFBO(int w, int h);
unsigned int GetFBOTexture();
void BindFBO();

void DrawGrid(const glm::mat4& viewProj);
void DrawBrushes(const glm::mat4& viewProj);
void DrawBrushWireframe(const glm::mat4& viewProj);
void DrawEntityBBoxes(const glm::mat4& viewProj);
void DrawColorLines(const std::vector<float>& buf, const glm::mat4& viewProj);
void DrawFaceHighlight(const glm::vec3* quad, const glm::mat4& viewProj);
void DrawVertexPoints(const std::vector<std::pair<glm::vec3, glm::vec3>>& pts, const glm::mat4& viewProj);
void DrawRawPrimitives(const std::vector<float>& buf, unsigned int mode, float pointSize, float lineWidth, const glm::mat4& viewProj);

// 供 main.cpp 加载 BVH 使用
void UploadBvhToGL(const MapData& data);
void DrawLoadedBvh(const glm::mat4& viewProj);
void CleanupGL();