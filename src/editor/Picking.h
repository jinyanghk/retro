#pragma once
#include <glm/glm.hpp>
#include "Brush.h"

bool ScreenPointInAABBWorld(const glm::vec3& worldMin, const glm::vec3& worldMax,
                            const glm::mat4& view, const glm::mat4& proj,
                            int w, int h, int mx, int my, float padding = 10.0f);

int PickBrushRay(const glm::vec3& rayOrigin, const glm::vec3& rayDir);
int PickEntityRay(const glm::vec3& rayOrigin, const glm::vec3& rayDir);
bool ScreenToWorldRay(const glm::mat4& view, const glm::mat4& proj,
                      int w, int h, int mx, int my,
                      glm::vec3& outOrigin, glm::vec3& outDir);

int PickVertex(const CBrush& b, const glm::mat4& view, const glm::mat4& proj, int w, int h, int mx, int my, float radiusPx = 15.0f);
int PickEdge(const CBrush& b, const glm::mat4& view, const glm::mat4& proj, int w, int h, int mx, int my, float radiusPx = 15.0f);
int PickFace(const CBrush& b, const glm::mat4& view, const glm::mat4& proj, int w, int h, int mx, int my);