#include "vk.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace gt4::vk {

void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + ": Vulkan result " + std::to_string(static_cast<int>(result)));
}

uint32_t Device::memoryType(uint32_t bits, VkMemoryPropertyFlags flags) const {
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & flags) == flags) return i;
    throw std::runtime_error("No suitable Vulkan memory type");
}

uint32_t Device::maxSamples(uint32_t requested) const {
    uint32_t samples = 1;
    for (uint32_t candidate = 2; candidate <= requested && candidate <= 64; candidate *= 2)
        if (sampleCounts & candidate) samples = candidate;
    return samples;
}

bool hasStencil(VkFormat format) {
    return format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
           format == VK_FORMAT_D16_UNORM_S8_UINT;
}
bool isDepth(VkFormat format) {
    return format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D16_UNORM || hasStencil(format);
}

void label(const Device& device, VkObjectType type, uint64_t handle, const char* name) {
    if (!vkSetDebugUtilsObjectNameEXT || !name || !*name) return;
    VkDebugUtilsObjectNameInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name;
    vkSetDebugUtilsObjectNameEXT(device.device, &info);
}

void beginLabel(VkCommandBuffer command, const char* name) {
    if (!vkCmdBeginDebugUtilsLabelEXT) return;
    VkDebugUtilsLabelEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    info.pLabelName = name;
    vkCmdBeginDebugUtilsLabelEXT(command, &info);
}

void endLabel(VkCommandBuffer command) {
    if (vkCmdEndDebugUtilsLabelEXT) vkCmdEndDebugUtilsLabelEXT(command);
}

Image createImage(const Device& device, const ImageDesc& desc) {
    Image image;
    image.format = desc.format;
    image.extent = desc.extent;
    image.mips = desc.mips;
    image.layers = desc.cube ? 6 : desc.layers;
    image.samples = desc.samples;
    image.aspect = isDepth(desc.format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.flags = desc.cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = desc.format;
    info.extent = {std::max(1u, desc.extent.width), std::max(1u, desc.extent.height), 1};
    info.mipLevels = desc.mips;
    info.arrayLayers = image.layers;
    info.samples = desc.samples;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = desc.usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check(vkCreateImage(device.device, &info, nullptr, &image.image), "vkCreateImage");
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device.device, image.image, &requirements);
    VkMemoryDedicatedAllocateInfo dedicated{};
    dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicated.image = image.image;
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.pNext = &dedicated;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = device.memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(device.device, &allocation, nullptr, &image.memory), "vkAllocateMemory(image)");
    check(vkBindImageMemory(device.device, image.image, image.memory, 0), "vkBindImageMemory");
    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = image.image;
    view.viewType = desc.cube ? VK_IMAGE_VIEW_TYPE_CUBE : image.layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    view.format = desc.format;
    view.subresourceRange = {image.aspect, 0, desc.mips, 0, image.layers};
    check(vkCreateImageView(device.device, &view, nullptr, &image.view), "vkCreateImageView");
    const bool perMip = desc.mips > 1 || desc.cube;
    if (perMip && (desc.usage & (VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))) {
        image.mipViews.resize(desc.mips);
        for (uint32_t mip = 0; mip < desc.mips; ++mip) {
            view.viewType = image.layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
            view.subresourceRange = {image.aspect, mip, 1, 0, image.layers};
            check(vkCreateImageView(device.device, &view, nullptr, &image.mipViews[mip]), "vkCreateImageView(mip)");
        }
    }
    label(device, VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(image.image), desc.name);
    return image;
}

void destroy(const Device& device, Image& image) {
    for (auto view : image.mipViews) if (view) vkDestroyImageView(device.device, view, nullptr);
    if (image.view) vkDestroyImageView(device.device, image.view, nullptr);
    if (image.image) vkDestroyImage(device.device, image.image, nullptr);
    if (image.memory) vkFreeMemory(device.device, image.memory, nullptr);
    image = Image{};
}

Buffer createBuffer(const Device& device, VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible, bool hostCached) {
    Buffer buffer;
    buffer.size = size;
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(device.device, &info, nullptr, &buffer.buffer), "vkCreateBuffer");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device.device, buffer.buffer, &requirements);
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    const VkMemoryPropertyFlags visible = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (!hostVisible) {
        allocation.memoryTypeIndex = device.memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    } else if (hostCached) {
        // Read back by the CPU: cached memory (reading write-combined memory is two orders of magnitude slower).
        try {
            allocation.memoryTypeIndex = device.memoryType(requirements.memoryTypeBits, visible | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
        } catch (const std::runtime_error&) {
            allocation.memoryTypeIndex = device.memoryType(requirements.memoryTypeBits, visible);
        }
    } else {
        allocation.memoryTypeIndex = device.memoryType(requirements.memoryTypeBits, visible);
    }
    check(vkAllocateMemory(device.device, &allocation, nullptr, &buffer.memory), "vkAllocateMemory(buffer)");
    check(vkBindBufferMemory(device.device, buffer.buffer, buffer.memory, 0), "vkBindBufferMemory");
    if (hostVisible) check(vkMapMemory(device.device, buffer.memory, 0, VK_WHOLE_SIZE, 0, &buffer.mapped), "vkMapMemory");
    return buffer;
}

void destroy(const Device& device, Buffer& buffer) {
    if (buffer.buffer) vkDestroyBuffer(device.device, buffer.buffer, nullptr);
    if (buffer.memory) vkFreeMemory(device.device, buffer.memory, nullptr);
    buffer = Buffer{};
}

namespace {
constexpr VkAccessFlags2 kWrites = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
    VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_HOST_WRITE_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
}

void Barriers::image(Image& image, VkImageLayout layout, VkPipelineStageFlags2 stage, VkAccessFlags2 access, bool discard) {
    const bool write = (access & kWrites) != 0;
    const bool previousWrite = (image.access & kWrites) != 0;
    if (!discard && image.layout == layout && !write && !previousWrite && image.layout != VK_IMAGE_LAYOUT_UNDEFINED) {
        image.stage |= stage;
        image.access |= access;
        return;
    }
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = image.stage == VK_PIPELINE_STAGE_2_NONE ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT : image.stage;
    barrier.srcAccessMask = image.access & kWrites;
    barrier.dstStageMask = stage;
    barrier.dstAccessMask = access;
    barrier.oldLayout = discard ? VK_IMAGE_LAYOUT_UNDEFINED : image.layout;
    barrier.newLayout = layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image.image;
    barrier.subresourceRange = {image.aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
    pending.push_back(barrier);
    image.layout = layout;
    image.stage = stage;
    image.access = access;
}

void Barriers::flush(VkCommandBuffer command) {
    if (pending.empty()) return;
    VkDependencyInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    info.imageMemoryBarrierCount = static_cast<uint32_t>(pending.size());
    info.pImageMemoryBarriers = pending.data();
    vkCmdPipelineBarrier2(command, &info);
    pending.clear();
}

void toSampled(Barriers& b, Image& image, VkPipelineStageFlags2 stage) {
    b.image(image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, stage, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}
void toStorage(Barriers& b, Image& image, bool discard) {
    b.image(image, VK_IMAGE_LAYOUT_GENERAL, kCompute,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, discard);
}
void toColor(Barriers& b, Image& image, bool discard) {
    b.image(image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, kColorOut,
            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, discard);
}
void toDepth(Barriers& b, Image& image, bool discard) {
    b.image(image, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, kDepthTests,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, discard);
}

void destroy(const Device& device, Pipeline& pipeline) {
    if (pipeline.pipeline) vkDestroyPipeline(device.device, pipeline.pipeline, nullptr);
    if (pipeline.layout) vkDestroyPipelineLayout(device.device, pipeline.layout, nullptr);
    if (pipeline.set) vkDestroyDescriptorSetLayout(device.device, pipeline.set, nullptr);
    pipeline = Pipeline{};
}

namespace {
VkShaderModule module(const Device& device, ShaderCode code) {
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.bytes;
    info.pCode = code.words;
    VkShaderModule result = VK_NULL_HANDLE;
    check(vkCreateShaderModule(device.device, &info, nullptr, &result), "vkCreateShaderModule");
    return result;
}

void layout(const Device& device, Pipeline& pipeline, const VkDescriptorType* types, size_t count,
            VkShaderStageFlags stages, uint32_t pushSize) {
    std::vector<VkDescriptorSetLayoutBinding> bindings(count);
    for (size_t i = 0; i < count; ++i) {
        bindings[i].binding = static_cast<uint32_t>(i);
        bindings[i].descriptorType = types[i];
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = stages;
    }
    VkDescriptorSetLayoutCreateInfo set{};
    set.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    set.bindingCount = static_cast<uint32_t>(count);
    set.pBindings = bindings.data();
    check(vkCreateDescriptorSetLayout(device.device, &set, nullptr, &pipeline.set), "vkCreateDescriptorSetLayout");
    VkPushConstantRange range{stages, 0, pushSize};
    VkPipelineLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount = 1;
    info.pSetLayouts = &pipeline.set;
    info.pushConstantRangeCount = pushSize ? 1 : 0;
    info.pPushConstantRanges = pushSize ? &range : nullptr;
    check(vkCreatePipelineLayout(device.device, &info, nullptr, &pipeline.layout), "vkCreatePipelineLayout");
    pipeline.pushSize = pushSize;
    pipeline.stages = stages;
}
}  // namespace

Pipeline createCompute(const Device& device, ShaderCode code, std::initializer_list<VkDescriptorType> types,
                       uint32_t pushSize, const char* name) {
    Pipeline pipeline;
    pipeline.bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
    layout(device, pipeline, types.begin(), types.size(), VK_SHADER_STAGE_COMPUTE_BIT, pushSize);
    VkShaderModule shader = module(device, code);
    VkComputePipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = shader;
    info.stage.pName = "main";
    info.layout = pipeline.layout;
    const VkResult result = vkCreateComputePipelines(device.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline.pipeline);
    vkDestroyShaderModule(device.device, shader, nullptr);
    check(result, name);
    label(device, VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(pipeline.pipeline), name);
    return pipeline;
}

Pipeline createGraphics(const Device& device, const GraphicsDesc& desc) {
    Pipeline pipeline;
    pipeline.bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    layout(device, pipeline, desc.types.data(), desc.types.size(),
           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, desc.pushSize);
    VkShaderModule vertex = module(device, desc.vertex);
    VkShaderModule fragment = desc.fragment.words ? module(device, desc.fragment) : VK_NULL_HANDLE;
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{0, desc.vertices.stride, VK_VERTEX_INPUT_RATE_VERTEX};
    VkPipelineVertexInputStateCreateInfo input{};
    input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (desc.vertices.stride) {
        input.vertexBindingDescriptionCount = 1;
        input.pVertexBindingDescriptions = &binding;
        input.vertexAttributeDescriptionCount = static_cast<uint32_t>(desc.vertices.attributes.size());
        input.pVertexAttributeDescriptions = desc.vertices.attributes.data();
    }
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = desc.cull;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.depthBiasEnable = desc.depthBias ? VK_TRUE : VK_FALSE;
    raster.depthClampEnable = desc.depthClamp ? VK_TRUE : VK_FALSE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = desc.samples;
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = desc.depthTest ? VK_TRUE : VK_FALSE;
    depth.depthWriteEnable = desc.depthWrite ? VK_TRUE : VK_FALSE;
    depth.depthCompareOp = desc.depthCompare;
    std::vector<VkPipelineColorBlendAttachmentState> attachments(desc.colors.size());
    for (auto& attachment : attachments) {
        attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        if (desc.blend == Blend::None) continue;
        attachment.blendEnable = VK_TRUE;
        attachment.colorBlendOp = VK_BLEND_OP_ADD;
        attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        switch (desc.blend) {
        case Blend::Alpha:
            attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            break;
        case Blend::PremultipliedAlpha:
            attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            break;
        case Blend::Additive:
            attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            break;
        case Blend::None: break;
        }
    }
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = static_cast<uint32_t>(attachments.size());
    blend.pAttachments = attachments.data();
    std::vector<VkDynamicState> dynamics{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    if (desc.depthBias) dynamics.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<uint32_t>(dynamics.size());
    dynamic.pDynamicStates = dynamics.data();
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = static_cast<uint32_t>(desc.colors.size());
    rendering.pColorAttachmentFormats = desc.colors.data();
    rendering.depthAttachmentFormat = desc.depth;
    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &rendering;
    info.stageCount = fragment ? 2 : 1;
    info.pStages = stages;
    info.pVertexInputState = &input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = pipeline.layout;
    const VkResult result = vkCreateGraphicsPipelines(device.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline.pipeline);
    vkDestroyShaderModule(device.device, vertex, nullptr);
    if (fragment) vkDestroyShaderModule(device.device, fragment, nullptr);
    check(result, desc.name);
    label(device, VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(pipeline.pipeline), desc.name);
    return pipeline;
}

VkWriteDescriptorSet& Descriptors::next(uint32_t binding, VkDescriptorType type) {
    if (count >= kMax) throw std::logic_error("Too many descriptors in one push");
    auto& write = writes[count];
    write = VkWriteDescriptorSet{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = type;
    return write;
}

Descriptors& Descriptors::sampled(uint32_t binding, VkImageView view, VkSampler sampler, VkImageLayout layout) {
    images[count] = {sampler, view, layout};
    auto& write = next(binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    write.pImageInfo = &images[count++];
    return *this;
}

Descriptors& Descriptors::texture(uint32_t binding, VkImageView view, VkImageLayout layout) {
    images[count] = {VK_NULL_HANDLE, view, layout};
    auto& write = next(binding, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    write.pImageInfo = &images[count++];
    return *this;
}

Descriptors& Descriptors::storage(uint32_t binding, VkImageView view) {
    images[count] = {VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL};
    auto& write = next(binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    write.pImageInfo = &images[count++];
    return *this;
}

Descriptors& Descriptors::sampler(uint32_t binding, VkSampler handle) {
    images[count] = {handle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
    auto& write = next(binding, VK_DESCRIPTOR_TYPE_SAMPLER);
    write.pImageInfo = &images[count++];
    return *this;
}

Descriptors& Descriptors::uniform(uint32_t binding, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range) {
    buffers[count] = {buffer, offset, range};
    auto& write = next(binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    write.pBufferInfo = &buffers[count++];
    return *this;
}

Descriptors& Descriptors::storageBuffer(uint32_t binding, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range) {
    buffers[count] = {buffer, offset, range};
    auto& write = next(binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    write.pBufferInfo = &buffers[count++];
    return *this;
}

void Descriptors::push(VkCommandBuffer command, const Pipeline& pipeline) {
    if (count) vkCmdPushDescriptorSetKHR(command, pipeline.bindPoint, pipeline.layout, 0, count, writes.data());
    count = 0;
}

void bind(VkCommandBuffer command, const Pipeline& pipeline) {
    vkCmdBindPipeline(command, pipeline.bindPoint, pipeline.pipeline);
}

void pushConstants(VkCommandBuffer command, const Pipeline& pipeline, const void* data, uint32_t size) {
    if (size > pipeline.pushSize) throw std::logic_error("Push constant block larger than the pipeline layout");
    vkCmdPushConstants(command, pipeline.layout, pipeline.stages, 0, size, data);
}

void dispatch2D(VkCommandBuffer command, uint32_t width, uint32_t height, uint32_t groupX, uint32_t groupY) {
    vkCmdDispatch(command, (width + groupX - 1) / groupX, (height + groupY - 1) / groupY, 1);
}

VkSampler createSampler(const Device& device, VkFilter filter, VkSamplerMipmapMode mip, VkSamplerAddressMode address,
                        float anisotropy, float lodBias, bool compare) {
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = filter;
    info.minFilter = filter;
    info.mipmapMode = mip;
    info.addressModeU = info.addressModeV = info.addressModeW = address;
    info.mipLodBias = lodBias;
    info.anisotropyEnable = (anisotropy > 1.0f && device.anisotropy) ? VK_TRUE : VK_FALSE;
    info.maxAnisotropy = std::min(std::max(anisotropy, 1.0f), device.maxAnisotropy);
    info.compareEnable = compare ? VK_TRUE : VK_FALSE;
    info.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    info.minLod = 0.0f;
    info.maxLod = VK_LOD_CLAMP_NONE;
    info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    VkSampler sampler = VK_NULL_HANDLE;
    check(vkCreateSampler(device.device, &info, nullptr, &sampler), "vkCreateSampler");
    return sampler;
}

Uploader::Uploader(const Device& device_, VkCommandPool pool_) : device(device_), pool(pool_) {
    VkCommandBufferAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocation.commandPool = pool;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(device.device, &allocation, &cmd), "vkAllocateCommandBuffers(upload)");
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(cmd, &begin), "vkBeginCommandBuffer(upload)");
}

Uploader::~Uploader() {
    for (auto& buffer : staging) destroy(device, buffer);
    if (cmd) vkFreeCommandBuffers(device.device, pool, 1, &cmd);
}

void Uploader::copyToImage(Image& image, const void* data, size_t bytes, uint32_t mip, uint32_t layer, VkExtent2D extent) {
    staging.push_back(createBuffer(device, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true));
    std::memcpy(staging.back().mapped, data, bytes);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {image.aspect, mip, layer, 1};
    copy.imageExtent = {extent.width, extent.height, 1};
    vkCmdCopyBufferToImage(cmd, staging.back().buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
}

void Uploader::copyToBuffer(Buffer& buffer, const void* data, size_t bytes) {
    staging.push_back(createBuffer(device, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true));
    std::memcpy(staging.back().mapped, data, bytes);
    VkBufferCopy copy{0, 0, bytes};
    vkCmdCopyBuffer(cmd, staging.back().buffer, buffer.buffer, 1, &copy);
}

void Uploader::submit() {
    check(vkEndCommandBuffer(cmd), "vkEndCommandBuffer(upload)");
    VkSubmitInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    info.commandBufferCount = 1;
    info.pCommandBuffers = &cmd;
    check(vkQueueSubmit(device.queue, 1, &info, VK_NULL_HANDLE), "vkQueueSubmit(upload)");
    check(vkQueueWaitIdle(device.queue), "vkQueueWaitIdle(upload)");
    for (auto& buffer : staging) destroy(device, buffer);
    staging.clear();
}

}  // namespace gt4::vk
