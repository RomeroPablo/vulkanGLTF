#pragma once
#include <cstdio>
#include <sstream>
#include <string>
#include <iostream>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#define VK_CHECK(x) do { VkResult err = x; if (err) { \
    std::cout << "Detected Vulkan error: " << err << " at " \
    << __FILE__ << ":" << __LINE__ << std::endl; \
    abort(); \
} } while(0)

inline unsigned char* readFile(const char* path, size_t* size){
    FILE* file = fopen(path, "rb");
    if(fseek(file, 0, SEEK_END) != 0){ fclose(file); return NULL; }

    long fileSize = ftell(file);
    if(fileSize < 0) {fclose(file); return NULL;}

    rewind(file);

    unsigned char* buffer = (unsigned char*)malloc(fileSize);

    size_t bytesRead = fread(buffer, 1, fileSize, file);
    if(bytesRead != (size_t)fileSize){ free(buffer); fclose(file); return NULL; }

    fclose(file);
    if(size) *size= bytesRead;
    return buffer;
};

std::string inline toHexString( uint32_t v ) { 
    std::stringstream s; s << std::hex << v; return s.str();}

std::string inline ver_string(const uint32_t v){
    return std::to_string(VK_VERSION_MAJOR(v)) + "." 
         + std::to_string(VK_VERSION_MINOR(v)) + "."
         + std::to_string(VK_VERSION_PATCH(v));
}

std::string inline vid_string(uint32_t v){
    switch(v){
      case VK_VENDOR_ID_KHRONOS     : return "Khronos";
      case VK_VENDOR_ID_VIV         : return "VIV";
      case VK_VENDOR_ID_VSI         : return "VSI";
      case VK_VENDOR_ID_KAZAN       : return "Kazan";
      case VK_VENDOR_ID_CODEPLAY    : return "Codeplay";
      case VK_VENDOR_ID_MESA        : return "MESA";
      case VK_VENDOR_ID_POCL        : return "Pocl";
      case VK_VENDOR_ID_MOBILEYE    : return "Mobileye";
      case 0x1002                   : return "AMD";
      case 0x1010                   : return "ImgTec";
      case 0x10DE                   : return "NVIDIA";
      case 0x13B5                   : return "ARM";
      case 0x5143                   : return "Qualcomm";
      case 0x8086                   : return "INTEL";
      default : return "invalid ( 0x" + toHexString( static_cast<uint32_t>( v ) ) + " )";
    }
}

std::string inline deviceType_string(VkPhysicalDeviceType v){
    switch(v){
      case VK_PHYSICAL_DEVICE_TYPE_OTHER            : return "Other";
      case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU   : return "IntegratedGpu";
      case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     : return "DiscreteGpu";
      case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU      : return "VirtualGpu";
      case VK_PHYSICAL_DEVICE_TYPE_CPU              : return "Cpu";
      default : return "invalid ( 0x" + toHexString( static_cast<uint32_t>( v ) ) + " )";
    }
};

void inline printDeviceProps(const VkPhysicalDeviceProperties& p){
    std::cout << "\t"   << p.deviceName << std::endl;
    std::cout << "\t\tAPI Version    : " << ver_string(p.apiVersion) << std::endl;
    std::cout << "\t\tDriver Version : " << ver_string(p.driverVersion) << std::endl;
    std::cout << "\t\tDevice Info    : " << vid_string(p.vendorID) << " - " 
        << "0x" << toHexString(p.deviceID) << " - " 
        << deviceType_string(p.deviceType) << std::endl;
}

void inline printBreak(const size_t s){
    std::string sr;
    for(size_t i{}; i < s; i++) sr.push_back('-');
    std::cout << sr << std::endl;
}

void inline printSparseProps(const VkPhysicalDeviceSparseProperties& p){
    std::cout << "\t\t<><> Physical Device Sparse Properties <><>" << std::endl;

    auto print_bool = [](const char* name, VkBool32 v){
        std::cout << "\t\t" << name << " : " << (v ? "true" : "false") << std::endl;
    };

    print_bool("residencyStandard2DBlockShape", p.residencyStandard2DBlockShape);
    print_bool("residencyStandard2DMultisampleBlockShape", p.residencyStandard2DMultisampleBlockShape);
    print_bool("residencyStandard3DBlockShape", p.residencyStandard3DBlockShape);
    print_bool("residencyAlignedMipSize", p.residencyAlignedMipSize);
    print_bool("residencyNonResidentStrict", p.residencyNonResidentStrict);
}

void inline printPhysicalLimits(const VkPhysicalDeviceLimits& p){
    std::cout << "\t\t<><> Physical Device Limits <><>" << std::endl;

    auto print_u32 = [](const char* name, uint32_t v){
        std::cout << "\t\t" << name << " : " << v << std::endl;
    };
    auto print_i32 = [](const char* name, int32_t v){
        std::cout << "\t\t" << name << " : " << v << std::endl;
    };
    auto print_u64 = [](const char* name, VkDeviceSize v){
        std::cout << "\t\t" << name << " : " << v << std::endl;
    };
    auto print_size = [](const char* name, size_t v){
        std::cout << "\t\t" << name << " : " << v << std::endl;
    };
    auto print_bool = [](const char* name, VkBool32 v){
        std::cout << "\t\t" << name << " : " << (v ? "true" : "false") << std::endl;
    };
    auto print_f32 = [](const char* name, float v){
        std::cout << "\t\t" << name << " : " << v << std::endl;
    };
    auto print_u32_2 = [](const char* name, const uint32_t v[2]){
        std::cout << "\t\t" << name << " : [" << v[0] << ", " << v[1] << "]" << std::endl;
    };
    auto print_u32_3 = [](const char* name, const uint32_t v[3]){
        std::cout << "\t\t" << name << " : [" << v[0] << ", " << v[1] << ", " << v[2] << "]" << std::endl;
    };
    auto print_f32_2 = [](const char* name, const float v[2]){
        std::cout << "\t\t" << name << " : [" << v[0] << ", " << v[1] << "]" << std::endl;
    };

    print_u32("maxImageDimension1D", p.maxImageDimension1D);
    print_u32("maxImageDimension2D", p.maxImageDimension2D);
    print_u32("maxImageDimension3D", p.maxImageDimension3D);
    print_u32("maxImageDimensionCube", p.maxImageDimensionCube);
    print_u32("maxImageArrayLayers", p.maxImageArrayLayers);
    print_u32("maxTexelBufferElements", p.maxTexelBufferElements);
    print_u32("maxUniformBufferRange", p.maxUniformBufferRange);
    print_u32("maxStorageBufferRange", p.maxStorageBufferRange);
    print_u32("maxPushConstantsSize", p.maxPushConstantsSize);
    print_u32("maxMemoryAllocationCount", p.maxMemoryAllocationCount);
    print_u32("maxSamplerAllocationCount", p.maxSamplerAllocationCount);
    print_u64("bufferImageGranularity", p.bufferImageGranularity);
    print_u64("sparseAddressSpaceSize", p.sparseAddressSpaceSize);
    print_u32("maxBoundDescriptorSets", p.maxBoundDescriptorSets);
    print_u32("maxPerStageDescriptorSamplers", p.maxPerStageDescriptorSamplers);
    print_u32("maxPerStageDescriptorUniformBuffers", p.maxPerStageDescriptorUniformBuffers);
    print_u32("maxPerStageDescriptorStorageBuffers", p.maxPerStageDescriptorStorageBuffers);
    print_u32("maxPerStageDescriptorSampledImages", p.maxPerStageDescriptorSampledImages);
    print_u32("maxPerStageDescriptorStorageImages", p.maxPerStageDescriptorStorageImages);
    print_u32("maxPerStageDescriptorInputAttachments", p.maxPerStageDescriptorInputAttachments);
    print_u32("maxPerStageResources", p.maxPerStageResources);
    print_u32("maxDescriptorSetSamplers", p.maxDescriptorSetSamplers);
    print_u32("maxDescriptorSetUniformBuffers", p.maxDescriptorSetUniformBuffers);
    print_u32("maxDescriptorSetUniformBuffersDynamic", p.maxDescriptorSetUniformBuffersDynamic);
    print_u32("maxDescriptorSetStorageBuffers", p.maxDescriptorSetStorageBuffers);
    print_u32("maxDescriptorSetStorageBuffersDynamic", p.maxDescriptorSetStorageBuffersDynamic);
    print_u32("maxDescriptorSetSampledImages", p.maxDescriptorSetSampledImages);
    print_u32("maxDescriptorSetStorageImages", p.maxDescriptorSetStorageImages);
    print_u32("maxDescriptorSetInputAttachments", p.maxDescriptorSetInputAttachments);
    print_u32("maxVertexInputAttributes", p.maxVertexInputAttributes);
    print_u32("maxVertexInputBindings", p.maxVertexInputBindings);
    print_u32("maxVertexInputAttributeOffset", p.maxVertexInputAttributeOffset);
    print_u32("maxVertexInputBindingStride", p.maxVertexInputBindingStride);
    print_u32("maxVertexOutputComponents", p.maxVertexOutputComponents);
    print_u32("maxTessellationGenerationLevel", p.maxTessellationGenerationLevel);
    print_u32("maxTessellationPatchSize", p.maxTessellationPatchSize);
    print_u32("maxTessellationControlPerVertexInputComponents", p.maxTessellationControlPerVertexInputComponents);
    print_u32("maxTessellationControlPerVertexOutputComponents", p.maxTessellationControlPerVertexOutputComponents);
    print_u32("maxTessellationControlPerPatchOutputComponents", p.maxTessellationControlPerPatchOutputComponents);
    print_u32("maxTessellationControlTotalOutputComponents", p.maxTessellationControlTotalOutputComponents);
    print_u32("maxTessellationEvaluationInputComponents", p.maxTessellationEvaluationInputComponents);
    print_u32("maxTessellationEvaluationOutputComponents", p.maxTessellationEvaluationOutputComponents);
    print_u32("maxGeometryShaderInvocations", p.maxGeometryShaderInvocations);
    print_u32("maxGeometryInputComponents", p.maxGeometryInputComponents);
    print_u32("maxGeometryOutputComponents", p.maxGeometryOutputComponents);
    print_u32("maxGeometryOutputVertices", p.maxGeometryOutputVertices);
    print_u32("maxGeometryTotalOutputComponents", p.maxGeometryTotalOutputComponents);
    print_u32("maxFragmentInputComponents", p.maxFragmentInputComponents);
    print_u32("maxFragmentOutputAttachments", p.maxFragmentOutputAttachments);
    print_u32("maxFragmentDualSrcAttachments", p.maxFragmentDualSrcAttachments);
    print_u32("maxFragmentCombinedOutputResources", p.maxFragmentCombinedOutputResources);
    print_u32("maxComputeSharedMemorySize", p.maxComputeSharedMemorySize);
    print_u32_3("maxComputeWorkGroupCount", p.maxComputeWorkGroupCount);
    print_u32("maxComputeWorkGroupInvocations", p.maxComputeWorkGroupInvocations);
    print_u32_3("maxComputeWorkGroupSize", p.maxComputeWorkGroupSize);
    print_u32("subPixelPrecisionBits", p.subPixelPrecisionBits);
    print_u32("subTexelPrecisionBits", p.subTexelPrecisionBits);
    print_u32("mipmapPrecisionBits", p.mipmapPrecisionBits);
    print_u32("maxDrawIndexedIndexValue", p.maxDrawIndexedIndexValue);
    print_u32("maxDrawIndirectCount", p.maxDrawIndirectCount);
    print_f32("maxSamplerLodBias", p.maxSamplerLodBias);
    print_f32("maxSamplerAnisotropy", p.maxSamplerAnisotropy);
    print_u32("maxViewports", p.maxViewports);
    print_u32_2("maxViewportDimensions", p.maxViewportDimensions);
    print_f32_2("viewportBoundsRange", p.viewportBoundsRange);
    print_u32("viewportSubPixelBits", p.viewportSubPixelBits);
    print_size("minMemoryMapAlignment", p.minMemoryMapAlignment);
    print_u64("minTexelBufferOffsetAlignment", p.minTexelBufferOffsetAlignment);
    print_u64("minUniformBufferOffsetAlignment", p.minUniformBufferOffsetAlignment);
    print_u64("minStorageBufferOffsetAlignment", p.minStorageBufferOffsetAlignment);
    print_i32("minTexelOffset", p.minTexelOffset);
    print_u32("maxTexelOffset", p.maxTexelOffset);
    print_i32("minTexelGatherOffset", p.minTexelGatherOffset);
    print_u32("maxTexelGatherOffset", p.maxTexelGatherOffset);
    print_f32("minInterpolationOffset", p.minInterpolationOffset);
    print_f32("maxInterpolationOffset", p.maxInterpolationOffset);
    print_u32("subPixelInterpolationOffsetBits", p.subPixelInterpolationOffsetBits);
    print_u32("maxFramebufferWidth", p.maxFramebufferWidth);
    print_u32("maxFramebufferHeight", p.maxFramebufferHeight);
    print_u32("maxFramebufferLayers", p.maxFramebufferLayers);
    print_u32("framebufferColorSampleCounts", p.framebufferColorSampleCounts);
    print_u32("framebufferDepthSampleCounts", p.framebufferDepthSampleCounts);
    print_u32("framebufferStencilSampleCounts", p.framebufferStencilSampleCounts);
    print_u32("framebufferNoAttachmentsSampleCounts", p.framebufferNoAttachmentsSampleCounts);
    print_u32("maxColorAttachments", p.maxColorAttachments);
    print_u32("sampledImageColorSampleCounts", p.sampledImageColorSampleCounts);
    print_u32("sampledImageIntegerSampleCounts", p.sampledImageIntegerSampleCounts);
    print_u32("sampledImageDepthSampleCounts", p.sampledImageDepthSampleCounts);
    print_u32("sampledImageStencilSampleCounts", p.sampledImageStencilSampleCounts);
    print_u32("storageImageSampleCounts", p.storageImageSampleCounts);
    print_u32("maxSampleMaskWords", p.maxSampleMaskWords);
    print_bool("timestampComputeAndGraphics", p.timestampComputeAndGraphics);
    print_f32("timestampPeriod", p.timestampPeriod);
    print_u32("maxClipDistances", p.maxClipDistances);
    print_u32("maxCullDistances", p.maxCullDistances);
    print_u32("maxCombinedClipAndCullDistances", p.maxCombinedClipAndCullDistances);
    print_u32("discreteQueuePriorities", p.discreteQueuePriorities);
    print_f32_2("pointSizeRange", p.pointSizeRange);
    print_f32_2("lineWidthRange", p.lineWidthRange);
    print_f32("pointSizeGranularity", p.pointSizeGranularity);
    print_f32("lineWidthGranularity", p.lineWidthGranularity);
    print_bool("strictLines", p.strictLines);
    print_bool("standardSampleLocations", p.standardSampleLocations);
    print_u64("optimalBufferCopyOffsetAlignment", p.optimalBufferCopyOffsetAlignment);
    print_u64("optimalBufferCopyRowPitchAlignment", p.optimalBufferCopyRowPitchAlignment);
    print_u64("nonCoherentAtomSize", p.nonCoherentAtomSize);
};

void inline printPhysicalFeatures(const VkPhysicalDeviceFeatures& p){
    std::cout << "\t\t<><> Physical Device Features <><>" << std::endl;
    auto print_bool = [](const char* name, VkBool32 v){
        std::cout << "\t\t" << name << " : " << (v ? "true" : "false") << std::endl;
    };
    print_bool("robustBufferAccess", p.robustBufferAccess);
    print_bool("fullDrawIndexUint32", p.fullDrawIndexUint32);
    print_bool("imageCubeArray", p.imageCubeArray);
    print_bool("independentBlend", p.independentBlend);
    print_bool("geometryShader", p.geometryShader);
    print_bool("tessellationShader", p.tessellationShader);
    print_bool("sampleRateShading", p.sampleRateShading);
    print_bool("dualSrcBlend", p.dualSrcBlend);
    print_bool("logicOp", p.logicOp);
    print_bool("multiDrawIndirect", p.multiDrawIndirect);
    print_bool("drawIndirectFirstInstance", p.drawIndirectFirstInstance);
    print_bool("depthClamp", p.depthClamp);
    print_bool("depthBiasClamp", p.depthBiasClamp);
    print_bool("fillModeNonSolid", p.fillModeNonSolid);
    print_bool("depthBounds", p.depthBounds);
    print_bool("wideLines", p.wideLines);
    print_bool("largePoints", p.largePoints);
    print_bool("alphaToOne", p.alphaToOne);
    print_bool("multiViewport", p.multiViewport);
    print_bool("samplerAnisotropy", p.samplerAnisotropy);
    print_bool("textureCompressionETC2", p.textureCompressionETC2);
    print_bool("textureCompressionASTC_LDR", p.textureCompressionASTC_LDR);
    print_bool("textureCompressionBC", p.textureCompressionBC);
    print_bool("occlusionQueryPrecise", p.occlusionQueryPrecise);
    print_bool("pipelineStatisticsQuery", p.pipelineStatisticsQuery);
    print_bool("vertexPipelineStoresAndAtomics", p.vertexPipelineStoresAndAtomics);
    print_bool("fragmentStoresAndAtomics", p.fragmentStoresAndAtomics);
    print_bool("shaderTessellationAndGeometryPointSize", p.shaderTessellationAndGeometryPointSize);
    print_bool("shaderImageGatherExtended", p.shaderImageGatherExtended);
    print_bool("shaderStorageImageExtendedFormats", p.shaderStorageImageExtendedFormats);
    print_bool("shaderStorageImageMultisample", p.shaderStorageImageMultisample);
    print_bool("shaderStorageImageReadWithoutFormat", p.shaderStorageImageReadWithoutFormat);
    print_bool("shaderStorageImageWriteWithoutFormat", p.shaderStorageImageWriteWithoutFormat);
    print_bool("shaderUniformBufferArrayDynamicIndexing", p.shaderUniformBufferArrayDynamicIndexing);
    print_bool("shaderSampledImageArrayDynamicIndexing", p.shaderSampledImageArrayDynamicIndexing);
    print_bool("shaderStorageBufferArrayDynamicIndexing", p.shaderStorageBufferArrayDynamicIndexing);
    print_bool("shaderStorageImageArrayDynamicIndexing", p.shaderStorageImageArrayDynamicIndexing);
    print_bool("shaderClipDistance", p.shaderClipDistance);
    print_bool("shaderCullDistance", p.shaderCullDistance);
    print_bool("shaderFloat64", p.shaderFloat64);
    print_bool("shaderInt64", p.shaderInt64);
    print_bool("shaderInt16", p.shaderInt16);
    print_bool("shaderResourceResidency", p.shaderResourceResidency);
    print_bool("shaderResourceMinLod", p.shaderResourceMinLod);
    print_bool("sparseBinding", p.sparseBinding);
    print_bool("sparseResidencyBuffer", p.sparseResidencyBuffer);
    print_bool("sparseResidencyImage2D", p.sparseResidencyImage2D);
    print_bool("sparseResidencyImage3D", p.sparseResidencyImage3D);
    print_bool("sparseResidency2Samples", p.sparseResidency2Samples);
    print_bool("sparseResidency4Samples", p.sparseResidency4Samples);
    print_bool("sparseResidency8Samples", p.sparseResidency8Samples);
    print_bool("sparseResidency16Samples", p.sparseResidency16Samples);
    print_bool("sparseResidencyAliased", p.sparseResidencyAliased);
    print_bool("variableMultisampleRate", p.variableMultisampleRate);
    print_bool("inheritedQueries", p.inheritedQueries);
}

void inline printMemoryProps(const VkPhysicalDeviceMemoryProperties& p){
    std::cout << "\t\t<><> Physical Memory Properties <><>" << std::endl;
    auto print_u32 = [](const char* name, uint32_t v){
        std::cout << "\t\t" << name << " : " << v << std::endl;
    };
    auto flags_string = [](VkMemoryPropertyFlags flags){
        std::string out;
        auto add = [&out](const char* name){
            if(!out.empty()) out += " | ";
            out += name;
        };
        if(flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) add("DEVICE_LOCAL");
        if(flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) add("HOST_VISIBLE");
        if(flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) add("HOST_COHERENT");
        if(flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) add("HOST_CACHED");
        if(flags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) add("LAZILY_ALLOCATED");
        if(flags & VK_MEMORY_PROPERTY_PROTECTED_BIT) add("PROTECTED");
        if(flags & VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD) add("DEVICE_COHERENT_AMD");
        if(flags & VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD) add("DEVICE_UNCACHED_AMD");
        if(flags & VK_MEMORY_PROPERTY_RDMA_CAPABLE_BIT_NV) add("RDMA_CAPABLE_NV");
        if(out.empty()) out = "0";
        return out;
    };
    auto heap_flags_string = [](VkMemoryHeapFlags flags){
        std::string out;
        auto add = [&out](const char* name){
            if(!out.empty()) out += " | ";
            out += name;
        };
        if(flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) add("DEVICE_LOCAL");
        if(flags & VK_MEMORY_HEAP_MULTI_INSTANCE_BIT) add("MULTI_INSTANCE");
        if(flags & VK_MEMORY_HEAP_TILE_MEMORY_BIT_QCOM) add("TILE_MEMORY_QCOM");
        if(out.empty()) out = "0";
        return out;
    };
    auto bytes_string = [](VkDeviceSize bytes){
        const double b = static_cast<double>(bytes);
        const double kib = 1024.0;
        const double mib = kib * 1024.0;
        const double gib = mib * 1024.0;
        std::stringstream s;
        s.setf(std::ios::fixed);
        s.precision(2);
        if(b >= gib) s << (b / gib) << " GiB";
        else if(b >= mib) s << (b / mib) << " MiB";
        else if(b >= kib) s << (b / kib) << " KiB";
        else s << b << " B";
        return s.str();
    };

    print_u32("memoryTypeCount", p.memoryTypeCount);
    for(uint32_t i = 0; i < p.memoryTypeCount; ++i){
        const VkMemoryType& t = p.memoryTypes[i];
        std::cout << "\t\tmemoryTypes[" << i << "] : heapIndex=" << t.heapIndex
                  << " flags=" << flags_string(t.propertyFlags) << std::endl;
    }
    print_u32("memoryHeapCount", p.memoryHeapCount);
    for(uint32_t i = 0; i < p.memoryHeapCount; ++i){
        const VkMemoryHeap& h = p.memoryHeaps[i];
        std::cout << "\t\tmemoryHeaps[" << i << "] : size=" << h.size
                  << " (" << bytes_string(h.size) << ")"
                  << " flags=" << heap_flags_string(h.flags) << std::endl;
    }
};

void inline printQueueFamilyProperties(const VkQueueFamilyProperties& p){
    auto flags_string = [](VkQueueFlags flags){
        std::string out;
        auto add = [&out](const char* name){
            if(!out.empty()) out += " | ";
            out += name;
        };
        if(flags & VK_QUEUE_GRAPHICS_BIT) add("GRAPHICS");
        if(flags & VK_QUEUE_COMPUTE_BIT) add("COMPUTE");
        if(flags & VK_QUEUE_TRANSFER_BIT) add("TRANSFER");
        if(flags & VK_QUEUE_SPARSE_BINDING_BIT) add("SPARSE_BINDING");
        if(flags & VK_QUEUE_PROTECTED_BIT) add("PROTECTED");
        if(flags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) add("VIDEO_DECODE_KHR");
        if(flags & VK_QUEUE_VIDEO_ENCODE_BIT_KHR) add("VIDEO_ENCODE_KHR");
        if(flags & VK_QUEUE_OPTICAL_FLOW_BIT_NV) add("OPTICAL_FLOW_NV");
        if(flags & VK_QUEUE_DATA_GRAPH_BIT_ARM) add("DATA_GRAPH_ARM");
        if(out.empty()) out = "0";
        return out;
    };
    auto print_u32 = [](const char* name, uint32_t v){
        std::cout << "\t\t" << name << " : " << v << std::endl;
    };
    auto print_extent3 = [](const char* name, const VkExtent3D& v){
        std::cout << "\t\t" << name << " : [" << v.width << ", " << v.height << ", " << v.depth << "]" << std::endl;
    };

    std::cout << "\t\tqueueFlags : " << flags_string(p.queueFlags) << std::endl;
    print_u32("queueCount", p.queueCount);
    print_u32("timestampValidBits", p.timestampValidBits);
    print_extent3("minImageTransferGranularity", p.minImageTransferGranularity);
}

void inline printPresentMode(const VkPresentModeKHR p){
    switch(p){
      case VK_PRESENT_MODE_IMMEDIATE_KHR                    : std::cout << "\t\tpresentMode : IMMEDIATE_KHR" << std::endl; break;
      case VK_PRESENT_MODE_MAILBOX_KHR                      : std::cout << "\t\tpresentMode : MAILBOX_KHR" << std::endl; break;
      case VK_PRESENT_MODE_FIFO_KHR                         : std::cout << "\t\tpresentMode : FIFO_KHR" << std::endl; break;
      case VK_PRESENT_MODE_FIFO_RELAXED_KHR                 : std::cout << "\t\tpresentMode : FIFO_RELAXED_KHR" << std::endl; break;
      case VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR        : std::cout << "\t\tpresentMode : SHARED_DEMAND_REFRESH_KHR" << std::endl; break;
      case VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR    : std::cout << "\t\tpresentMode : SHARED_CONTINUOUS_REFRESH_KHR" << std::endl; break;
      case VK_PRESENT_MODE_FIFO_LATEST_READY_KHR            : std::cout << "\t\tpresentMode : FIFO_LATEST_READY_KHR" << std::endl; break;
#if defined(VK_PRESENT_MODE_FIFO_LATEST_READY_EXT) && (!defined(VK_PRESENT_MODE_FIFO_LATEST_READY_KHR) || VK_PRESENT_MODE_FIFO_LATEST_READY_EXT != VK_PRESENT_MODE_FIFO_LATEST_READY_KHR)
      case VK_PRESENT_MODE_FIFO_LATEST_READY_EXT            : std::cout << "\t\tpresentMode : FIFO_LATEST_READY_EXT" << std::endl; break;
#endif
      default : std::cout << "\t\tpresentMode : invalid ( 0x" << toHexString( static_cast<uint32_t>( p ) ) << " )" << std::endl; break;
    }
};

#pragma once
#include <vector>
#include <cmath>

struct Vertex {
    float pos[3];
    float color[3];
    float uv[2];
    float normal[3];
};

inline std::vector<Vertex> GenerateSphere(
    float radius,
    uint32_t stacks,
    uint32_t slices,
    float r = 1.0f,
    float g = 1.0f,
    float b = 1.0f)
{
    std::vector<Vertex> vertices;

    for (uint32_t i = 0; i < stacks; ++i) {
        float theta0 = M_PI * float(i) / float(stacks);
        float theta1 = M_PI * float(i + 1) / float(stacks);

        for (uint32_t j = 0; j < slices; ++j) {
            float phi0 = 2.0f * M_PI * float(j) / float(slices);
            float phi1 = 2.0f * M_PI * float(j + 1) / float(slices);

            float x00 = radius * std::sin(theta0) * std::cos(phi0);
            float y00 = radius * std::cos(theta0);
            float z00 = radius * std::sin(theta0) * std::sin(phi0);

            float x01 = radius * std::sin(theta0) * std::cos(phi1);
            float y01 = radius * std::cos(theta0);
            float z01 = radius * std::sin(theta0) * std::sin(phi1);

            float x10 = radius * std::sin(theta1) * std::cos(phi0);
            float y10 = radius * std::cos(theta1);
            float z10 = radius * std::sin(theta1) * std::sin(phi0);

            float x11 = radius * std::sin(theta1) * std::cos(phi1);
            float y11 = radius * std::cos(theta1);
            float z11 = radius * std::sin(theta1) * std::sin(phi1);

            Vertex v00{{x00,y00,z00},{r,g,b},{0.0f,0.0f},{x00,y00,z00}};
            Vertex v01{{x01,y01,z01},{r,g,b},{0.0f,0.0f},{x01,y01,z01}};
            Vertex v10{{x10,y10,z10},{r,g,b},{0.0f,0.0f},{x10,y10,z10}};
            Vertex v11{{x11,y11,z11},{r,g,b},{0.0f,0.0f},{x11,y11,z11}};

            vertices.push_back(v00);
            vertices.push_back(v10);
            vertices.push_back(v11);

            vertices.push_back(v00);
            vertices.push_back(v11);
            vertices.push_back(v01);
        }
    }

    return vertices;
}
