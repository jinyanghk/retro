#pragma once
#include <glm/glm.hpp>
#include "imgui.h"

void DrawAndManipulateGizmo(const glm::mat4& view, const glm::mat4& proj,
                            ImVec2 viewportPos, ImVec2 viewportSize);