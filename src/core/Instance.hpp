#ifndef INSTANCE_HPP
#define INSTANCE_HPP

#include "Window.hpp"
#include <vulkan/vulkan_raii.hpp>

// Vulkan setup
class Instance {
  public:
    Instance(Window &window, bool isDebug);
    ~Instance();

    Instance(const Instance &) = delete;
    Instance &operator=(const Instance &) = delete;

    vk::raii::Instance *getVkInstance();
    vk::raii::SurfaceKHR *getVkSurface();
    std::vector<vk::raii::PhysicalDevice> getPhysicalDevices();

  private:
    Window &window;
    vk::raii::Context context;
    vk::raii::Instance instance;
    vk::raii::SurfaceKHR surface;

    bool isDebug;
    bool checkValidationLayerSupport();
    VkDebugUtilsMessengerEXT debugMessenger;
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                        VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                        const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                                                        void *pUserData);
    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT &createInfo);
    VkResult CreateDebugUtilsMessengerEXT(const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
                                          const VkAllocationCallbacks *pAllocator);
    void DestroyDebugUtilsMessengerEXT(const VkAllocationCallbacks *pAllocator);

    void initDebugMessenger();
    void initVkInstance();
    void initVkSurface();
};

#endif
