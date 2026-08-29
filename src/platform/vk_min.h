#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <xcb/xcb.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define VKAPI_PTR
#define VK_NULL_HANDLE 0
#define VK_TRUE 1
#define VK_FALSE 0
#define VK_MAKE_VERSION(major,minor,patch) (((major)<<22)|((minor)<<12)|(patch))
#define VK_API_VERSION_1_0 VK_MAKE_VERSION(1,0,0)
#define VK_KHR_SURFACE_EXTENSION_NAME "VK_KHR_surface"
#ifndef _WIN32
#define VK_KHR_XCB_SURFACE_EXTENSION_NAME "VK_KHR_xcb_surface"
#else
#define VK_KHR_WIN32_SURFACE_EXTENSION_NAME "VK_KHR_win32_surface"
#endif
#define VK_KHR_SWAPCHAIN_EXTENSION_NAME "VK_KHR_swapchain"
#define VK_SUBPASS_EXTERNAL (~0u)
#define VK_WHOLE_SIZE (~(uint64_t)0)

typedef uint32_t VkFlags; typedef uint32_t VkBool32; typedef uint64_t VkDeviceSize;
typedef int32_t VkResult; typedef uint32_t VkStructureType; typedef uint32_t VkFormat;
typedef uint32_t VkColorSpaceKHR; typedef uint32_t VkPresentModeKHR; typedef uint32_t VkImageUsageFlags;
typedef uint32_t VkSharingMode; typedef uint32_t VkSurfaceTransformFlagBitsKHR; typedef uint32_t VkCompositeAlphaFlagBitsKHR;
typedef uint32_t VkImageViewType; typedef uint32_t VkComponentSwizzle; typedef uint32_t VkImageAspectFlags;
typedef uint32_t VkSampleCountFlagBits; typedef uint32_t VkAttachmentLoadOp; typedef uint32_t VkAttachmentStoreOp;
typedef uint32_t VkImageLayout; typedef uint32_t VkPipelineBindPoint; typedef uint32_t VkPipelineStageFlags;
typedef uint32_t VkAccessFlags; typedef uint32_t VkDependencyFlags; typedef uint32_t VkCommandPoolCreateFlags;
typedef uint32_t VkCommandBufferLevel; typedef uint32_t VkCommandBufferUsageFlags; typedef uint32_t VkQueueFlags;
typedef uint32_t VkBufferUsageFlags; typedef uint32_t VkMemoryPropertyFlags; typedef uint32_t VkMemoryMapFlags;

typedef struct VkInstance_T* VkInstance; typedef struct VkPhysicalDevice_T* VkPhysicalDevice;
typedef struct VkDevice_T* VkDevice; typedef struct VkQueue_T* VkQueue; typedef struct VkCommandBuffer_T* VkCommandBuffer;
typedef uint64_t VkSurfaceKHR; typedef uint64_t VkSwapchainKHR; typedef uint64_t VkImage; typedef uint64_t VkImageView;
typedef uint64_t VkRenderPass; typedef uint64_t VkFramebuffer; typedef uint64_t VkCommandPool; typedef uint64_t VkSemaphore;
typedef uint64_t VkFence; typedef uint64_t VkDeviceMemory; typedef uint64_t VkBuffer;

enum {
 VK_SUCCESS=0, VK_SUBOPTIMAL_KHR=1000001003, VK_ERROR_OUT_OF_DATE_KHR=-1000001004,
 VK_STRUCTURE_TYPE_APPLICATION_INFO=0, VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO=1,
 VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO=2, VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO=3,
 VK_STRUCTURE_TYPE_SUBMIT_INFO=4, VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO=5, VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO=9,
 VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO=12,
 VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO=15, VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO=38,
 VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO=37, VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO=39,
 VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO=40, VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO=42,
 VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO=43, VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER=45,
#ifndef _WIN32
 VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR=1000005000,
#else
 VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR=1000009000,
#endif
 VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR=1000001000, VK_STRUCTURE_TYPE_PRESENT_INFO_KHR=1000001001,
 VK_FORMAT_B8G8R8A8_UNORM=44, VK_FORMAT_B8G8R8A8_SRGB=50, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR=0,
 VK_PRESENT_MODE_IMMEDIATE_KHR=0, VK_PRESENT_MODE_MAILBOX_KHR=1, VK_PRESENT_MODE_FIFO_KHR=2,
 VK_IMAGE_USAGE_TRANSFER_DST_BIT=0x2, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT=0x10,
 VK_BUFFER_USAGE_TRANSFER_SRC_BIT=0x1, VK_SHARING_MODE_EXCLUSIVE=0,
 VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR=0x1, VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR=0x1,
 VK_IMAGE_VIEW_TYPE_2D=1, VK_COMPONENT_SWIZZLE_IDENTITY=0, VK_IMAGE_ASPECT_COLOR_BIT=0x1,
 VK_SAMPLE_COUNT_1_BIT=0x1, VK_ATTACHMENT_LOAD_OP_LOAD=0, VK_ATTACHMENT_LOAD_OP_CLEAR=1,
 VK_ATTACHMENT_LOAD_OP_DONT_CARE=2, VK_ATTACHMENT_STORE_OP_STORE=0, VK_ATTACHMENT_STORE_OP_DONT_CARE=1,
 VK_IMAGE_LAYOUT_UNDEFINED=0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL=2, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL=7, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR=1000001002,
 VK_PIPELINE_BIND_POINT_GRAPHICS=0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT=0x1, VK_PIPELINE_STAGE_TRANSFER_BIT=0x1000,
 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT=0x2000, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT=0x400,
 VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT=0x100, VK_ACCESS_TRANSFER_WRITE_BIT=0x1000,
 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT=0x2, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT=0x4,
 VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT=0x2, VK_QUEUE_FAMILY_IGNORED=0xffffffffu,
 VK_COMMAND_BUFFER_LEVEL_PRIMARY=0, VK_QUEUE_GRAPHICS_BIT=0x1
};

typedef struct VkAllocationCallbacks VkAllocationCallbacks;
typedef struct { uint32_t width,height; } VkExtent2D;
typedef struct { uint32_t width,height,depth; } VkExtent3D;
typedef struct { int32_t x,y; } VkOffset2D;
typedef struct { VkOffset2D offset; VkExtent2D extent; } VkRect2D;
typedef struct { int32_t x,y,z; } VkOffset3D;
typedef struct { VkStructureType sType; const void* pNext; VkFlags flags; VkDeviceSize size; VkBufferUsageFlags usage; VkSharingMode sharingMode; uint32_t queueFamilyIndexCount; const uint32_t* pQueueFamilyIndices; } VkBufferCreateInfo;
typedef struct { VkDeviceSize size,alignment; uint32_t memoryTypeBits; } VkMemoryRequirements;
typedef struct { VkMemoryPropertyFlags propertyFlags; uint32_t heapIndex; } VkMemoryType;
typedef struct { VkDeviceSize size; VkFlags flags; } VkMemoryHeap;
typedef struct { uint32_t memoryTypeCount; VkMemoryType memoryTypes[32]; uint32_t memoryHeapCount; VkMemoryHeap memoryHeaps[16]; } VkPhysicalDeviceMemoryProperties;
typedef struct { VkStructureType sType; const void* pNext; VkDeviceSize allocationSize; uint32_t memoryTypeIndex; } VkMemoryAllocateInfo;
typedef struct { VkImageAspectFlags aspectMask; uint32_t mipLevel,baseArrayLayer,layerCount; } VkImageSubresourceLayers;
typedef struct { VkDeviceSize bufferOffset; uint32_t bufferRowLength,bufferImageHeight; VkImageSubresourceLayers imageSubresource; VkOffset3D imageOffset; VkExtent3D imageExtent; } VkBufferImageCopy;
typedef struct { float float32[4]; } VkClearColorValue;
typedef union { VkClearColorValue color; struct { float depth; uint32_t stencil; } depthStencil; } VkClearValue;

typedef struct { VkStructureType sType; const void* pNext; const char* pApplicationName; uint32_t applicationVersion; const char* pEngineName; uint32_t engineVersion; uint32_t apiVersion; } VkApplicationInfo;
typedef struct { VkStructureType sType; const void* pNext; VkFlags flags; const VkApplicationInfo* pApplicationInfo; uint32_t enabledLayerCount; const char* const* ppEnabledLayerNames; uint32_t enabledExtensionCount; const char* const* ppEnabledExtensionNames; } VkInstanceCreateInfo;
#ifndef _WIN32
typedef struct { VkStructureType sType; const void* pNext; VkFlags flags; xcb_connection_t* connection; xcb_window_t window; } VkXcbSurfaceCreateInfoKHR;
#else
typedef struct { VkStructureType sType; const void* pNext; VkFlags flags; HINSTANCE hinstance; HWND hwnd; } VkWin32SurfaceCreateInfoKHR;
#endif
typedef struct { VkQueueFlags queueFlags; uint32_t queueCount; uint32_t timestampValidBits; VkExtent3D minImageTransferGranularity; } VkQueueFamilyProperties;
typedef struct { VkStructureType sType; const void* pNext; VkFlags flags; uint32_t queueFamilyIndex; uint32_t queueCount; const float* pQueuePriorities; } VkDeviceQueueCreateInfo;
typedef struct { VkStructureType sType; const void* pNext; VkFlags flags; uint32_t queueCreateInfoCount; const VkDeviceQueueCreateInfo* pQueueCreateInfos; uint32_t enabledLayerCount; const char* const* ppEnabledLayerNames; uint32_t enabledExtensionCount; const char* const* ppEnabledExtensionNames; const void* pEnabledFeatures; } VkDeviceCreateInfo;
typedef struct { uint32_t minImageCount,maxImageCount; VkExtent2D currentExtent,minImageExtent,maxImageExtent; uint32_t maxImageArrayLayers; VkFlags supportedTransforms; VkSurfaceTransformFlagBitsKHR currentTransform; VkFlags supportedCompositeAlpha; VkImageUsageFlags supportedUsageFlags; } VkSurfaceCapabilitiesKHR;
typedef struct { VkFormat format; VkColorSpaceKHR colorSpace; } VkSurfaceFormatKHR;
typedef struct { VkStructureType sType; const void* pNext; VkFlags flags; VkSurfaceKHR surface; uint32_t minImageCount; VkFormat imageFormat; VkColorSpaceKHR imageColorSpace; VkExtent2D imageExtent; uint32_t imageArrayLayers; VkImageUsageFlags imageUsage; VkSharingMode imageSharingMode; uint32_t queueFamilyIndexCount; const uint32_t* pQueueFamilyIndices; VkSurfaceTransformFlagBitsKHR preTransform; VkCompositeAlphaFlagBitsKHR compositeAlpha; VkPresentModeKHR presentMode; VkBool32 clipped; VkSwapchainKHR oldSwapchain; } VkSwapchainCreateInfoKHR;
typedef struct { VkComponentSwizzle r,g,b,a; } VkComponentMapping;
typedef struct { VkImageAspectFlags aspectMask; uint32_t baseMipLevel,levelCount,baseArrayLayer,layerCount; } VkImageSubresourceRange;
typedef struct { VkStructureType sType; const void* pNext; VkAccessFlags srcAccessMask,dstAccessMask; VkImageLayout oldLayout,newLayout; uint32_t srcQueueFamilyIndex,dstQueueFamilyIndex; VkImage image; VkImageSubresourceRange subresourceRange; } VkImageMemoryBarrier;
typedef struct { VkStructureType sType; const void* pNext; VkFlags flags; VkImage image; VkImageViewType viewType; VkFormat format; VkComponentMapping components; VkImageSubresourceRange subresourceRange; } VkImageViewCreateInfo;
typedef struct { VkFlags flags; VkFormat format; VkSampleCountFlagBits samples; VkAttachmentLoadOp loadOp; VkAttachmentStoreOp storeOp; VkAttachmentLoadOp stencilLoadOp; VkAttachmentStoreOp stencilStoreOp; VkImageLayout initialLayout; VkImageLayout finalLayout; } VkAttachmentDescription;
typedef struct { uint32_t attachment; VkImageLayout layout; } VkAttachmentReference;
typedef struct { VkFlags flags; VkPipelineBindPoint pipelineBindPoint; uint32_t inputAttachmentCount; const VkAttachmentReference* pInputAttachments; uint32_t colorAttachmentCount; const VkAttachmentReference* pColorAttachments; const VkAttachmentReference* pResolveAttachments; const VkAttachmentReference* pDepthStencilAttachment; uint32_t preserveAttachmentCount; const uint32_t* pPreserveAttachments; } VkSubpassDescription;
typedef struct { uint32_t srcSubpass,dstSubpass; VkPipelineStageFlags srcStageMask,dstStageMask; VkAccessFlags srcAccessMask,dstAccessMask; VkDependencyFlags dependencyFlags; } VkSubpassDependency;
typedef struct { VkStructureType sType; const void* pNext; VkFlags flags; uint32_t attachmentCount; const VkAttachmentDescription* pAttachments; uint32_t subpassCount; const VkSubpassDescription* pSubpasses; uint32_t dependencyCount; const VkSubpassDependency* pDependencies; } VkRenderPassCreateInfo;
typedef struct { VkStructureType sType; const void* pNext; VkFlags flags; VkRenderPass renderPass; uint32_t attachmentCount; const VkImageView* pAttachments; uint32_t width,height,layers; } VkFramebufferCreateInfo;
typedef struct { VkStructureType sType; const void* pNext; VkCommandPoolCreateFlags flags; uint32_t queueFamilyIndex; } VkCommandPoolCreateInfo;
typedef struct { VkStructureType sType; const void* pNext; VkCommandPool commandPool; VkCommandBufferLevel level; uint32_t commandBufferCount; } VkCommandBufferAllocateInfo;
typedef struct { VkStructureType sType; const void* pNext; VkCommandBufferUsageFlags flags; const void* pInheritanceInfo; } VkCommandBufferBeginInfo;
typedef struct { VkStructureType sType; const void* pNext; VkRenderPass renderPass; VkFramebuffer framebuffer; VkRect2D renderArea; uint32_t clearValueCount; const VkClearValue* pClearValues; } VkRenderPassBeginInfo;
typedef struct { VkImageAspectFlags aspectMask; uint32_t colorAttachment; VkClearValue clearValue; } VkClearAttachment;
typedef struct { VkRect2D rect; uint32_t baseArrayLayer; uint32_t layerCount; } VkClearRect;
typedef struct { VkStructureType sType; const void* pNext; uint32_t waitSemaphoreCount; const VkSemaphore* pWaitSemaphores; const VkPipelineStageFlags* pWaitDstStageMask; uint32_t commandBufferCount; const VkCommandBuffer* pCommandBuffers; uint32_t signalSemaphoreCount; const VkSemaphore* pSignalSemaphores; } VkSubmitInfo;
typedef struct { VkStructureType sType; const void* pNext; VkFlags flags; } VkSemaphoreCreateInfo;
typedef struct { VkStructureType sType; const void* pNext; uint32_t waitSemaphoreCount; const VkSemaphore* pWaitSemaphores; uint32_t swapchainCount; const VkSwapchainKHR* pSwapchains; const uint32_t* pImageIndices; VkResult* pResults; } VkPresentInfoKHR;

typedef void* (*PFN_vkVoidFunction)(void);
typedef PFN_vkVoidFunction (*PFN_vkGetInstanceProcAddr)(VkInstance,const char*);
typedef PFN_vkVoidFunction (*PFN_vkGetDeviceProcAddr)(VkDevice,const char*);

#define VK_FN(name,ret,...) typedef ret (*PFN_##name)(__VA_ARGS__)
VK_FN(vkCreateInstance,VkResult,const VkInstanceCreateInfo*,const VkAllocationCallbacks*,VkInstance*);
VK_FN(vkDestroyInstance,void,VkInstance,const VkAllocationCallbacks*);
#ifndef _WIN32
VK_FN(vkCreateXcbSurfaceKHR,VkResult,VkInstance,const VkXcbSurfaceCreateInfoKHR*,const VkAllocationCallbacks*,VkSurfaceKHR*);
#else
VK_FN(vkCreateWin32SurfaceKHR,VkResult,VkInstance,const VkWin32SurfaceCreateInfoKHR*,const VkAllocationCallbacks*,VkSurfaceKHR*);
#endif
VK_FN(vkDestroySurfaceKHR,void,VkInstance,VkSurfaceKHR,const VkAllocationCallbacks*);
VK_FN(vkEnumeratePhysicalDevices,VkResult,VkInstance,uint32_t*,VkPhysicalDevice*);
VK_FN(vkGetPhysicalDeviceQueueFamilyProperties,void,VkPhysicalDevice,uint32_t*,VkQueueFamilyProperties*);
VK_FN(vkGetPhysicalDeviceMemoryProperties,void,VkPhysicalDevice,VkPhysicalDeviceMemoryProperties*);
VK_FN(vkGetPhysicalDeviceSurfaceSupportKHR,VkResult,VkPhysicalDevice,uint32_t,VkSurfaceKHR,VkBool32*);
VK_FN(vkGetPhysicalDeviceSurfaceCapabilitiesKHR,VkResult,VkPhysicalDevice,VkSurfaceKHR,VkSurfaceCapabilitiesKHR*);
VK_FN(vkGetPhysicalDeviceSurfaceFormatsKHR,VkResult,VkPhysicalDevice,VkSurfaceKHR,uint32_t*,VkSurfaceFormatKHR*);
VK_FN(vkGetPhysicalDeviceSurfacePresentModesKHR,VkResult,VkPhysicalDevice,VkSurfaceKHR,uint32_t*,VkPresentModeKHR*);
VK_FN(vkCreateDevice,VkResult,VkPhysicalDevice,const VkDeviceCreateInfo*,const VkAllocationCallbacks*,VkDevice*);
VK_FN(vkDestroyDevice,void,VkDevice,const VkAllocationCallbacks*);
VK_FN(vkGetDeviceQueue,void,VkDevice,uint32_t,uint32_t,VkQueue*);
VK_FN(vkCreateBuffer,VkResult,VkDevice,const VkBufferCreateInfo*,const VkAllocationCallbacks*,VkBuffer*);
VK_FN(vkDestroyBuffer,void,VkDevice,VkBuffer,const VkAllocationCallbacks*);
VK_FN(vkGetBufferMemoryRequirements,void,VkDevice,VkBuffer,VkMemoryRequirements*);
VK_FN(vkAllocateMemory,VkResult,VkDevice,const VkMemoryAllocateInfo*,const VkAllocationCallbacks*,VkDeviceMemory*);
VK_FN(vkFreeMemory,void,VkDevice,VkDeviceMemory,const VkAllocationCallbacks*);
VK_FN(vkBindBufferMemory,VkResult,VkDevice,VkBuffer,VkDeviceMemory,VkDeviceSize);
VK_FN(vkMapMemory,VkResult,VkDevice,VkDeviceMemory,VkDeviceSize,VkDeviceSize,VkMemoryMapFlags,void**);
VK_FN(vkUnmapMemory,void,VkDevice,VkDeviceMemory);
VK_FN(vkCreateSwapchainKHR,VkResult,VkDevice,const VkSwapchainCreateInfoKHR*,const VkAllocationCallbacks*,VkSwapchainKHR*);
VK_FN(vkDestroySwapchainKHR,void,VkDevice,VkSwapchainKHR,const VkAllocationCallbacks*);
VK_FN(vkGetSwapchainImagesKHR,VkResult,VkDevice,VkSwapchainKHR,uint32_t*,VkImage*);
VK_FN(vkCreateImageView,VkResult,VkDevice,const VkImageViewCreateInfo*,const VkAllocationCallbacks*,VkImageView*);
VK_FN(vkDestroyImageView,void,VkDevice,VkImageView,const VkAllocationCallbacks*);
VK_FN(vkCreateRenderPass,VkResult,VkDevice,const VkRenderPassCreateInfo*,const VkAllocationCallbacks*,VkRenderPass*);
VK_FN(vkDestroyRenderPass,void,VkDevice,VkRenderPass,const VkAllocationCallbacks*);
VK_FN(vkCreateFramebuffer,VkResult,VkDevice,const VkFramebufferCreateInfo*,const VkAllocationCallbacks*,VkFramebuffer*);
VK_FN(vkDestroyFramebuffer,void,VkDevice,VkFramebuffer,const VkAllocationCallbacks*);
VK_FN(vkCreateCommandPool,VkResult,VkDevice,const VkCommandPoolCreateInfo*,const VkAllocationCallbacks*,VkCommandPool*);
VK_FN(vkDestroyCommandPool,void,VkDevice,VkCommandPool,const VkAllocationCallbacks*);
VK_FN(vkAllocateCommandBuffers,VkResult,VkDevice,const VkCommandBufferAllocateInfo*,VkCommandBuffer*);
VK_FN(vkResetCommandBuffer,VkResult,VkCommandBuffer,VkFlags);
VK_FN(vkBeginCommandBuffer,VkResult,VkCommandBuffer,const VkCommandBufferBeginInfo*);
VK_FN(vkEndCommandBuffer,VkResult,VkCommandBuffer);
VK_FN(vkCmdBeginRenderPass,void,VkCommandBuffer,const VkRenderPassBeginInfo*,uint32_t);
VK_FN(vkCmdEndRenderPass,void,VkCommandBuffer);
VK_FN(vkCmdClearAttachments,void,VkCommandBuffer,uint32_t,const VkClearAttachment*,uint32_t,const VkClearRect*);
VK_FN(vkCmdPipelineBarrier,void,VkCommandBuffer,VkPipelineStageFlags,VkPipelineStageFlags,VkDependencyFlags,uint32_t,const void*,uint32_t,const void*,uint32_t,const VkImageMemoryBarrier*);
VK_FN(vkCmdCopyBufferToImage,void,VkCommandBuffer,VkBuffer,VkImage,VkImageLayout,uint32_t,const VkBufferImageCopy*);
VK_FN(vkCreateSemaphore,VkResult,VkDevice,const VkSemaphoreCreateInfo*,const VkAllocationCallbacks*,VkSemaphore*);
VK_FN(vkDestroySemaphore,void,VkDevice,VkSemaphore,const VkAllocationCallbacks*);
VK_FN(vkAcquireNextImageKHR,VkResult,VkDevice,VkSwapchainKHR,uint64_t,VkSemaphore,VkFence,uint32_t*);
VK_FN(vkQueueSubmit,VkResult,VkQueue,uint32_t,const VkSubmitInfo*,VkFence);
VK_FN(vkQueuePresentKHR,VkResult,VkQueue,const VkPresentInfoKHR*);
VK_FN(vkQueueWaitIdle,VkResult,VkQueue);
VK_FN(vkDeviceWaitIdle,VkResult,VkDevice);
#undef VK_FN

#ifdef __cplusplus
}
#endif
