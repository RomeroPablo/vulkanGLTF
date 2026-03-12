#pragma once
#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION
#include "external/tiny_gltf.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "lib.hpp"

struct GltfModel{
struct PrimitiveRange {
    uint32_t firstVertex{0};
    uint32_t vertexCount{0};
    int materialIndex{-1};
};

static glm::mat4 nodeLocalMatrix(const tinygltf::Node& node) {
    glm::mat4 matrix = glm::mat4(1.0f);
    if (node.matrix.size() == 16) {
        matrix = glm::make_mat4(node.matrix.data());
    } else {
        glm::vec3 translation(0.0f);
        if (node.translation.size() == 3) {
            translation = glm::vec3(
                static_cast<float>(node.translation[0]),
                static_cast<float>(node.translation[1]),
                static_cast<float>(node.translation[2]));
        }

        glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
        if (node.rotation.size() == 4) {
            rotation = glm::quat(
                static_cast<float>(node.rotation[3]),
                static_cast<float>(node.rotation[0]),
                static_cast<float>(node.rotation[1]),
                static_cast<float>(node.rotation[2]));
        }

        glm::vec3 scale(1.0f);
        if (node.scale.size() == 3) {
            scale = glm::vec3(
                static_cast<float>(node.scale[0]),
                static_cast<float>(node.scale[1]),
                static_cast<float>(node.scale[2]));
        }

        matrix = glm::translate(glm::mat4(1.0f), translation) *
                 glm::mat4_cast(rotation) *
                 glm::scale(glm::mat4(1.0f), scale);
    }
    return matrix;
}

static void appendPrimitiveVertices(const tinygltf::Model& model,
                                    const tinygltf::Primitive& primitive,
                                    const glm::mat4& worldMatrix,
                                    std::vector<Vertex>& outVertices,
                                    std::vector<PrimitiveRange>& outRanges) {
    auto posIt = primitive.attributes.find("POSITION");
    if (posIt == primitive.attributes.end()) {
        return;
    }

    const tinygltf::Accessor& posAccessor = model.accessors[posIt->second];
    const tinygltf::BufferView& posView = model.bufferViews[posAccessor.bufferView];
    const tinygltf::Buffer& posBuffer = model.buffers[posView.buffer];
    const uint8_t* posData = posBuffer.data.data() + posView.byteOffset + posAccessor.byteOffset;
    const size_t posStride = posAccessor.ByteStride(posView) ? posAccessor.ByteStride(posView) : sizeof(float) * 3;

    bool hasNormals = false;
    const uint8_t* normalData = nullptr;
    size_t normalStride = 0;
    auto normalIt = primitive.attributes.find("NORMAL");
    if (normalIt != primitive.attributes.end()) {
        const tinygltf::Accessor& normalAccessor = model.accessors[normalIt->second];
        const tinygltf::BufferView& normalView = model.bufferViews[normalAccessor.bufferView];
        const tinygltf::Buffer& normalBuffer = model.buffers[normalView.buffer];
        normalData = normalBuffer.data.data() + normalView.byteOffset + normalAccessor.byteOffset;
        normalStride = normalAccessor.ByteStride(normalView) ? normalAccessor.ByteStride(normalView) : sizeof(float) * 3;
        hasNormals = true;
    }

    bool hasUV0 = false;
    const uint8_t* uvData = nullptr;
    size_t uvStride = 0;
    auto uvIt = primitive.attributes.find("TEXCOORD_0");
    if (uvIt != primitive.attributes.end()) {
        const tinygltf::Accessor& uvAccessor = model.accessors[uvIt->second];
        const tinygltf::BufferView& uvView = model.bufferViews[uvAccessor.bufferView];
        const tinygltf::Buffer& uvBuffer = model.buffers[uvView.buffer];
        uvData = uvBuffer.data.data() + uvView.byteOffset + uvAccessor.byteOffset;
        uvStride = uvAccessor.ByteStride(uvView) ? uvAccessor.ByteStride(uvView) : sizeof(float) * 2;
        hasUV0 = true;
    }

    bool hasColor = false;
    const uint8_t* colorData = nullptr;
    size_t colorStride = 0;
    int colorType = TINYGLTF_TYPE_VEC3;
    int colorComponentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    bool colorNormalized = false;
    auto colorIt = primitive.attributes.find("COLOR_0");
    if (colorIt != primitive.attributes.end()) {
        const tinygltf::Accessor& colorAccessor = model.accessors[colorIt->second];
        const tinygltf::BufferView& colorView = model.bufferViews[colorAccessor.bufferView];
        const tinygltf::Buffer& colorBuffer = model.buffers[colorView.buffer];
        colorData = colorBuffer.data.data() + colorView.byteOffset + colorAccessor.byteOffset;
        colorStride = colorAccessor.ByteStride(colorView) ?
            colorAccessor.ByteStride(colorView) :
            (tinygltf::GetNumComponentsInType(colorAccessor.type) *
             tinygltf::GetComponentSizeInBytes(colorAccessor.componentType));
        colorType = colorAccessor.type;
        colorComponentType = colorAccessor.componentType;
        colorNormalized = colorAccessor.normalized;
        hasColor = true;
    }

    auto readColor = [&](uint32_t vertexIndex) -> glm::vec3 {
        if (!hasColor) {
            return glm::vec3(1.0f);
        }
        const uint8_t* p = colorData + vertexIndex * colorStride;
        const int componentCount = tinygltf::GetNumComponentsInType(colorType);
        glm::vec3 c(1.0f);
        switch (colorComponentType) {
            case TINYGLTF_COMPONENT_TYPE_FLOAT: {
                const float* v = reinterpret_cast<const float*>(p);
                c.r = v[0];
                c.g = v[1];
                c.b = v[2];
                break;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
                const uint8_t* v = reinterpret_cast<const uint8_t*>(p);
                if (colorNormalized) {
                    c.r = v[0] / 255.0f;
                    c.g = v[1] / 255.0f;
                    c.b = v[2] / 255.0f;
                } else {
                    c.r = static_cast<float>(v[0]);
                    c.g = static_cast<float>(v[1]);
                    c.b = static_cast<float>(v[2]);
                }
                break;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
                const uint16_t* v = reinterpret_cast<const uint16_t*>(p);
                if (colorNormalized) {
                    c.r = v[0] / 65535.0f;
                    c.g = v[1] / 65535.0f;
                    c.b = v[2] / 65535.0f;
                } else {
                    c.r = static_cast<float>(v[0]);
                    c.g = static_cast<float>(v[1]);
                    c.b = static_cast<float>(v[2]);
                }
                break;
            }
            default:
                break;
        }
        if (componentCount < 3) {
            return glm::vec3(1.0f);
        }
        return c;
    };

    const uint32_t firstVertex = static_cast<uint32_t>(outVertices.size());

    auto emitVertex = [&](uint32_t vertexIndex) {
        const float* p = reinterpret_cast<const float*>(posData + vertexIndex * posStride);
        glm::vec4 worldPos = worldMatrix * glm::vec4(p[0], p[1], p[2], 1.0f);
        glm::vec3 color = readColor(vertexIndex);
        glm::vec3 normal(0.0f, 0.0f, 1.0f);
        if (hasNormals) {
            const float* n = reinterpret_cast<const float*>(normalData + vertexIndex * normalStride);
            normal = glm::normalize(glm::mat3(worldMatrix) * glm::vec3(n[0], n[1], n[2]));
        }
        glm::vec2 uv0(0.0f);
        if (hasUV0) {
            const float* uv = reinterpret_cast<const float*>(uvData + vertexIndex * uvStride);
            uv0 = glm::vec2(uv[0], uv[1]);
        }

        Vertex out{};
        out.pos[0] = worldPos.x;
        out.pos[1] = worldPos.y;
        out.pos[2] = worldPos.z;
        out.color[0] = color.r;
        out.color[1] = color.g;
        out.color[2] = color.b;
        out.uv[0] = uv0.x;
        out.uv[1] = uv0.y;
        out.normal[0] = normal.x;
        out.normal[1] = normal.y;
        out.normal[2] = normal.z;
        outVertices.push_back(out);
    };

    if (primitive.indices < 0) {
        for (uint32_t i = 0; i < posAccessor.count; i++) {
            emitVertex(i);
        }
        const uint32_t vertexCount = static_cast<uint32_t>(outVertices.size()) - firstVertex;
        if (vertexCount > 0) {
            outRanges.push_back({firstVertex, vertexCount, primitive.material});
        }
        return;
    }

    const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
    const tinygltf::BufferView& indexView = model.bufferViews[indexAccessor.bufferView];
    const tinygltf::Buffer& indexBuffer = model.buffers[indexView.buffer];
    const uint8_t* indexData = indexBuffer.data.data() + indexView.byteOffset + indexAccessor.byteOffset;
    const size_t indexStride = indexAccessor.ByteStride(indexView) ?
        indexAccessor.ByteStride(indexView) :
        tinygltf::GetComponentSizeInBytes(indexAccessor.componentType);

    for (uint32_t i = 0; i < indexAccessor.count; i++) {
        const uint8_t* p = indexData + i * indexStride;
        uint32_t index = 0;
        switch (indexAccessor.componentType) {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                index = *reinterpret_cast<const uint8_t*>(p);
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                index = *reinterpret_cast<const uint16_t*>(p);
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                index = *reinterpret_cast<const uint32_t*>(p);
                break;
            default:
                continue;
        }
        emitVertex(index);
    }

    const uint32_t vertexCount = static_cast<uint32_t>(outVertices.size()) - firstVertex;
    if (vertexCount > 0) {
        outRanges.push_back({firstVertex, vertexCount, primitive.material});
    }
}

static void appendNodeMesh(const tinygltf::Model& model,
                           int nodeIndex,
                           const glm::mat4& parentMatrix,
                           std::vector<Vertex>& outVertices,
                           std::vector<PrimitiveRange>& outRanges) {
    const tinygltf::Node& node = model.nodes[nodeIndex];
    glm::mat4 world = parentMatrix * nodeLocalMatrix(node);
    if (node.mesh >= 0) {
        const tinygltf::Mesh& mesh = model.meshes[node.mesh];
        for (const tinygltf::Primitive& primitive : mesh.primitives) {
            appendPrimitiveVertices(model, primitive, world, outVertices, outRanges);
        }
    }
    for (int child : node.children) {
        appendNodeMesh(model, child, world, outVertices, outRanges);
    }
}
};
