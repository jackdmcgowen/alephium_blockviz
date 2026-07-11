#version 450
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragPos;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in float fragAlpha;

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 lightPos;
    float pad1;
    vec3 viewPos;
    float pad2;
    float anim_scale;
    float anim_alpha;
    float anim_time;
    float pad3;
} ubo;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(ubo.viewPos - fragPos);

    vec3 keyColor  = vec3(1.00, 0.97, 0.92);
    vec3 fillColor = vec3(0.55, 0.62, 0.78);

    vec3 L_key = normalize(ubo.lightPos - fragPos);
    vec3 L_fill = normalize(vec3(-0.45, 0.55, -0.25));

    float wrap = 0.15;
    float NdotK = max((dot(N, L_key) + wrap) / (1.0 + wrap), 0.0);
    float NdotF = max(dot(N, L_fill), 0.0);

    vec3 ambient = 0.32 * vec3(0.92, 0.94, 1.00);
    vec3 diffuse = 0.72 * NdotK * keyColor + 0.22 * NdotF * fillColor;

    vec3 H = normalize(L_key + V);
    float NdotH = max(dot(N, H), 0.0);
    float spec = pow(NdotH, 48.0);
    float albedoLum = dot(fragColor, vec3(0.299, 0.587, 0.114));
    vec3 specular = (0.28 * (1.0 - 0.55 * albedoLum)) * spec * keyColor;

    float rim = pow(1.0 - max(dot(N, V), 0.0), 2.5);
    vec3 rimLight = rim * 0.12 * fillColor;

    vec3 lit = ambient + diffuse + specular + rimLight;
    vec3 finalColor = lit * fragColor;
    finalColor = finalColor / (finalColor + vec3(0.35)) * 1.15;
    finalColor = clamp(finalColor, 0.0, 1.0);

    outColor = vec4(finalColor, clamp(fragAlpha, 0.0, 1.0));
}
