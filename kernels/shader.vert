#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 vertColor;

layout(set = 0, binding = 0) uniform MVP {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

void main(){
    //gl_Position = vec4(inPos, 1.0);
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPos, 1.0);
    vertColor = inColor;
}
