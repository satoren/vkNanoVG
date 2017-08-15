#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vulkan/vulkan.h>

#include "nanovg.h"
#define NANOVG_VULKAN_IMPLEMENTATION
#include "nanovg_vk.h"

#include "demo.h"
#include "perf.h"

#define NUM_SAMPLES VK_SAMPLE_COUNT_1_BIT

#ifndef NDEBUG
PFN_vkCreateDebugReportCallbackEXT _vkCreateDebugReportCallbackEXT;
PFN_vkDebugReportMessageEXT _vkDebugReportMessageEXT;
PFN_vkDestroyDebugReportCallbackEXT _vkDestroyDebugReportCallbackEXT;

VkDebugReportCallbackEXT debug_report_callback;
VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT report_object_type, uint64_t object,
                                             size_t location, int32_t messageCode, const char *pLayerPrefix, const char *pMessage, void *pUserData) {

  printf("%s\n", pMessage);
  return VK_FALSE;
}
/*
VKAPI_ATTR VkBool32 VKAPI_CALL vkDebugCallback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT, uint64_t object,
	size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage, void* pUserData)
{
	// OutputDebugString(L"Message Code: "); OutputDebugString(std::to_wstring(messageCode).c_str()); OutputDebugString(L"\n");
//	return VK_FALSE;
}*/

#endif

void errorcb(int error, const char *desc) {
  printf("GLFW error %d: %s\n", error, desc);
}

int blowup = 0;
int screenshot = 0;
int premult = 0;

static void key(GLFWwindow *window, int key, int scancode, int action, int mods) {
  if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    glfwSetWindowShouldClose(window, true);
  if (key == GLFW_KEY_SPACE && action == GLFW_PRESS)
    blowup = !blowup;
  if (key == GLFW_KEY_S && action == GLFW_PRESS)
    screenshot = 1;
  if (key == GLFW_KEY_P && action == GLFW_PRESS)
    premult = !premult;
}

static VkInstance createVkInstance() {

  // initialize the VkApplicationInfo structure
  const VkApplicationInfo app_info = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pNext = NULL,
      .pApplicationName = "NanoVG",
      .applicationVersion = 1,
      .pEngineName = "NanoVG",
      .engineVersion = 1,
      .apiVersion = VK_API_VERSION_1_0};

#ifndef NDEBUG
  static const char *append_extensions[] = {
      VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
  };
  const uint32_t append_extensions_count = sizeof(append_extensions) / sizeof(append_extensions[0]);
#else
  static const char *append_extensions[] = {
      0,
  };
  const uint32_t append_extensions_count = 0;
#endif
  uint32_t extensions_count = 0;
  const char **glfw_extensions = glfwGetRequiredInstanceExtensions(&extensions_count);

  const char **extensions = calloc(extensions_count + append_extensions_count, sizeof(char *));

  for (int i = 0; i < extensions_count; ++i) {
    extensions[i] = glfw_extensions[i];
  }
  for (int i = 0; i < append_extensions_count; ++i) {
    extensions[extensions_count++] = append_extensions[i];
  }

  // initialize the VkInstanceCreateInfo structure
  VkInstanceCreateInfo inst_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .pApplicationInfo = &app_info,
      .enabledExtensionCount = extensions_count,
      .ppEnabledExtensionNames = extensions,

      .enabledLayerCount = 0,
      .ppEnabledLayerNames = NULL,
  };

#ifndef NDEBUG
  uint32_t layerCount = 0;
  vkEnumerateInstanceLayerProperties(&layerCount, 0);
  VkLayerProperties *layerprop = (VkLayerProperties *)malloc(sizeof(VkLayerProperties) * layerCount);
  vkEnumerateInstanceLayerProperties(&layerCount, layerprop);
  printf("supported layers:");
  for (int i = 0; i < layerCount; ++i) {
    printf("%s ,", layerprop[i].layerName);
  }
  printf("\n");

  static const char *instance_validation_layers[] = {
      "VK_LAYER_LUNARG_standard_validation"
      //      "VK_LAYER_GOOGLE_threading",
      //      "VK_LAYER_GOOGLE_unique_objects",
      //      "VK_LAYER_LUNARG_api_dump",
      //     "VK_LAYER_LUNARG_device_limits",
      //      "VK_LAYER_LUNARG_draw_state",
      //   "VK_LAYER_LUNARG_image",
      //  "VK_LAYER_LUNARG_mem_tracker",
      // "VK_LAYER_LUNARG_object_tracker",
      //   "VK_LAYER_LUNARG_param_checker",
      //  "VK_LAYER_LUNARG_screenshot",
      //  "VK_LAYER_LUNARG_swapchain",
  };
  inst_info.enabledLayerCount = sizeof(instance_validation_layers) / sizeof(instance_validation_layers[0]);
  inst_info.ppEnabledLayerNames = instance_validation_layers;
#endif

  VkInstance inst;
  VkResult res = vkCreateInstance(&inst_info, NULL, &inst);

  free(extensions);

#ifndef NDEBUG

  free(layerprop);
  // load extensions
  _vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)(vkGetInstanceProcAddr(inst, "vkCreateDebugReportCallbackEXT"));
  _vkDebugReportMessageEXT = (PFN_vkDebugReportMessageEXT)(vkGetInstanceProcAddr(inst, "vkDebugReportMessageEXT"));
  _vkDestroyDebugReportCallbackEXT = (PFN_vkDestroyDebugReportCallbackEXT)(vkGetInstanceProcAddr(inst, "vkDestroyDebugReportCallbackEXT"));
  VkDebugReportCallbackCreateInfoEXT callbackInfo = {
      .sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
      .flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT,
  };

  callbackInfo.pfnCallback = &debugCallback;

  _vkCreateDebugReportCallbackEXT(inst, &callbackInfo, 0, &debug_report_callback);

#endif
  if (res == VK_ERROR_INCOMPATIBLE_DRIVER) {
    printf("cannot find a compatible Vulkan ICD\n");
    exit(-1);
  } else if (res) {
    printf("unknown error\n");
    exit(-1);
  }
  return inst;
}

typedef struct SwapchainBuffers {
  VkImage image;
  VkImageView view;
} SwapchainBuffers;

typedef struct DepthBuffer {
  VkFormat format;

  VkImage image;
  VkDeviceMemory mem;
  VkImageView view;
} DepthBuffer;

typedef struct FrameBuffers {
  VkSwapchainKHR swap_chain;
  SwapchainBuffers *swap_chain_buffers;
  uint32_t swapchain_image_count;
  VkFramebuffer *framebuffers;

  uint32_t current_buffer;

  VkExtent2D buffer_size;

  VkRenderPass render_pass;

  DepthBuffer depth;
  VkFence draw_fence;
  VkSemaphore present_complete_semaphore;

} FrameBuffers;

typedef struct PhysicalDevicesProps {

  VkPhysicalDevice gpu;
  VkPhysicalDeviceProperties gpu_props;
  VkPhysicalDeviceMemoryProperties memory_properties;

  uint32_t queue_family_props_count;
  VkQueueFamilyProperties *queue_family_props;

  uint32_t graphics_queue_family_index;
  uint32_t present_queue_family_index;

  VkFormat format;
  VkColorSpaceKHR color_space;
} PhysicalDevicesProps;

VkDevice createVkDevice(PhysicalDevicesProps props) {

  VkResult res;

  float queue_priorities[1] = {0.0};
  VkDeviceQueueCreateInfo queue_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .pNext = NULL,
      .queueCount = 1,
      .pQueuePriorities = queue_priorities,
      .queueFamilyIndex = props.graphics_queue_family_index,
  };

  const char *deviceExtensions[] = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME,
  };
  VkDeviceCreateInfo device_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = NULL,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queue_info,
      .enabledExtensionCount = sizeof(deviceExtensions) / sizeof(deviceExtensions[0]),
      .ppEnabledExtensionNames = deviceExtensions,
      .enabledLayerCount = 0,
      .ppEnabledLayerNames = NULL,
      .pEnabledFeatures = NULL,
  };
  VkDevice device;
  res = vkCreateDevice(props.gpu, &device_info, NULL, &device);

  assert(res == VK_SUCCESS);
  return device;
}

VkCommandPool createCmdPool(VkDevice device, PhysicalDevicesProps props) {
  VkResult res;
  /* Create a command pool to allocate our command buffer from */
  VkCommandPoolCreateInfo cmd_pool_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .pNext = NULL,
      .queueFamilyIndex = props.graphics_queue_family_index,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
  };
  VkCommandPool cmd_pool;
  res = vkCreateCommandPool(device, &cmd_pool_info, NULL, &cmd_pool);
  assert(res == VK_SUCCESS);
  return cmd_pool;
}
VkCommandBuffer createCmdBuffer(VkDevice device, VkCommandPool cmd_pool) {

  VkResult res;
  VkCommandBufferAllocateInfo cmd = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .pNext = NULL,
      .commandPool = cmd_pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
  };

  VkCommandBuffer cmd_buffer;
  res = vkAllocateCommandBuffers(device, &cmd, &cmd_buffer);
  assert(res == VK_SUCCESS);
  return cmd_buffer;
}

bool memory_type_from_properties(PhysicalDevicesProps propst, uint32_t typeBits, VkFlags requirements_mask, uint32_t *typeIndex) {
  // Search memtypes to find first index with those properties
  for (uint32_t i = 0; i < propst.memory_properties.memoryTypeCount; i++) {
    if ((typeBits & 1) == 1) {
      // Type is available, does it match user properties?
      if ((propst.memory_properties.memoryTypes[i].propertyFlags & requirements_mask) == requirements_mask) {
        *typeIndex = i;
        return true;
      }
    }
    typeBits >>= 1;
  }
  // No memory types matched, return failure
  return false;
}
static PhysicalDevicesProps initPhysicalDevicesProps(VkInstance inst, VkSurfaceKHR surface) {
  VkResult res;
  PhysicalDevicesProps props;
  uint32_t gpu_count = 1;
  res = vkEnumeratePhysicalDevices(inst, &gpu_count, &props.gpu);
  if (res != VK_SUCCESS && res != VK_INCOMPLETE) {
    printf("vkEnumeratePhysicalDevices failed %d \n", res);
    exit(-1);
  }

  vkGetPhysicalDeviceQueueFamilyProperties(props.gpu, &props.queue_family_props_count, NULL);
  assert(props.queue_family_props_count >= 1);

  props.queue_family_props = (VkQueueFamilyProperties *)malloc(props.queue_family_props_count * sizeof(VkQueueFamilyProperties));

  vkGetPhysicalDeviceQueueFamilyProperties(props.gpu, &props.queue_family_props_count, props.queue_family_props);
  assert(props.queue_family_props_count >= 1);

  // Search for a graphics and a present queue in the array of queue
  // families, try to find one that supports both
  props.graphics_queue_family_index = UINT32_MAX;
  props.present_queue_family_index = UINT32_MAX;
  for (uint32_t i = 0; i < props.queue_family_props_count; ++i) {
    VkBool32 supportsPresent;
    vkGetPhysicalDeviceSurfaceSupportKHR(props.gpu, i, surface, &supportsPresent);
    if ((props.queue_family_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
      props.graphics_queue_family_index = i;
      if (supportsPresent == VK_TRUE) {
        props.graphics_queue_family_index = i;
        props.present_queue_family_index = i;
        break;
      }
    }
    if (supportsPresent == VK_TRUE) {
      props.present_queue_family_index = i;
    }
  }
  if (props.graphics_queue_family_index == UINT32_MAX || props.present_queue_family_index == UINT32_MAX) {
    printf("Could not find a queues for both graphics and present\n");
    exit(-1);
  }

  /* This is as good a place as any to do this */
  vkGetPhysicalDeviceMemoryProperties(props.gpu, &props.memory_properties);
  vkGetPhysicalDeviceProperties(props.gpu, &props.gpu_props);

  // Get the list of VkFormats that are supported:
  uint32_t formatCount;
  res = vkGetPhysicalDeviceSurfaceFormatsKHR(props.gpu, surface, &formatCount, NULL);
  assert(res == VK_SUCCESS);
  VkSurfaceFormatKHR *surfFormats = (VkSurfaceFormatKHR *)malloc(formatCount * sizeof(VkSurfaceFormatKHR));
  res = vkGetPhysicalDeviceSurfaceFormatsKHR(props.gpu, surface, &formatCount, surfFormats);
  assert(res == VK_SUCCESS);
  // If the format list includes just one entry of VK_FORMAT_UNDEFINED,
  // the surface has no preferred format.  Otherwise, at least one
  // supported format will be returned.
  if (formatCount == 1 && surfFormats[0].format == VK_FORMAT_UNDEFINED) {
    props.format = VK_FORMAT_B8G8R8A8_UNORM;
  } else {
    assert(formatCount >= 1);
    props.format = surfFormats[0].format;
  }
  props.color_space = surfFormats[0].colorSpace;
  free(surfFormats);
  return props;
}

DepthBuffer createDepthBuffer(VkDevice device, PhysicalDevicesProps props, int width, int height) {
  VkResult res;
  DepthBuffer depth;
  depth.format = VK_FORMAT_D24_UNORM_S8_UINT;

  const VkFormat depth_format = depth.format;
  VkFormatProperties fprops;
  vkGetPhysicalDeviceFormatProperties(props.gpu, depth_format, &fprops);
  VkImageTiling image_tilling;
  if (fprops.linearTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
    image_tilling = VK_IMAGE_TILING_LINEAR;
  } else if (fprops.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
    image_tilling = VK_IMAGE_TILING_OPTIMAL;
  } else {
    /* Try other depth formats? */
    printf("depth_format %d Unsupported.\n", depth_format);
    exit(-1);
  }

  VkImageCreateInfo image_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .pNext = NULL,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = depth_format,
      .tiling = image_tilling,
      .extent.width = width,
      .extent.height = height,
      .extent.depth = 1,
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = NUM_SAMPLES,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .queueFamilyIndexCount = 0,
      .pQueueFamilyIndices = NULL,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      .flags = 0,

  };
  VkMemoryAllocateInfo mem_alloc = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = NULL,
      .allocationSize = 0,
      .memoryTypeIndex = 0,
  };

  VkImageViewCreateInfo view_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                     .pNext = NULL,
                                     .image = VK_NULL_HANDLE,
                                     .format = depth_format,
                                     .components.r = VK_COMPONENT_SWIZZLE_R,
                                     .components.g = VK_COMPONENT_SWIZZLE_G,
                                     .components.b = VK_COMPONENT_SWIZZLE_B,
                                     .components.a = VK_COMPONENT_SWIZZLE_A,
                                     .subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                                     .subresourceRange.baseMipLevel = 0,
                                     .subresourceRange.levelCount = 1,
                                     .subresourceRange.baseArrayLayer = 0,
                                     .subresourceRange.layerCount = 1,
                                     .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                     .flags = 0};

  if (depth_format == VK_FORMAT_D16_UNORM_S8_UINT || depth_format == VK_FORMAT_D24_UNORM_S8_UINT ||
      depth_format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
    view_info.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
  }

  VkMemoryRequirements mem_reqs;

  /* Create image */
  res = vkCreateImage(device, &image_info, NULL, &depth.image);
  assert(res == VK_SUCCESS);

  vkGetImageMemoryRequirements(device, depth.image, &mem_reqs);

  mem_alloc.allocationSize = mem_reqs.size;
  /* Use the memory properties to determine the type of memory required */

  bool pass =
      memory_type_from_properties(props, mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &mem_alloc.memoryTypeIndex);
  assert(pass);

  /* Allocate memory */
  res = vkAllocateMemory(device, &mem_alloc, NULL, &depth.mem);
  assert(res == VK_SUCCESS);

  /* Bind memory */
  res = vkBindImageMemory(device, depth.image, depth.mem, 0);
  assert(res == VK_SUCCESS);

  /* Create image view */
  view_info.image = depth.image;
  res = vkCreateImageView(device, &view_info, NULL, &depth.view);
  assert(res == VK_SUCCESS);

  return depth;
}

static void setupImageLayout(VkCommandBuffer cmdbuffer, VkImage image,
                             VkImageAspectFlags aspectMask,
                             VkImageLayout old_image_layout,
                             VkImageLayout new_image_layout) {

  VkImageMemoryBarrier image_memory_barrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .pNext = NULL,
      .srcAccessMask = 0,
      .dstAccessMask = 0,
      .oldLayout = old_image_layout,
      .newLayout = new_image_layout,
      .image = image,
      .subresourceRange = {aspectMask, 0, 1, 0, 1}};

  if (new_image_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    /* Make sure anything that was copying from this image has completed */
    image_memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  }

  if (new_image_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
    image_memory_barrier.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  }

  if (new_image_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
    image_memory_barrier.dstAccessMask =
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  }

  if (new_image_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    /* Make sure any Copy or CPU writes to image are flushed */
    image_memory_barrier.dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
  }

  VkImageMemoryBarrier *pmemory_barrier = &image_memory_barrier;

  VkPipelineStageFlags src_stages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  VkPipelineStageFlags dest_stages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

  vkCmdPipelineBarrier(cmdbuffer, src_stages, dest_stages, 0, 0, NULL,
                       0, NULL, 1, pmemory_barrier);
}

SwapchainBuffers createSwapchainBuffers(VkDevice device, PhysicalDevicesProps props, VkCommandBuffer cmdbuffer, VkImage image) {

  VkResult res;
  SwapchainBuffers buffer;
  VkImageViewCreateInfo color_attachment_view = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .pNext = NULL,
      .format = props.format,
      .components =
          {
              .r = VK_COMPONENT_SWIZZLE_R,
              .g = VK_COMPONENT_SWIZZLE_G,
              .b = VK_COMPONENT_SWIZZLE_B,
              .a = VK_COMPONENT_SWIZZLE_A,
          },
      .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           .baseMipLevel = 0,
                           .levelCount = 1,
                           .baseArrayLayer = 0,
                           .layerCount = 1},
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .flags = 0,
  };

  buffer.image = image;

  setupImageLayout(
      cmdbuffer, image, VK_IMAGE_ASPECT_COLOR_BIT,
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

  color_attachment_view.image = buffer.image;

  res = vkCreateImageView(device, &color_attachment_view, NULL,
                          &buffer.view);
  assert(res == VK_SUCCESS);
  return buffer;
}

VkRenderPass createRenderPass(VkDevice device, VkFormat color_format, VkFormat depth_format) {
  const VkAttachmentDescription attachments[2] = {
      [0] =
          {
              .format = color_format,
              .samples = NUM_SAMPLES,
              .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
              .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
              .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
              .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
              .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          },
      [1] =
          {
              .format = depth_format,
              .samples = NUM_SAMPLES,
              .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
              .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
              .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
              .initialLayout =
                  VK_IMAGE_LAYOUT_UNDEFINED,
              .finalLayout =
                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
          },
  };
  const VkAttachmentReference color_reference = {
      .attachment = 0,
      .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
  };
  const VkAttachmentReference depth_reference = {
      .attachment = 1,
      .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
  };
  const VkSubpassDescription subpass = {
      .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .flags = 0,
      .inputAttachmentCount = 0,
      .pInputAttachments = NULL,
      .colorAttachmentCount = 1,
      .pColorAttachments = &color_reference,
      .pResolveAttachments = NULL,
      .pDepthStencilAttachment = &depth_reference,
      .preserveAttachmentCount = 0,
      .pPreserveAttachments = NULL,
  };
  const VkRenderPassCreateInfo rp_info = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .pNext = NULL,
      .attachmentCount = 2,
      .pAttachments = attachments,
      .subpassCount = 1,
      .pSubpasses = &subpass,
      .dependencyCount = 0,
      .pDependencies = NULL,
  };
  VkRenderPass render_pass;
  VkResult res;
  res = vkCreateRenderPass(device, &rp_info, NULL, &render_pass);
  assert(res == VK_SUCCESS);
  return render_pass;
}

FrameBuffers createFrameBuffers(VkDevice device, VkSurfaceKHR surface, PhysicalDevicesProps props, VkCommandBuffer setup_cmd_buffer, int winWidth, int winHeight, VkSwapchainKHR oldSwapchain) {

  VkResult res;
  // Check the surface capabilities and formats
  VkSurfaceCapabilitiesKHR surfCapabilities;
  res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
      props.gpu, surface, &surfCapabilities);
  assert(res == VK_SUCCESS);

  VkExtent2D buffer_size;
  // width and height are either both -1, or both not -1.
  if (surfCapabilities.currentExtent.width == (uint32_t)-1) {
    buffer_size.width = winWidth;
    buffer_size.width = winHeight;
  } else {
    // If the surface size is defined, the swap chain size must match
    buffer_size = surfCapabilities.currentExtent;
  }

  DepthBuffer depth = createDepthBuffer(device, props, buffer_size.width, buffer_size.height);

  VkRenderPass render_pass = createRenderPass(device, props.format, depth.format);

  VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR;


  uint32_t presentModeCount;
  vkGetPhysicalDeviceSurfacePresentModesKHR(props.gpu, surface, &presentModeCount, NULL);
  assert(presentModeCount > 0);

  VkPresentModeKHR* presentModes = malloc(sizeof(VkPresentModeKHR)*presentModeCount);
  vkGetPhysicalDeviceSurfacePresentModesKHR(props.gpu, surface, &presentModeCount, presentModes);

  for (size_t i = 0; i < presentModeCount; i++)
  {
	  if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
	  {
		  swapchainPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
		  break;
	  }
	  if ((swapchainPresentMode != VK_PRESENT_MODE_MAILBOX_KHR) && (presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR))
	  {
		  swapchainPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
	  }
  }
  free(presentModes);


  VkSurfaceTransformFlagsKHR preTransform;
  if (surfCapabilities.supportedTransforms &
      VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
    preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  } else {
    preTransform = surfCapabilities.currentTransform;
  }

  // Determine the number of VkImage's to use in the swap chain (we desire to
  // own only 1 image at a time, besides the images being displayed and
  // queued for display):
  uint32_t desiredNumberOfSwapchainImages =
      surfCapabilities.minImageCount + 1;
  if ((surfCapabilities.maxImageCount > 0) &&
      (desiredNumberOfSwapchainImages > surfCapabilities.maxImageCount)) {
    // Application must settle for fewer images than desired:
    desiredNumberOfSwapchainImages = surfCapabilities.maxImageCount;
  }

  const VkSwapchainCreateInfoKHR swapchainInfo = {
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .pNext = NULL,
      .surface = surface,
      .minImageCount = desiredNumberOfSwapchainImages,
      .imageFormat = props.format,
      .imageColorSpace = props.color_space,
      .imageExtent =
          {
              .width = buffer_size.width,
              .height = buffer_size.height,
          },
      .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      .preTransform = preTransform,
      .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .imageArrayLayers = 1,
      .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 0,
      .pQueueFamilyIndices = NULL,
      .presentMode = swapchainPresentMode,
      .oldSwapchain = oldSwapchain,
      .clipped = true,
  };
  VkSwapchainKHR swap_chain;
  res = vkCreateSwapchainKHR(device, &swapchainInfo, NULL,
                             &swap_chain);
  assert(res == VK_SUCCESS);

  if (oldSwapchain != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(device, oldSwapchain, NULL);
  }

  uint32_t swapchain_image_count;
  res = vkGetSwapchainImagesKHR(device, swap_chain,
                                &swapchain_image_count, NULL);
  assert(res == VK_SUCCESS);

  VkImage *swapchainImages =
      (VkImage *)malloc(swapchain_image_count * sizeof(VkImage));

  assert(swapchainImages);

  res = vkGetSwapchainImagesKHR(device, swap_chain,
                                &swapchain_image_count,
                                swapchainImages);
  assert(res == VK_SUCCESS);

  SwapchainBuffers *swap_chain_buffers = (SwapchainBuffers *)malloc(swapchain_image_count * sizeof(SwapchainBuffers));
  for (uint32_t i = 0; i < swapchain_image_count; i++) {
    swap_chain_buffers[i] = createSwapchainBuffers(device, props, setup_cmd_buffer, swapchainImages[i]);
  }
  free(swapchainImages);

  VkImageView attachments[2];
  attachments[1] = depth.view;

  const VkFramebufferCreateInfo fb_info = {
      .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .pNext = NULL,
      .renderPass = render_pass,
      .attachmentCount = 2,
      .pAttachments = attachments,
      .width = buffer_size.width,
      .height = buffer_size.height,
      .layers = 1,
  };
  uint32_t i;

  VkFramebuffer *framebuffers = (VkFramebuffer *)malloc(swapchain_image_count *
                                                        sizeof(VkFramebuffer));
  assert(framebuffers);

  for (i = 0; i < swapchain_image_count; i++) {
    attachments[0] = swap_chain_buffers[i].view;
    res = vkCreateFramebuffer(device, &fb_info, NULL,
                              &framebuffers[i]);
    assert(res == VK_SUCCESS);
  }

  FrameBuffers buffer = {
      .swap_chain = swap_chain,
      .swap_chain_buffers = swap_chain_buffers,
      .swapchain_image_count = swapchain_image_count,
      .framebuffers = framebuffers,
      .current_buffer = 0,
      .buffer_size = buffer_size,
      .render_pass = render_pass,
      .depth = depth,
  };

  return buffer;
}

void destroyFrameBuffers(VkDevice device, FrameBuffers *buffer) {

  for (int i = 0; i < buffer->swapchain_image_count; ++i) {
    vkDestroyImageView(device, buffer->swap_chain_buffers[i].view, 0);
    vkDestroyFramebuffer(device, buffer->framebuffers[i], 0);
  }

  vkDestroyImageView(device, buffer->depth.view, 0);
  vkDestroyImage(device, buffer->depth.image, 0);
  vkFreeMemory(device, buffer->depth.mem, 0);

  vkDestroyRenderPass(device, buffer->render_pass, 0);
  vkDestroySwapchainKHR(device, buffer->swap_chain, 0);

  free(buffer->framebuffers);
  free(buffer->swap_chain_buffers);
}

void prepareFrame(VkDevice device, VkCommandBuffer cmd_buffer, FrameBuffers *fb) {
  VkResult res;

  VkSemaphoreCreateInfo presentCompleteSemaphoreCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
  };

  res = vkCreateSemaphore(device, &presentCompleteSemaphoreCreateInfo, NULL, &fb->present_complete_semaphore);
  assert(res == VK_SUCCESS);

  VkFenceCreateInfo fenceInfo = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
  };
  vkCreateFence(device, &fenceInfo, NULL, &fb->draw_fence);

  // Get the index of the next available swapchain image:
  res = vkAcquireNextImageKHR(device, fb->swap_chain, UINT64_MAX,
                              fb->present_complete_semaphore,
                              fb->draw_fence,
                              &fb->current_buffer);
  assert(res == VK_SUCCESS);

  const VkCommandBufferBeginInfo cmd_buf_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
  };
  res = vkBeginCommandBuffer(cmd_buffer, &cmd_buf_info);
  assert(res == VK_SUCCESS);

  VkClearValue clear_values[2];
  clear_values[0].color.float32[0] = 0.3f;
  clear_values[0].color.float32[1] = 0.3f;
  clear_values[0].color.float32[2] = 0.32f;
  clear_values[0].color.float32[3] = 1.0f;
  clear_values[1].depthStencil.depth = 1.0f;
  clear_values[1].depthStencil.stencil = 0;

  VkRenderPassBeginInfo rp_begin;
  rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  rp_begin.pNext = NULL;
  rp_begin.renderPass = fb->render_pass;
  rp_begin.framebuffer = fb->framebuffers[fb->current_buffer];
  rp_begin.renderArea.offset.x = 0;
  rp_begin.renderArea.offset.y = 0;
  rp_begin.renderArea.extent.width = fb->buffer_size.width;
  rp_begin.renderArea.extent.height = fb->buffer_size.height;
  rp_begin.clearValueCount = 2;
  rp_begin.pClearValues = clear_values;

  vkCmdBeginRenderPass(cmd_buffer, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
  
  VkViewport viewport;
  viewport.width = fb->buffer_size.width;
  viewport.height = fb->buffer_size.height;
  viewport.minDepth = (float)0.0f;
  viewport.maxDepth = (float)1.0f;
  viewport.x = rp_begin.renderArea.offset.x;
  viewport.y = rp_begin.renderArea.offset.y;
  vkCmdSetViewport(cmd_buffer, 0, 1, &viewport);

  VkRect2D scissor = rp_begin.renderArea;
  vkCmdSetScissor(cmd_buffer, 0, 1, &scissor);

}
void submitFrame(VkDevice device, VkQueue queue, VkCommandBuffer cmd_buffer, FrameBuffers *fb) {
  VkResult res;

  vkCmdEndRenderPass(cmd_buffer);

  VkImageMemoryBarrier prePresentBarrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .pNext = NULL,
      .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};

  prePresentBarrier.image = fb->swap_chain_buffers[fb->current_buffer].image;
  VkImageMemoryBarrier *pmemory_barrier = &prePresentBarrier;
  vkCmdPipelineBarrier(cmd_buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0,
                       NULL, 1, pmemory_barrier);

  vkEndCommandBuffer(cmd_buffer);

  VkPipelineStageFlags pipe_stage_flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

  VkSubmitInfo submit_info = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit_info.pNext = NULL;
  submit_info.waitSemaphoreCount = 1;
  submit_info.pWaitSemaphores = &fb->present_complete_semaphore;
  submit_info.pWaitDstStageMask = &pipe_stage_flags;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &cmd_buffer;
  submit_info.signalSemaphoreCount = 0;
  submit_info.pSignalSemaphores = NULL;

  /* Queue the command buffer for execution */
  res = vkQueueSubmit(queue, 1, &submit_info, 0);
  assert(res == VK_SUCCESS);

  /* Now present the image in the window */

  VkPresentInfoKHR present = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  present.pNext = NULL;
  present.swapchainCount = 1;
  present.pSwapchains = &fb->swap_chain;
  present.pImageIndices = &fb->current_buffer;

  /* Make sure command buffer is finished before presenting */
  do {
    res = vkWaitForFences(device, 1, &fb->draw_fence, VK_TRUE, 100000000);
  } while (res == VK_TIMEOUT);

  assert(res == VK_SUCCESS);
  res = vkQueuePresentKHR(queue, &present);
  assert(res == VK_SUCCESS);

  res = vkQueueWaitIdle(queue);
  assert(res == VK_SUCCESS);
  if (fb->draw_fence != VK_NULL_HANDLE) {
    vkDestroyFence(device, fb->draw_fence, 0);
  }
  if (fb->present_complete_semaphore != VK_NULL_HANDLE) {
    vkDestroySemaphore(device, fb->present_complete_semaphore, NULL);
  }
}

int main() {
  GLFWwindow *window;

  if (!glfwInit()) {
    printf("Failed to init GLFW.");
    return -1;
  }

  if (!glfwVulkanSupported()) {
    printf("vulkan dose not supported\n");
    return 1;
  }

  glfwSetErrorCallback(errorcb);

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

  window = glfwCreateWindow(1000, 600, "NanoVG", NULL, NULL);
  if (!window) {
    glfwTerminate();
    return -1;
  }

  glfwSetKeyCallback(window, key);

  glfwSetTime(0);

  VkInstance instance = createVkInstance();

  VkResult res;
  VkSurfaceKHR surface;
  res = glfwCreateWindowSurface(instance, window, 0, &surface);
  if (VK_SUCCESS != res) {
    printf("glfwCreateWindowSurface failed\n");
    exit(-1);
  }

  PhysicalDevicesProps physical_devices_props = initPhysicalDevicesProps(instance, surface);

  VkDevice device = createVkDevice(physical_devices_props);

  VkQueue queue;
  vkGetDeviceQueue(device, physical_devices_props.graphics_queue_family_index, 0, &queue);

  VkCommandPool cmd_pool = createCmdPool(device, physical_devices_props);

  int winWidth, winHeight;
  glfwGetWindowSize(window, &winWidth, &winHeight);

  VkCommandBuffer setup_cmd_buffer = createCmdBuffer(device, cmd_pool);
  const VkCommandBufferBeginInfo cmd_buf_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
  };
  vkBeginCommandBuffer(setup_cmd_buffer, &cmd_buf_info);
  FrameBuffers fb = createFrameBuffers(device, surface, physical_devices_props, setup_cmd_buffer, winWidth, winHeight, 0);
  vkEndCommandBuffer(setup_cmd_buffer);
  VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &setup_cmd_buffer;

  vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(queue);

  vkFreeCommandBuffers(device, cmd_pool, 1, &setup_cmd_buffer);

  VkCommandBuffer cmd_buffer = createCmdBuffer(device, cmd_pool);
  VKNVGCreateInfo create_info = {0};
  create_info.device = device;
  create_info.gpu = physical_devices_props.gpu;
  create_info.renderpass = fb.render_pass;
  create_info.cmd_buffer = cmd_buffer;
  
  NVGcontext *vg = nvgCreateVk(create_info, NVG_ANTIALIAS | NVG_STENCIL_STROKES);

  DemoData data;
  PerfGraph fps;
  if (loadDemoData(vg, &data) == -1)
    return -1;

  initGraph(&fps, GRAPH_RENDER_FPS, "Frame Time");
  double prevt = glfwGetTime();

  while (!glfwWindowShouldClose(window)) {
    float pxRatio;
    double mx, my, t, dt;

    int cwinWidth, cwinHeight;
    glfwGetWindowSize(window, &cwinWidth, &cwinHeight);
    if (winWidth != cwinWidth || winHeight != cwinHeight) {
      winWidth = cwinWidth;
      winHeight = cwinHeight;
      destroyFrameBuffers(device, &fb);
      fb = createFrameBuffers(device, surface, physical_devices_props, setup_cmd_buffer, winWidth, winHeight, 0);
    }

    prepareFrame(device, cmd_buffer, &fb);

    t = glfwGetTime();
    dt = t - prevt;
    prevt = t;
    updateGraph(&fps, (float)dt);
    pxRatio = (float)fb.buffer_size.width / (float)winWidth;

    glfwGetCursorPos(window, &mx, &my);

    nvgBeginFrame(vg, winWidth, winHeight, pxRatio);
    renderDemo(vg, (float)mx, (float)my, (float)winWidth, (float)winHeight, (float)t, blowup, &data);
    renderGraph(vg, 5, 5, &fps);

    nvgEndFrame(vg);

    submitFrame(device, queue, cmd_buffer, &fb);

    glfwPollEvents();
  }
  vkFreeCommandBuffers(device, cmd_pool, 1, &cmd_buffer);

  freeDemoData(vg, &data);
  nvgDeleteVk(vg);

  vkDestroyCommandPool(device, cmd_pool, 0);

  destroyFrameBuffers(device, &fb);
  free(physical_devices_props.queue_family_props);

  vkDestroyDevice(device, NULL);

#ifndef NDEBUG
  _vkDestroyDebugReportCallbackEXT(instance, debug_report_callback, 0);
#endif

  vkDestroyInstance(instance, NULL);

  glfwDestroyWindow(window);
  glfwTerminate();
}