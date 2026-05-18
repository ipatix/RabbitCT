#include "rabbitCt.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vulkan/vulkan.h>

static VkInstance vkInstance;
static VkDebugUtilsMessengerEXT vkDebugUtilsMessenger;

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
    for (size_t i = 0; i < sizeof(enabledLayers) / sizeof(enabledLayers[0]); i++) {
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
        .apiVersion = VK_API_VERSION_1_0,
    };

    static const char *enabledExtensions[] = {
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
    };

    VkInstanceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .enabledExtensionCount = sizeof(enabledExtensions) / sizeof(enabledExtensions[0]),
        .ppEnabledExtensionNames = enabledExtensions,
    };

    // Enable layers if required
    if (enableLayers) {
        createInfo.enabledLayerCount = sizeof(enabledLayers) / sizeof(enabledLayers[0]);
        createInfo.ppEnabledLayerNames = enabledLayers;
    }

    // Actually create our VK instance
    VK_CALL(vkCreateInstance, &createInfo, NULL, &vkInstance);
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void *pUserData) {

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

int lolaVkPrepare(RabbitCtGlobalData *rcgd)
{
    lolaVkCreateInstance();
    lolaVkDebugCreate();
    return 1;
}

int lolaVkFinish(RabbitCtGlobalData *rcgd)
{
    lolaVkDebugDestroy();
    vkDestroyInstance(vkInstance, NULL);
    return 1;
}

int lolaVkBackprojection(RabbitCtGlobalData *rcgd)
{
    return 1;
}
