#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec3 aColor;
layout(location=3) in vec2 aUV;
uniform mat4 uMVP;
uniform mat4 uModel;
out vec3 vNormal;
out vec3 vWorldPos;
out vec3 vColor;
out vec2 vUV;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vNormal   = mat3(uModel) * aNormal;
    vWorldPos = vec3(uModel * vec4(aPos, 1.0));
    vColor    = aColor;
    vUV       = aUV;
}