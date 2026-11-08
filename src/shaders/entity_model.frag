#version 330 core
in vec2 vUV;
in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vTangent;
in vec3 vBitangent;

out vec4 FragColor;

uniform sampler2D uTex;
uniform sampler2D uNormalMap;
uniform vec3 uSunDir;      // 指向"光源来的方向"的反向（即光的方向）
uniform vec3 uSunColor;
uniform vec3 uAmbient;
uniform vec3 uCameraPos;

void main() {
    vec3 albedo = texture(uTex, vUV).rgb;

    // 法线贴图 → TBN
    vec3 normalTex = texture(uNormalMap, vUV).xyz;
    vec3 tangentNormal = normalTex * 2.0 - 1.0;

    vec3 N = normalize(vNormal);
    vec3 T = normalize(vTangent);
    vec3 B = normalize(vBitangent);
    mat3 TBN = mat3(T, B, N);
    vec3 worldNormal = normalize(TBN * tangentNormal);

    // 方向光（Lambert）
    vec3 L = normalize(-uSunDir);
    float NdotL = max(dot(worldNormal, L), 0.0);

    // 高光（Blinn-Phong）
    vec3 V = normalize(uCameraPos - vWorldPos);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(worldNormal, H), 0.0), 32.0) * 0.2;

    // 环境光 + 方向光 + 高光
    vec3 color = albedo * (uAmbient + uSunColor * NdotL) + vec3(spec);
    FragColor = vec4(color, 1.0);
}