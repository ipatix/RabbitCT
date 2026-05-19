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

    VkLayerProperties *properties = calloc(sizeof(*properties), layerCount);
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

static void lolaVkCreateResources(RabbitCtGlobalData *rcgd)
{
    vkVoxelBuffers = calloc(rcgd->problemSize, sizeof(*vkVoxelBuffers));
    if (!vkVoxelBuffers)
        pdie("calloc");

    for (size_t i = 0; i < rcgd->problemSize; i++) {
        VkImageCreateInfo voxelsImageCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
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
}

static void lolaVkDestroyResources(RabbitCtGlobalData *rcgd)
{
    for (size_t i = 0; i < rcgd->problemSize; i++)
        vkDestroyImage(vkDevice, vkVoxelBuffers[i], NULL);

    free(vkVoxelBuffers);
}

int lolaVkPrepare(RabbitCtGlobalData *rcgd)
{
    lolaVkCreateInstance();
    lolaVkDebugCreate();
    lolaVkPhysicalDeviceSelect();
    lolaVkQueueFamilyFind();
    lolaVkCreateLogicalDevice();
    lolaVkCreateResources(rcgd);
    return 1;
}

int lolaVkFinish(RabbitCtGlobalData *rcgd)
{
    lolaVkDestroyResources(rcgd);
    vkDestroyDevice(vkDevice, NULL);
    lolaVkDebugDestroy();
    vkDestroyInstance(vkInstance, NULL);
    return 1;
}

int lolaVkBackprojection(RabbitCtGlobalData *rcgd)
{
    return 1;
}
