#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_beta.h>
#include <vulkan/vulkan_raii.hpp>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <ranges>
#include <set>
#include <vector>

const auto APPLICATION_NAME = "Vulkan M4 Setup";
const std::vector<const char *> validationLayers = {"VK_LAYER_KHRONOS_validation"};

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

bool checkValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    return std::all_of(validationLayers.begin(), validationLayers.end(), [&availableLayers](const char *layerName) {
        return std::ranges::any_of(availableLayers, [layerName](const auto &layerProperties) {
            return strcmp(layerName, layerProperties.layerName) == 0;
        });
    });
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                    VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                                                    void *pUserData) {
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
    }
    return VK_FALSE;
}

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
                                      const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pDebugMessenger) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT &createInfo) {
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
}

VkDebugUtilsMessengerEXT initDebugMessenger(VkInstance instance) {
    if (!enableValidationLayers)
        return VK_NULL_HANDLE;
    VkDebugUtilsMessengerCreateInfoEXT createInfo;
    populateDebugMessengerCreateInfo(createInfo);

    VkDebugUtilsMessengerEXT debugMessenger;
    if (CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
        throw std::runtime_error("failed to set up debug messenger!");
    }

    return debugMessenger;
}

void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger,
                                   const VkAllocationCallbacks *pAllocator) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(instance, debugMessenger, pAllocator);
    }
}

SDL_Window *initSDL3() {
    if (SDL_Init(SDL_INIT_VIDEO) == false) {
        SDL_Log("Erreur SDL_Init : %s", SDL_GetError());
        throw std::runtime_error("Failed to initialize SDL");
    }

    SDL_Window *window = SDL_CreateWindow(APPLICATION_NAME, 800, 600, SDL_WINDOW_VULKAN);
    if (!window) {
        std::cerr << "Erreur création fenêtre : " << SDL_GetError() << std::endl;
        throw std::runtime_error("Failed to create window");
    }

    return window;
}

vk::raii::Instance initVkInstance(vk::raii::Context &context) {
    if (enableValidationLayers && !checkValidationLayerSupport()) {
        throw std::runtime_error("validation layers requested, but not available!");
    }

    vk::ApplicationInfo appInfo;
    appInfo.pApplicationName = APPLICATION_NAME;
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_4;

    uint32_t extensionCount = 0;
    const char *const *extensions = SDL_Vulkan_GetInstanceExtensions(&extensionCount);

    std::vector<const char *> requiredExtensions;
    for (uint32_t i = 0; i < extensionCount; i++) {
        requiredExtensions.emplace_back(extensions[i]);
    }
    requiredExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    requiredExtensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    if (enableValidationLayers) {
        requiredExtensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    vk::InstanceCreateInfo createInfo;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size());
    createInfo.ppEnabledExtensionNames = requiredExtensions.data();
    createInfo.flags = vk::InstanceCreateFlags();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();

        populateDebugMessengerCreateInfo(debugCreateInfo);
        createInfo.pNext = &debugCreateInfo;
    } else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }

    createInfo.flags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;

    return vk::raii::Instance(context, createInfo);
}

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() { return graphicsFamily.has_value() && presentFamily.has_value(); }
};

QueueFamilyIndices findQueueFamilies(const vk::raii::PhysicalDevice &device, const vk::raii::SurfaceKHR &surface) {
    QueueFamilyIndices indices;

    auto queueFamilies = device.getQueueFamilyProperties();

    for (uint32_t i = 0; i < queueFamilies.size(); ++i) {
        const auto &queueFamily = queueFamilies[i];
        if (queueFamily.queueFlags & vk::QueueFlagBits::eGraphics)
            indices.graphicsFamily = i;
        if (device.getSurfaceSupportKHR(i, *surface))
            indices.presentFamily = i;
    }

    return indices;
}

bool checkDeviceExtensionSupport(const vk::raii::PhysicalDevice &device, const std::vector<const char *> &deviceExtensions) {
    auto availableExtensions = device.enumerateDeviceExtensionProperties();

    std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

    for (const auto &extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<vk::SurfaceFormatKHR> formats;
    std::vector<vk::PresentModeKHR> presentModes;
};

SwapChainSupportDetails querySwapChainSupport(const vk::raii::PhysicalDevice &device, const vk::raii::SurfaceKHR &surface) {
    SwapChainSupportDetails details;

    details.capabilities = device.getSurfaceCapabilitiesKHR(*surface);
    details.formats = device.getSurfaceFormatsKHR(*surface);
    details.presentModes = device.getSurfacePresentModesKHR(*surface);

    return details;
}

int rateDeviceSuitability(const vk::raii::PhysicalDevice &device, const vk::raii::SurfaceKHR &surface,
                          const std::vector<const char *> &deviceExtensions) {
    auto deviceProperties = device.getProperties();
    auto deviceFeatures = device.getFeatures2();

    bool extensionsSupported = checkDeviceExtensionSupport(device, deviceExtensions);

    int score = 0;
    if (deviceProperties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
        score += 10;
    if (findQueueFamilies(device, surface).isComplete())
        score += 10;
    if (extensionsSupported) {
        score += 10;
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device, surface);
        if (!swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty())
            score += 10;
    }

    return score;
}

vk::raii::PhysicalDevice initVkPysicalDevice(const vk::raii::Instance &instance, const vk::raii::SurfaceKHR &surface,
                                             std::vector<const char *> deviceExtensions) {
    auto devices = instance.enumeratePhysicalDevices();

    if (devices.empty()) {
        throw std::runtime_error("failed to find GPUs with Vulkan support!");
    }

    int bestScore = -1;
    size_t bestIndex = 0;

    for (size_t i = 0; i < devices.size(); ++i) {
        int score = rateDeviceSuitability(devices[i], surface, deviceExtensions);
        if (score > bestScore) {
            bestScore = score;
            bestIndex = i;
        }
    }

    if (bestScore > 0) {
        return std::move(devices[bestIndex]);
    } else {
        throw std::runtime_error("failed to find a suitable GPU!");
    }
}

vk::raii::Device initVkLogicalDevice(const vk::raii::PhysicalDevice &physicalDevice, QueueFamilyIndices indices,
                                     const std::vector<const char *> &deviceExtensions) {
    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily.value(), indices.presentFamily.value()};
    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        vk::DeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    vk::PhysicalDeviceFeatures deviceFeatures{};
    vk::DeviceCreateInfo createInfo;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;

    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    return vk::raii::Device(physicalDevice, createInfo);
}

vk::raii::Queue initVkPresentQueue(const vk::raii::Device &logicalDevice, QueueFamilyIndices indices) {
    return vk::raii::Queue(logicalDevice, indices.graphicsFamily.value(), 0);
}

vk::raii::SurfaceKHR initVkSurface(const vk::raii::Instance &instance, SDL_Window *window) {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window, *instance, NULL, &surface)) {
        throw std::runtime_error("Failed to create window surface: " + std::string(SDL_GetError()));
    }
    return vk::raii::SurfaceKHR(instance, surface);
}

vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR> &availableFormats) {
    auto it = std::ranges::find_if(availableFormats, [](const auto &format) {
        return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
    });
    return it != availableFormats.end() ? *it : availableFormats[0];
}

vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR> &availablePresentModes) {
    auto it = std::ranges::find(availablePresentModes, vk::PresentModeKHR::eMailbox);
    return it != availablePresentModes.end() ? *it : vk::PresentModeKHR::eFifo;
}

VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities, SDL_Window *window) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        return capabilities.currentExtent;

    int width, height;
    SDL_GetWindowSizeInPixels(window, &width, &height);

    VkExtent2D actualExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};

    actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    actualExtent.height =
        std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

    return actualExtent;
}

struct SwapChainDetails {
    vk::raii::SwapchainKHR swapChain = nullptr;
    vk::Format imageFormat = vk::Format::eUndefined;
    vk::Extent2D extent = vk::Extent2D{0, 0};
    std::vector<vk::Image> images;
};

SwapChainDetails initSwapChain(const vk::raii::PhysicalDevice &physicalDevice, SDL_Window *window,
                               const vk::raii::SurfaceKHR &surface, QueueFamilyIndices indices,
                               const vk::raii::Device &logicalDevice) {
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice, surface);

    auto surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    auto presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities, window);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    vk::SwapchainCreateInfoKHR createInfo{};
    createInfo.surface = *surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = vk::Extent2D{static_cast<uint32_t>(extent.width), static_cast<uint32_t>(extent.height)};
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;

    uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};
    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = vk::SharingMode::eConcurrent;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = vk::SharingMode::eExclusive;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
    }

    createInfo.preTransform = static_cast<vk::SurfaceTransformFlagBitsKHR>(swapChainSupport.capabilities.currentTransform);
    createInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    createInfo.presentMode = presentMode;
    createInfo.clipped = vk::True;
    createInfo.oldSwapchain = nullptr;

    SwapChainDetails details;
    details.swapChain = vk::raii::SwapchainKHR(logicalDevice, createInfo);
    details.imageFormat = surfaceFormat.format;
    details.extent = vk::Extent2D{static_cast<uint32_t>(extent.width), static_cast<uint32_t>(extent.height)};

    uint32_t swapchainImageCount = 0;
    vkGetSwapchainImagesKHR(*logicalDevice, *details.swapChain, &swapchainImageCount, nullptr);
    std::vector<VkImage> vkImages(swapchainImageCount);
    vkGetSwapchainImagesKHR(*logicalDevice, *details.swapChain, &swapchainImageCount, vkImages.data());
    details.images.assign(vkImages.begin(), vkImages.end());

    return details;
}

std::vector<vk::raii::ImageView> initImageViews(const vk::raii::Device &logicalDevice, const SwapChainDetails &details) {
    std::vector<vk::raii::ImageView> swapChainImageViews;
    swapChainImageViews.reserve(details.images.size());

    for (const auto &image : details.images) {
        vk::ImageViewCreateInfo createInfo{};
        createInfo.image = image;
        createInfo.viewType = vk::ImageViewType::e2D;
        createInfo.format = details.imageFormat;
        createInfo.components.r = vk::ComponentSwizzle::eIdentity;
        createInfo.components.g = vk::ComponentSwizzle::eIdentity;
        createInfo.components.b = vk::ComponentSwizzle::eIdentity;
        createInfo.components.a = vk::ComponentSwizzle::eIdentity;
        createInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        swapChainImageViews.emplace_back(logicalDevice, createInfo);
    }

    return swapChainImageViews;
}

static std::vector<char> readFile(const std::string &filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("failed to open file!");
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

vk::raii::ShaderModule createShaderModule(const std::vector<char> &code, const vk::raii::Device &logicalDevice) {
    vk::ShaderModuleCreateInfo createInfo{};
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

    return vk::raii::ShaderModule(logicalDevice, createInfo);
}

vk::raii::Pipeline initGraphicsPipeline(const vk::raii::Device &logicalDevice, const vk::raii::RenderPass &renderPass,
                                        const vk::raii::PipelineLayout &pipelineLayout, const vk::Extent2D &extent) {
    auto vertShaderCode = readFile("shaders/vert.spv");
    auto fragShaderCode = readFile("shaders/frag.spv");

    vk::raii::ShaderModule vertShaderModule = createShaderModule(vertShaderCode, logicalDevice);
    vk::raii::ShaderModule fragShaderModule = createShaderModule(fragShaderCode, logicalDevice);

    vk::PipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
    vertShaderStageInfo.module = *vertShaderModule;
    vertShaderStageInfo.pName = "main";

    vk::PipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
    fragShaderStageInfo.module = *fragShaderModule;
    fragShaderStageInfo.pName = "main";

    vk::PipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    std::vector<vk::DynamicState> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.pVertexBindingDescriptions = nullptr;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr;

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
    inputAssembly.primitiveRestartEnable = vk::False;

    vk::Viewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    vk::Rect2D scissor{};
    scissor.offset = vk::Offset2D{0, 0};
    scissor.extent = extent;

    vk::PipelineViewportStateCreateInfo viewportState{};
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    vk::PipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.depthClampEnable = vk::False;
    rasterizer.rasterizerDiscardEnable = vk::False;
    rasterizer.polygonMode = vk::PolygonMode::eFill;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = vk::CullModeFlagBits::eBack;
    rasterizer.frontFace = vk::FrontFace::eClockwise;
    rasterizer.depthBiasEnable = vk::False;
    rasterizer.depthBiasConstantFactor = 0.0f;
    rasterizer.depthBiasClamp = 0.0f;
    rasterizer.depthBiasSlopeFactor = 0.0f;

    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sampleShadingEnable = vk::False;
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;
    multisampling.minSampleShading = 1.0f;
    multisampling.pSampleMask = nullptr;
    multisampling.alphaToCoverageEnable = vk::False;
    multisampling.alphaToOneEnable = vk::False;

    vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    colorBlendAttachment.blendEnable = vk::True;
    colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
    colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
    colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;

    vk::PipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.logicOpEnable = vk::False;
    colorBlending.logicOp = vk::LogicOp::eCopy;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    vk::GraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = nullptr;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = *pipelineLayout;
    pipelineInfo.renderPass = *renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    return vk::raii::Pipeline(logicalDevice, nullptr, pipelineInfo);
}

vk::raii::PipelineLayout initPipelineLayout(const vk::raii::Device &logicalDevice) {
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.setLayoutCount = 0;
    pipelineLayoutInfo.pSetLayouts = nullptr;
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;

    return vk::raii::PipelineLayout(logicalDevice, pipelineLayoutInfo);
}

vk::raii::RenderPass initRenderPass(const vk::raii::Device &logicalDevice, vk::Format swapChainImageFormat) {
    vk::AttachmentDescription colorAttachment{};
    colorAttachment.format = swapChainImageFormat;
    colorAttachment.samples = vk::SampleCountFlagBits::e1;
    colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
    colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
    colorAttachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

    vk::AttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

    vk::AttachmentDescription attachments[] = {colorAttachment};
    vk::AttachmentReference colorAttachmentRefs[] = {colorAttachmentRef};

    vk::SubpassDescription subpass{};
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = colorAttachmentRefs;

    vk::RenderPassCreateInfo renderPassInfo{};
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    return vk::raii::RenderPass(logicalDevice, renderPassInfo);
}

std::vector<vk::raii::Framebuffer> initFramebuffers(const vk::raii::RenderPass &renderPass,
                                                    const vk::raii::Device &logicalDevice,
                                                    const std::vector<vk::raii::ImageView> &swapChainImageViews,
                                                    const vk::Extent2D &extent) {
    std::vector<vk::raii::Framebuffer> swapChainFramebuffers;
    swapChainFramebuffers.reserve(swapChainImageViews.size());

    for (const auto &imageView : swapChainImageViews) {
        vk::FramebufferCreateInfo framebufferInfo{};
        framebufferInfo.renderPass = *renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &(*imageView);
        framebufferInfo.width = extent.width;
        framebufferInfo.height = extent.height;
        framebufferInfo.layers = 1;

        swapChainFramebuffers.emplace_back(logicalDevice, framebufferInfo);
    }

    return swapChainFramebuffers;
}

int main() {
    std::vector<const char *> deviceExtensions = {VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME, VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    SDL_Window *window = initSDL3();

    vk::raii::Context context;
    vk::raii::Instance instance = initVkInstance(context);

    VkDebugUtilsMessengerEXT debugMessenger = initDebugMessenger(*instance);
    vk::raii::SurfaceKHR surface = initVkSurface(instance, window);

    vk::raii::PhysicalDevice physicalDevice = initVkPysicalDevice(instance, surface, deviceExtensions);
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice, surface);

    vk::raii::Device logicalDevice = initVkLogicalDevice(physicalDevice, indices, deviceExtensions);

    vk::raii::Queue presentQueue = initVkPresentQueue(logicalDevice, indices);

    SwapChainDetails swapChainDetails = initSwapChain(physicalDevice, window, surface, indices, logicalDevice);

    std::vector<vk::raii::ImageView> swapChainImageViews = initImageViews(logicalDevice, swapChainDetails);

    vk::raii::PipelineLayout pipelineLayout = initPipelineLayout(logicalDevice);
    vk::raii::RenderPass renderPass = initRenderPass(logicalDevice, swapChainDetails.imageFormat);
    vk::raii::Pipeline graphicsPipeline =
        initGraphicsPipeline(logicalDevice, renderPass, pipelineLayout, swapChainDetails.extent);

    std::vector<vk::raii::Framebuffer> swapChainFramebuffers =
        initFramebuffers(renderPass, logicalDevice, swapChainImageViews, swapChainDetails.extent);

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT)
                running = false;
        }
    }

    DestroyDebugUtilsMessengerEXT(*instance, debugMessenger, nullptr);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
