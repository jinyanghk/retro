#version 330 core
layout(location = 0) in vec3 aPos;
out vec3 vTexCoords;
uniform mat4 uProjection;
uniform mat4 uView;

void main() {
    // 天空盒跟随摄像机，去掉平移部分
    mat4 viewNoTranslation = mat4(mat3(uView));
    vec4 pos = uProjection * viewNoTranslation * vec4(aPos, 1.0);
    gl_Position = pos.xyww; // 👈 把深度（Z）强制设为 1.0（最远处），防止遮挡地形
    vTexCoords = aPos;
}