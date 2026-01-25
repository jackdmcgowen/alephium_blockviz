#version 450
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragPos;
layout(location = 2) in vec3 fragNormal;

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 lightPos;
    float pad1;
    vec3 viewPos;
    float pad2;
    float meters;
} ubo;

layout(location = 0) out vec4 outColor;

void main() {
// ───────────────────────────────────────────────
    //  Material / Lighting parameters
    // ───────────────────────────────────────────────
    vec3  lightColor       = vec3(1.00, 0.98, 0.92);  // slightly warm white
    float ambientStrength  = 0.15;
    float diffuseStrength  = 0.85;
    float specularStrength = 0.45;
    float shininess        = 16.0;                     // higher = smaller, sharper highlight

    // Normalize inputs (usually already normalized, but safe)
    vec3 N = normalize(fragNormal);
    vec3 L = normalize(ubo.lightPos - fragPos);        // light direction
    vec3 V = normalize(ubo.viewPos  - fragPos);        // view direction
    vec3 R = reflect(-L, N);                           // reflection direction

    // ───────────────────────────────────────────────
    // Phong components
    // ───────────────────────────────────────────────

    // 1. Ambient
    vec3 ambient = ambientStrength * lightColor;

    // 2. Diffuse
    float NdotL   = max(dot(N, L), 0.0);
    vec3  diffuse = diffuseStrength * NdotL * lightColor;

    // Blinn-Phong specular
    vec3 H = normalize(L + V);                // halfway vector
    float NdotH = max(dot(N, H), 0.0);
    float spec  = pow(NdotH, shininess * 4.0); // shininess usually needs higher exponent
    vec3 specular = specularStrength * spec * lightColor;
    
    //float distance    = length(ubo.lightPos - fragPos);
    //float attenuation = 1.0 / (1.0 + 0.09 * distance + 0.032 * distance * distance);
    //diffuse  *= attenuation;
    //specular *= attenuation;

    // ───────────────────────────────────────────────
    // Final color
    // ───────────────────────────────────────────────
    vec3 lighting = ambient + diffuse + specular;

    // Apply material/instance color
    vec3 finalColor = lighting * fragColor;
    
    // Rim / fresnel effect (optional – gives nice edge glow)
    //float rim = 1.0 - max(dot(N, V), 0.0);
    //rim = pow(rim, 3.0);                      // sharper rim
    //vec3 rimLight = vec3(0.4, 0.6, 1.0) * rim * 0.6;
    //finalColor += rimLight;

    // Optional: slight gamma-like correction or tone mapping
    //finalColor = pow(finalColor, vec3(1.0/2.2));   // approx gamma 2.2 → sRGB

    outColor = vec4(finalColor, 1.0);
}