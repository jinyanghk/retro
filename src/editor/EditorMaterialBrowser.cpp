#include "EditorPanels.h"
#include "SceneGlobals.h"
#include "UndoStack.h"
#include <GL/glew.h>
#include <imgui.h>
#include <algorithm>

extern bool g_bShowMaterialBrowser;
extern std::string* g_pTargetMaterial;
extern char g_matBrowserFilter[128];
extern GLuint GetMaterialThumb(const std::string& matName);

void DrawMaterialBrowserWindow() {
    if (!g_bShowMaterialBrowser) return;

    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Material Browser", &g_bShowMaterialBrowser)) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##MatFilter", "Filter materials...", g_matBrowserFilter, sizeof(g_matBrowserFilter));
        ImGui::Separator();

        std::string filterLower = g_matBrowserFilter;
        std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);

        float thumbSize = 128.0f, padding = 8.0f, cellSize = thumbSize + padding * 2;
        int columns = std::max(1, (int)(ImGui::GetContentRegionAvail().x / cellSize));

        if (ImGui::BeginChild("##MatGridScroll", ImVec2(0, 0), true)) {
            std::vector<const std::string*> filtered;
            for (auto& m : g_allMaterials) {
                std::string lower = m;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (filterLower.empty() || lower.find(filterLower) != std::string::npos) filtered.push_back(&m);
            }

            int rows = ((int)filtered.size() + columns - 1) / columns;
            for (int row = 0; row < rows; row++) {
                for (int col = 0; col < columns; col++) {
                    int idx = row * columns + col;
                    if (idx >= (int)filtered.size()) break;
                    const std::string& mat = *filtered[idx];
                    GLuint texId = GetMaterialThumb(mat);
                    
                    ImGui::PushID(idx);
                    ImGui::BeginGroup();
                    if (ImGui::ImageButton(mat.c_str(), (ImTextureID)(intptr_t)texId, ImVec2(thumbSize, thumbSize))) {
                        if (g_pTargetMaterial) {
                            UndoStack::Instance().PushSnapshot("Material Change");
                            *g_pTargetMaterial = mat;
                            // 这里回写逻辑（如果你在旧文件里写过了）
                        }
                        g_bShowMaterialBrowser = false;
                    }
                    ImGui::TextWrapped("%s", mat.c_str());
                    ImGui::EndGroup();
                    if (col < columns - 1 && idx + 1 < (int)filtered.size()) ImGui::SameLine();
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
        }
    }
    ImGui::End();
}