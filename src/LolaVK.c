#include "rabbitCt.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vulkan/vulkan.h>

static VkInstance vkInstance;
static VkDebugUtilsMessengerEXT vkDebugUtilsMessenger;
static VkPhysicalDevice vkPhysicalDevice;
static size_t graphicsQueueFamily;
static VkDevice vkDevice;
static VkQueue vkQueue;
static VkImage *vkVoxelBuffers;
static VkDeviceMemory vkVoxelMemory;
static VkFramebuffer *vkVoxelFramebuffers;
static VkImageView *vkVoxelViews;
static VkShaderModule vkVertShaderModule;
static VkShaderModule vkFragShaderModule;
static VkPipelineLayout vkPipelineLayout;
static VkRenderPass vkRenderPass;
static VkPipeline vkPipeline;

#define ARRAY_COUNT(arr) (sizeof(arr) / sizeof((arr)[0]))

#define VK_CALL(func, ...) \
    do { \
        VkResult result_ = func(__VA_ARGS__); \
        if (result_ != VK_SUCCESS) { \
            fprintf(stderr, "%s failed: %d\n  %s:%s:%d", #func, result_, __FILE__, __func__, __LINE__); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

static void pdie(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

static void lolaVkCreateInstance(void)
{
    // Get list of available validation layers
    uint32_t layerCount;
    VK_CALL(vkEnumerateInstanceLayerProperties, &layerCount, NULL);

    VkLayerProperties *properties = calloc(layerCount, sizeof(*properties));
    if (!properties)
        pdie("calloc");

    VK_CALL(vkEnumerateInstanceLayerProperties, &layerCount, properties);

    static const char *enabledLayers[] = {
        "VK_LAYER_KHRONOS_validation",
    };

    // Check if required layers are available
    bool enableLayers = true;
    for (size_t i = 0; i < ARRAY_COUNT(enabledLayers); i++) {
        bool found = false;
        for (size_t j = 0; j < layerCount; j++) {
            if (strcmp(properties[j].layerName, enabledLayers[i]) == 0) {
                found = true;
                break;
            }
        }

        if (!found) {
            fprintf(stderr, "Unable to find layer: %s, Disabling layers...\n", enabledLayers[i]);
            enableLayers = false;
            break;
        }
    }

    free(properties);

    VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "RabbitCT",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "N/A",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_1,
    };

    static const char *enabledExtensions[] = {
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
    };

    VkInstanceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .enabledExtensionCount = ARRAY_COUNT(enabledExtensions),
        .ppEnabledExtensionNames = enabledExtensions,
    };

    // Enable layers if required
    if (enableLayers) {
        createInfo.enabledLayerCount = ARRAY_COUNT(enabledLayers);
        createInfo.ppEnabledLayerNames = enabledLayers;
    }

    // Actually create our VK instance
    VK_CALL(vkCreateInstance, &createInfo, NULL, &vkInstance);
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void *pUserData)
{
    (void)messageSeverity;
    (void)messageType;
    (void)pUserData;

    fprintf(stderr, "validation layer: %s\n", pCallbackData->pMessage);

    return VK_FALSE;
}

static void lolaVkDebugCreate(void)
{
    // Enable debug output
    VkDebugUtilsMessengerCreateInfoEXT createInfoDbg = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debugCallback,
    };

    PFN_vkCreateDebugUtilsMessengerEXT func = (void *)vkGetInstanceProcAddr(vkInstance, "vkCreateDebugUtilsMessengerEXT");
    if (!func) {
        fprintf(stderr, "vkCreateDebugUtilsMessengerEXT wasn't found\n");
        exit(EXIT_FAILURE);
    }

    VK_CALL(func, vkInstance, &createInfoDbg, NULL, &vkDebugUtilsMessenger);
}

static void lolaVkDebugDestroy(void)
{
    PFN_vkDestroyDebugUtilsMessengerEXT func = (void *)vkGetInstanceProcAddr(vkInstance, "vkDestroyDebugUtilsMessengerEXT");
    if (!func) {
        fprintf(stderr, "vkDestroyDebugUtilsMessengerEXT wasn't found\n");
        exit(EXIT_FAILURE);
    }
    func(vkInstance, vkDebugUtilsMessenger, NULL);
}

static size_t getPhysicalDevicePriority(VkPhysicalDevice device)
{
    static const VkPhysicalDeviceType priorityList[] = {
        VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
        VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,
        VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU,
        VK_PHYSICAL_DEVICE_TYPE_CPU,
        VK_PHYSICAL_DEVICE_TYPE_OTHER,
    };

    if (!device)
        return ARRAY_COUNT(priorityList);

    VkPhysicalDeviceProperties2 props = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
    };
    vkGetPhysicalDeviceProperties2(device, &props);

    for (size_t i = 0; i < ARRAY_COUNT(priorityList); i++) {
        if (props.properties.deviceType == priorityList[i])
            return i;
    }

    fprintf(stderr, "Invalid device type: %d\n", props.properties.deviceType);
    exit(EXIT_FAILURE);
}

static void lolaVkPhysicalDeviceSelect(void)
{
    uint32_t deviceCount;
    VK_CALL(vkEnumeratePhysicalDevices, vkInstance, &deviceCount, NULL);

    VkPhysicalDevice *devices = calloc(deviceCount, sizeof(*devices));
    if (!devices)
        pdie("calloc");

    VK_CALL(vkEnumeratePhysicalDevices, vkInstance, &deviceCount, devices);

    VkPhysicalDevice deviceCandidate = NULL;
    for (size_t i = 0; i < deviceCount; i++) {
        if (getPhysicalDevicePriority(devices[i]) < getPhysicalDevicePriority(deviceCandidate))
            deviceCandidate = devices[i];
    }
    if (!deviceCandidate) {
        fprintf(stderr, "No device found\n");
        exit(EXIT_FAILURE);
    }

    vkPhysicalDevice = deviceCandidate;

    VkPhysicalDeviceProperties2 props = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
    };
    vkGetPhysicalDeviceProperties2(deviceCandidate, &props);

    printf("selected device: %s\n", props.properties.deviceName);

    free(devices);
}

static void lolaVkQueueFamilyFind(void)
{
    uint32_t queueFamilyCount;
    vkGetPhysicalDeviceQueueFamilyProperties2(vkPhysicalDevice, &queueFamilyCount, NULL);

    VkQueueFamilyProperties2 *queueFamilies = calloc(queueFamilyCount, sizeof(*queueFamilies));
    if (!queueFamilies)
        pdie("calloc");

    for (size_t i = 0; i < queueFamilyCount; i++)
        queueFamilies[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;

    vkGetPhysicalDeviceQueueFamilyProperties2(vkPhysicalDevice, &queueFamilyCount, queueFamilies);

    bool found = false;
    for (size_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphicsQueueFamily = i;
            found = true;
            break;
        }
    }

    if (!found) {
        fprintf(stderr, "Unable to find graphics queue family out of %u queue families\n", queueFamilyCount);
        exit(EXIT_FAILURE);
    }

    free(queueFamilies);
}

static void lolaVkCreateLogicalDevice(void)
{
    const float queuePriority = 1.0f;

    VkDeviceQueueCreateInfo queueCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = graphicsQueueFamily,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority,
    };

    VkPhysicalDeviceFeatures deviceFeatures = { 0 };

    VkDeviceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pQueueCreateInfos = &queueCreateInfo,
        .queueCreateInfoCount = 1,
        .pEnabledFeatures = &deviceFeatures,
    };

    VK_CALL(vkCreateDevice, vkPhysicalDevice, &createInfo, NULL, &vkDevice);
    vkGetDeviceQueue(vkDevice, graphicsQueueFamily, 0, &vkQueue);
}

static uint32_t lolaVkFindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(vkPhysicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if (typeFilter & (1 << i) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }

    fprintf(stderr, "LolaVK: Unable to find requested memory type: %x\n", typeFilter);
    exit(EXIT_FAILURE);
    return 0;
}

static void lolaVkCreateResources(RabbitCtGlobalData *rcgd)
{
    // Create VkImages
    vkVoxelBuffers = calloc(rcgd->problemSize, sizeof(*vkVoxelBuffers));
    if (!vkVoxelBuffers)
        pdie("calloc");

    for (size_t i = 0; i < rcgd->problemSize; i++) {
        VkImageCreateInfo voxelsImageCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_R32_SFLOAT,
            .extent = { rcgd->problemSize, rcgd->problemSize, 1 },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        };
        VK_CALL(vkCreateImage, vkDevice, &voxelsImageCreateInfo, NULL, &vkVoxelBuffers[i]);
    }

    // Allocate memory for images
    if (rcgd->problemSize == 0) {
        fprintf(stderr, "LolaVK: Problem size must be greater than 0\n");
        exit(EXIT_FAILURE);
    }

    VkMemoryRequirements2 memRequirements = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
    };
    VkImageMemoryRequirementsInfo2 memReqInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
        .image = vkVoxelBuffers[0],
    };
    vkGetImageMemoryRequirements2(vkDevice, &memReqInfo, &memRequirements);

    VkDeviceSize alignedSize = memRequirements.memoryRequirements.size;
    alignedSize = alignedSize + memRequirements.memoryRequirements.alignment - 1;
    alignedSize /= memRequirements.memoryRequirements.alignment;
    alignedSize *= memRequirements.memoryRequirements.alignment;

    VkMemoryAllocateInfo voxelMemoryAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = alignedSize * rcgd->problemSize,
        .memoryTypeIndex = lolaVkFindMemoryType(memRequirements.memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };

    VK_CALL(vkAllocateMemory, vkDevice, &voxelMemoryAllocInfo, NULL, &vkVoxelMemory);

    for (size_t i = 0; i < rcgd->problemSize; i++)
        VK_CALL(vkBindImageMemory, vkDevice, vkVoxelBuffers[i], vkVoxelMemory, alignedSize * i);

    // Create VkImageViews
    vkVoxelViews = calloc(rcgd->problemSize, sizeof(*vkVoxelViews));
    if (!vkVoxelBuffers)
        pdie("calloc");

    for (size_t i = 0; i < rcgd->problemSize; i++) {
        VkImageViewCreateInfo voxelsImageViewCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = vkVoxelBuffers[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_R32_SFLOAT,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        VK_CALL(vkCreateImageView, vkDevice, &voxelsImageViewCreateInfo, NULL, &vkVoxelViews[i]);
    }
}

static void lolaVkDestroyResources(RabbitCtGlobalData *rcgd)
{
    for (size_t i = 0; i < rcgd->problemSize; i++)
        vkDestroyImageView(vkDevice, vkVoxelViews[i], NULL);

    vkFreeMemory(vkDevice, vkVoxelMemory, NULL);

    for (size_t i = 0; i < rcgd->problemSize; i++)
        vkDestroyImage(vkDevice, vkVoxelBuffers[i], NULL);

    free(vkVoxelBuffers);
}

extern unsigned char LolaVK_frag_spv[];
extern unsigned int LolaVK_frag_spv_len;
extern unsigned char LolaVK_vert_spv[];
extern unsigned int LolaVK_vert_spv_len;

static void lolaVkCreatePipeline(const RabbitCtGlobalData *rcgd)
{
    // Shader modules
    VkShaderModuleCreateInfo vertCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = LolaVK_vert_spv_len,
        .pCode = (const uint32_t *)LolaVK_vert_spv,
    };

    VK_CALL(vkCreateShaderModule, vkDevice, &vertCreateInfo, NULL, &vkVertShaderModule);

    VkShaderModuleCreateInfo fragCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = LolaVK_frag_spv_len,
        .pCode = (const uint32_t *)LolaVK_frag_spv,
    };

    VK_CALL(vkCreateShaderModule, vkDevice, &fragCreateInfo, NULL, &vkFragShaderModule);

    // Shader stages
    VkPipelineShaderStageCreateInfo vertShaderStageInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vkVertShaderModule,
        .pName = "main",
    };

    VkPipelineShaderStageCreateInfo fragShaderStageInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = vkFragShaderModule,
        .pName = "main",
    };

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        vertShaderStageInfo,
        fragShaderStageInfo,
    };

    // Dynamic state
    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = ARRAY_COUNT(dynamicStates),
        .pDynamicStates = dynamicStates,
    };

    // Vertex input
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 0,
        .pVertexBindingDescriptions = NULL, // Optional
        .vertexAttributeDescriptionCount = 0,
        .pVertexAttributeDescriptions = NULL, // Optional
    };

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    // Viewport
    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float) rcgd->problemSize,
        .height = (float) rcgd->problemSize,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };

    VkRect2D scissor = {
        .offset = {0, 0},
        .extent = {rcgd->problemSize, rcgd->problemSize},
    };

    VkPipelineViewportStateCreateInfo viewportState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
    };

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .lineWidth = 1.0f,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp = 0.0f,
        .depthBiasSlopeFactor = 0.0f,
    };

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .sampleShadingEnable = VK_FALSE,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .minSampleShading = 1.0f, // Optional
        .pSampleMask = NULL, // Optional
        .alphaToCoverageEnable = VK_FALSE, // Optional
        .alphaToOneEnable = VK_FALSE, // Optional
    };

    // Blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT,
        .blendEnable = VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE, // Optional
        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO, // Optional
        .colorBlendOp = VK_BLEND_OP_ADD, // Optional
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE, // Optional
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO, // Optional
        .alphaBlendOp = VK_BLEND_OP_ADD, // Optional
    };

    VkPipelineColorBlendStateCreateInfo colorBlending = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY, // Optional
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment,
        .blendConstants = { 0.0f, 0.0f, 0.0f, 0.0f }, // Optional
    };

    // Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 0, // Optional
        .pSetLayouts = NULL, // Optional
        .pushConstantRangeCount = 0, // Optional
        .pPushConstantRanges = NULL, // Optional
    };

    VK_CALL(vkCreatePipelineLayout, vkDevice, &pipelineLayoutInfo, NULL, &vkPipelineLayout);

    // Render passes
    VkAttachmentDescription colorAttachment = {
        .format = VK_FORMAT_R32_SFLOAT,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    };

    VkAttachmentReference colorAttachmentRef = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentRef,
    };

    VkRenderPassCreateInfo renderPassInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &colorAttachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
    };

    VK_CALL(vkCreateRenderPass, vkDevice, &renderPassInfo, NULL, &vkRenderPass);

    // Pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = ARRAY_COUNT(shaderStages),
        .pStages = shaderStages,
        .pVertexInputState = &vertexInputInfo,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = NULL, // Optional
        .pColorBlendState = &colorBlending,
        .pDynamicState = &dynamicState,
        .layout = vkPipelineLayout,
        .renderPass = vkRenderPass,
        .subpass = 0,
    };

    VK_CALL(vkCreateGraphicsPipelines, vkDevice, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &vkPipeline);
}

static void lolaVkDestroyPipeline(void)
{
    vkDestroyPipeline(vkDevice, vkPipeline, NULL);
    vkDestroyRenderPass(vkDevice, vkRenderPass, NULL);
    vkDestroyPipelineLayout(vkDevice, vkPipelineLayout, NULL);
    vkDestroyShaderModule(vkDevice, vkFragShaderModule, NULL);
    vkDestroyShaderModule(vkDevice, vkVertShaderModule, NULL);
}

static void lolaVkCreateFramebuffers(RabbitCtGlobalData *rcgd)
{
    vkVoxelFramebuffers = calloc(rcgd->problemSize, sizeof(*vkVoxelFramebuffers));
    if (!vkVoxelFramebuffers)
        pdie("calloc");

    for (size_t i = 0; i < rcgd->problemSize; i++) {
        VkImageView attachments[] = {
            vkVoxelViews[i],
        };

        VkFramebufferCreateInfo framebufferInfo = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = vkRenderPass,
            .attachmentCount = 1,
            .pAttachments = attachments,
            .width = rcgd->problemSize,
            .height = rcgd->problemSize,
            .layers = 1,
        };

        VK_CALL(vkCreateFramebuffer, vkDevice, &framebufferInfo, NULL, &vkVoxelFramebuffers[i]);
    }
}

static void lolaVkDestroyFramebuffers(RabbitCtGlobalData *rcgd)
{
    for (size_t i = 0; i < rcgd->problemSize; i++)
        vkDestroyFramebuffer(vkDevice, vkVoxelFramebuffers[i], NULL);

    free(vkVoxelFramebuffers);
}

int lolaVkPrepare(RabbitCtGlobalData *rcgd)
{
    lolaVkCreateInstance();
    lolaVkDebugCreate();
    lolaVkPhysicalDeviceSelect();
    lolaVkQueueFamilyFind();
    lolaVkCreateLogicalDevice();
    lolaVkCreateResources(rcgd);
    lolaVkCreatePipeline(rcgd);
    lolaVkCreateFramebuffers(rcgd);
    return 1;
}

int lolaVkFinish(RabbitCtGlobalData *rcgd)
{
    lolaVkDestroyFramebuffers(rcgd);
    lolaVkDestroyPipeline();
    lolaVkDestroyResources(rcgd);
    vkDestroyDevice(vkDevice, NULL);
    lolaVkDebugDestroy();
    vkDestroyInstance(vkInstance, NULL);
    return 1;
}

int lolaVkBackprojection(RabbitCtGlobalData *rcgd)
{
    (void)rcgd;

    return 1;
}
