#version 430 core

in layout(location = 0) vec3 position;
in layout(location = 1) vec3 normal;
in layout(location = 2) vec2 UV;
in layout(location = 3) vec4 color;
in layout(location = 4) vec3 tangent;
in layout(location = 5) vec3 bitangent;

layout(binding = 2) uniform sampler2D displacementTexture;
uniform float displacementCoefficient;

uniform mat4 MVP;
uniform mat4 MV;
uniform mat4 MVnormal;

uniform float shininess;
uniform vec2 uvOffset;

uniform bool isIlluminated;
uniform bool isTextured;
uniform bool isNormalMapped;
uniform bool isDisplacementMapped;
uniform bool isInverted;

out layout(location = 0) vec3 vertex_out;
out layout(location = 1) vec3 normal_out;
out layout(location = 2) vec2 uv_out;
out layout(location = 3) vec4 color_out;
out layout(location = 4) vec3 tangent_out;
out layout(location = 5) vec3 bitangent_out;

void main() {
    vec3 displacement = vec3(0.0);
    if (isDisplacementMapped){
        float o = texture(displacementTexture, UV + uvOffset).r * 2.0 - 1.0;
        float u = (texture(displacementTexture, UV + uvOffset + vec2(0.001, 0.0)).r*2.0-1.0 - o) / 0.004;
        float v = (texture(displacementTexture, UV + uvOffset + vec2(0.0, 0.001)).r*2.0-1.0 - o) / 0.004;
        
        displacement = normal * displacementCoefficient * o;
    }
    
    normal_out = normalize(vec3(MVnormal * vec4(normal, 1.0f)));
    vertex_out = vec3(MV * vec4(position+displacement, 1.0f));
    uv_out = UV + uvOffset;
    gl_Position =  MVP * vec4(position+displacement, 1.0f);
    
    color_out = color;

    tangent_out = normalize(vec3(MVnormal * vec4(tangent, 1.0f)));
    bitangent_out = normalize(vec3(MVnormal * vec4(bitangent, 1.0f)));
}
