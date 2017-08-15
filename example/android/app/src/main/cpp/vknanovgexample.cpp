#include <string>
#include <vector>
#include <chrono>
#include <cassert>

#include <android/log.h>
#include <android_native_app_glue.h>
#include "example.hpp"

#include "vulkan_wrapper.h"

#include "nanovg.h"
#define NANOVG_VULKAN_IMPLEMENTATION
#include "nanovg_vk.h"

#include "demo.h"
#include "perf.h"

static const char *kTAG = "VkNanoVG Example";
#define LOGI(...) \
  ((void)__android_log_print(ANDROID_LOG_INFO, kTAG, __VA_ARGS__))
#define LOGW(...) \
  ((void)__android_log_print(ANDROID_LOG_WARN, kTAG, __VA_ARGS__))
#define LOGE(...) \
  ((void)__android_log_print(ANDROID_LOG_ERROR, kTAG, __VA_ARGS__))

#define VK_CHECK_RESULT(f)                                                      \
  {                                                                             \
    VkResult res = (f);                                                         \
    if (res != VK_SUCCESS) {                                                    \
      __android_log_print(ANDROID_LOG_ERROR, "nanovgExample ",                  \
                          "Vulkan error %d. File[%s], line[%d]", res, __FILE__, \
                          __LINE__);                                            \
      assert(res == VK_SUCCESS);                                                \
    }                                                                           \
  }

struct VulkanDeviceInfo {
  bool initialized_;

  VkInstance instance_;
  VkPhysicalDevice gpuDevice_;
  VkDevice device_;

  VkSurfaceKHR surface_;
  VkQueue queue_;
};
VulkanDeviceInfo device;

struct DepthBuffer {
  VkFormat format;
  VkImage image;
  VkDeviceMemory mem;
  VkImageView view;
};
struct VulkanSwapchainInfo {
  VkSwapchainKHR swapchain_;
  uint32_t swapchainLength_;

  VkExtent2D displaySize_;
  VkFormat displayFormat_;

  // array of frame buffers and views
  VkFramebuffer *framebuffers_;
  VkImageView *displayViews_;

  DepthBuffer depthbuffer_;
};
VulkanSwapchainInfo swapchain;

struct VulkanRenderInfo {
  VkRenderPass renderPass_;
  VkCommandPool cmdPool_;
  std::vector<VkCommandBuffer> cmdBuffer_;
  VkSemaphore semaphore_;
  VkFence fence_;
};
VulkanRenderInfo render;

struct ExampleData {
  NVGcontext *vg;
  DemoData data;
  PerfGraph fps;

  std::chrono::steady_clock::time_point startt;
  std::chrono::steady_clock::time_point prevt;
};
ExampleData exampleData;

android_app *androidAppCtx = nullptr;

// Create vulkan device
void CreateVulkanDevice(ANativeWindow *platformWindow,
                        VkApplicationInfo *appInfo) {
  std::vector<const char *> instance_extensions;
  std::vector<const char *> device_extensions;

  instance_extensions.push_back("VK_KHR_surface");
  instance_extensions.push_back("VK_KHR_android_surface");

  device_extensions.push_back("VK_KHR_swapchain");

  // **********************************************************
  // Create the Vulkan instance
  VkInstanceCreateInfo instanceCreateInfo{
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pNext = nullptr,
      .pApplicationInfo = appInfo,
      .enabledExtensionCount = static_cast<uint32_t>(instance_extensions.size()),
      .ppEnabledExtensionNames = instance_extensions.data(),
      .enabledLayerCount = 0,
      .ppEnabledLayerNames = nullptr,
  };
  VK_CHECK_RESULT(vkCreateInstance(&instanceCreateInfo, nullptr, &device.instance_));
  VkAndroidSurfaceCreateInfoKHR createInfo = {VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
  createInfo.window = platformWindow;

  VK_CHECK_RESULT(vkCreateAndroidSurfaceKHR(device.instance_, &createInfo, nullptr,
                                            &device.surface_));
  // Find one GPU to use:
  // On Android, every GPU device is equal -- supporting graphics/compute/present
  // for this sample, we use the very first GPU device found on the system
  uint32_t gpuCount = 0;
  VK_CHECK_RESULT(vkEnumeratePhysicalDevices(device.instance_, &gpuCount, nullptr));
  VkPhysicalDevice tmpGpus[gpuCount];
  VK_CHECK_RESULT(vkEnumeratePhysicalDevices(device.instance_, &gpuCount, tmpGpus));
  device.gpuDevice_ = tmpGpus[0]; // Pick up the first GPU Device

  // Create a logical device (vulkan device)
  float priorities[] = {
      1.0f,
  };
  VkDeviceQueueCreateInfo queueCreateInfo{
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .queueCount = 1,
      .queueFamilyIndex = 0,
      .pQueuePriorities = priorities,
  };

  VkDeviceCreateInfo deviceCreateInfo{
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = nullptr,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queueCreateInfo,
      .enabledLayerCount = 0,
      .ppEnabledLayerNames = nullptr,
      .enabledExtensionCount = static_cast<uint32_t>(device_extensions.size()),
      .ppEnabledExtensionNames = device_extensions.data(),
      .pEnabledFeatures = nullptr,
  };

  VK_CHECK_RESULT(vkCreateDevice(device.gpuDevice_, &deviceCreateInfo, nullptr,
                                 &device.device_));
  vkGetDeviceQueue(device.device_, 0, 0, &device.queue_);
}

// A helper function
bool MapMemoryTypeToIndex(uint32_t typeBits,
                          VkFlags requirements_mask,
                          uint32_t *typeIndex) {
  VkPhysicalDeviceMemoryProperties memoryProperties;
  vkGetPhysicalDeviceMemoryProperties(device.gpuDevice_, &memoryProperties);
  // Search memtypes to find first index with those properties
  for (uint32_t i = 0; i < 32; i++) {
    if ((typeBits & 1) == 1) {
      // Type is available, does it match user properties?
      if ((memoryProperties.memoryTypes[i].propertyFlags &
           requirements_mask) == requirements_mask) {
        *typeIndex = i;
        return true;
      }
    }
    typeBits >>= 1;
  }
  return false;
}

void CreateDepthBuffer(uint32_t width, uint32_t height) {
  VkResult res;
  DepthBuffer depth;
  depth.format = VK_FORMAT_D24_UNORM_S8_UINT;

  const VkFormat depth_format = depth.format;
  VkFormatProperties fprops;
  vkGetPhysicalDeviceFormatProperties(device.gpuDevice_, depth_format, &fprops);
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
      .samples = VK_SAMPLE_COUNT_1_BIT,
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
  res = vkCreateImage(device.device_, &image_info, NULL, &depth.image);
  assert(res == VK_SUCCESS);

  vkGetImageMemoryRequirements(device.device_, depth.image, &mem_reqs);

  mem_alloc.allocationSize = mem_reqs.size;
  /* Use the memory properties to determine the type of memory required */

  bool pass = MapMemoryTypeToIndex(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &mem_alloc.memoryTypeIndex);
  assert(pass);

  /* Allocate memory */
  VK_CHECK_RESULT(vkAllocateMemory(device.device_, &mem_alloc, NULL, &depth.mem));
  assert(res == VK_SUCCESS);

  /* Bind memory */
  VK_CHECK_RESULT(vkBindImageMemory(device.device_, depth.image, depth.mem, 0));

  /* Create image view */
  view_info.image = depth.image;
  VK_CHECK_RESULT(vkCreateImageView(device.device_, &view_info, NULL, &depth.view));

  swapchain.depthbuffer_ = depth;
}
void CreateSwapChain() {
  memset(&swapchain, 0, sizeof(swapchain));

  // **********************************************************
  // Get the surface capabilities because:
  //   - It contains the minimal and max length of the chain, we will need it
  //   - It's necessary to query the supported surface format (R8G8B8A8 for
  //   instance ...)
  VkSurfaceCapabilitiesKHR surfaceCapabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device.gpuDevice_, device.surface_,
                                            &surfaceCapabilities);
  // Query the list of supported surface format and choose one we like
  uint32_t formatCount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(device.gpuDevice_, device.surface_,
                                       &formatCount, nullptr);
  VkSurfaceFormatKHR *formats = new VkSurfaceFormatKHR[formatCount];
  vkGetPhysicalDeviceSurfaceFormatsKHR(device.gpuDevice_, device.surface_,
                                       &formatCount, formats);
  LOGI("Got %d formats", formatCount);

  uint32_t chosenFormat;
  for (chosenFormat = 0; chosenFormat < formatCount; chosenFormat++) {
    if (formats[chosenFormat].format == VK_FORMAT_R8G8B8A8_UNORM)
      break;
  }
  assert(chosenFormat < formatCount);

  swapchain.displaySize_ = surfaceCapabilities.currentExtent;
  swapchain.displayFormat_ = formats[chosenFormat].format;

  // **********************************************************
  // Create a swap chain (here we choose the minimum available number of surface
  // in the chain)
  uint32_t queueFamily = 0;
  VkSwapchainCreateInfoKHR swapchainCreateInfo{
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .pNext = nullptr,
      .surface = device.surface_,
      .minImageCount = surfaceCapabilities.minImageCount,
      .imageFormat = formats[chosenFormat].format,
      .imageColorSpace = formats[chosenFormat].colorSpace,
      .imageExtent = surfaceCapabilities.currentExtent,
      .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
      .imageArrayLayers = 1,
      .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 1,
      .pQueueFamilyIndices = &queueFamily,
      .presentMode = VK_PRESENT_MODE_FIFO_KHR,
      .oldSwapchain = VK_NULL_HANDLE,
      .clipped = VK_FALSE,
  };
  VK_CHECK_RESULT(vkCreateSwapchainKHR(device.device_, &swapchainCreateInfo,
                                       nullptr, &swapchain.swapchain_));

  // Get the length of the created swap chain
  VK_CHECK_RESULT(vkGetSwapchainImagesKHR(device.device_, swapchain.swapchain_,
                                          &swapchain.swapchainLength_, nullptr));
  delete[] formats;

  CreateDepthBuffer(swapchain.displaySize_.width, swapchain.displaySize_.height);
}

void CreateFrameBuffers(VkRenderPass &renderPass,
                        VkImageView depthView = VK_NULL_HANDLE) {

  // query display attachment to swapchain
  uint32_t SwapchainImagesCount = 0;
  VK_CHECK_RESULT(vkGetSwapchainImagesKHR(device.device_, swapchain.swapchain_,
                                          &SwapchainImagesCount, nullptr));
  VkImage *displayImages = new VkImage[SwapchainImagesCount];
  VK_CHECK_RESULT(vkGetSwapchainImagesKHR(device.device_, swapchain.swapchain_,
                                          &SwapchainImagesCount, displayImages));

  // create image view for each swapchain image
  swapchain.displayViews_ = new VkImageView[SwapchainImagesCount];
  for (uint32_t i = 0; i < SwapchainImagesCount; i++) {
    VkImageViewCreateInfo viewCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .image = displayImages[i],
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = swapchain.displayFormat_,
        .components = {
            .r = VK_COMPONENT_SWIZZLE_R,
            .g = VK_COMPONENT_SWIZZLE_G,
            .b = VK_COMPONENT_SWIZZLE_B,
            .a = VK_COMPONENT_SWIZZLE_A,
        },
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1,
        },
        .flags = 0,
    };
    VK_CHECK_RESULT(vkCreateImageView(device.device_, &viewCreateInfo, nullptr,
                                      &swapchain.displayViews_[i]));
  }
  delete[] displayImages;

  // create a framebuffer from each swapchain image
  swapchain.framebuffers_ = new VkFramebuffer[swapchain.swapchainLength_];
  for (uint32_t i = 0; i < swapchain.swapchainLength_; i++) {
    VkImageView attachments[2] = {
        swapchain.displayViews_[i], depthView,
    };
    VkFramebufferCreateInfo fbCreateInfo{
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext = nullptr,
        .renderPass = renderPass,
        .layers = 1,
        .attachmentCount = 1, // 2 if using depth
        .pAttachments = attachments,
        .width = static_cast<uint32_t>(swapchain.displaySize_.width),
        .height = static_cast<uint32_t>(swapchain.displaySize_.height),
    };
    fbCreateInfo.attachmentCount = (depthView == VK_NULL_HANDLE ? 1 : 2);

    VK_CHECK_RESULT(vkCreateFramebuffer(device.device_, &fbCreateInfo, nullptr,
                                        &swapchain.framebuffers_[i]));
  }
}

void CreateExampleData() {

  VkPhysicalDeviceMemoryProperties memoryProperties;
  VkPhysicalDeviceProperties gpuProperties;
  vkGetPhysicalDeviceMemoryProperties(device.gpuDevice_, &memoryProperties);
  vkGetPhysicalDeviceProperties(device.gpuDevice_, &gpuProperties);

  VKNVGCreateInfo create_info = {device.device_};
  create_info.renderpass = render.renderPass_;
  create_info.memory_properties = memoryProperties;
  create_info.physical_device_properties = gpuProperties;
  create_info.cmd_buffer = render.cmdBuffer_[0];

  exampleData.vg = nvgCreateVk(create_info, NVG_ANTIALIAS | NVG_STENCIL_STROKES);

  if (loadDemoData(exampleData.vg, &exampleData.data) == -1)
    abort();

  initGraph(&exampleData.fps, GRAPH_RENDER_FPS, "Frame Time");

  exampleData.startt = std::chrono::steady_clock::now();
  exampleData.prevt = exampleData.startt;
}

bool InitExample(android_app *app) {
  androidAppCtx = app;

  // copy resource from assets and change cwd for demo
  std::string resource_dir = app->activity->externalDataPath + std::string("/example");
  copyFromAssets(app->activity->assetManager, "", resource_dir);
  copyFromAssets(app->activity->assetManager, "images", resource_dir + "/images");
  chdir(resource_dir.c_str());

  if (!InitVulkan()) {
    LOGW("Vulkan is unavailable, install vulkan and re-start");
    return false;
  }

  VkApplicationInfo appInfo = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pNext = nullptr,
      .apiVersion = VK_MAKE_VERSION(1, 0, 0),
      .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
      .engineVersion = VK_MAKE_VERSION(1, 0, 0),
      .pApplicationName = "vkNanoVGExample",
      .pEngineName = "vkNanoVGExample",
  };

  // create a device
  CreateVulkanDevice(app->window, &appInfo);

  CreateSwapChain();

  // -----------------------------------------------------------------
  // Create render pass
  VkAttachmentDescription attachmentDescriptions[2] = {
      {
          .format = swapchain.displayFormat_,
          .samples = VK_SAMPLE_COUNT_1_BIT,
          .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
          .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
          .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
          .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
          .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      },
      {
          .format = swapchain.depthbuffer_.format,
          .samples = VK_SAMPLE_COUNT_1_BIT,
          .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
          .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
          .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
          .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
          .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      }};

  VkAttachmentReference colourReference = {
      .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

  const VkAttachmentReference depth_reference = {
      .attachment = 1,
      .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
  };
  VkSubpassDescription subpassDescription{
      .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .flags = 0,
      .inputAttachmentCount = 0,
      .pInputAttachments = nullptr,
      .colorAttachmentCount = 1,
      .pColorAttachments = &colourReference,
      .pResolveAttachments = nullptr,
      .pDepthStencilAttachment = &depth_reference,
      .preserveAttachmentCount = 0,
      .pPreserveAttachments = nullptr,
  };
  VkRenderPassCreateInfo renderPassCreateInfo{
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .pNext = nullptr,
      .attachmentCount = 21,
      .pAttachments = attachmentDescriptions,
      .subpassCount = 1,
      .pSubpasses = &subpassDescription,
      .dependencyCount = 0,
      .pDependencies = nullptr,
  };
  VK_CHECK_RESULT(vkCreateRenderPass(device.device_, &renderPassCreateInfo,
                                     nullptr, &render.renderPass_));

  // -----------------------------------------------------------------
  // Create 2 frame buffers.
  CreateFrameBuffers(render.renderPass_, swapchain.depthbuffer_.view);

  // -----------------------------------------------
  // Create a pool of command buffers to allocate command buffer from
  VkCommandPoolCreateInfo cmdPoolCreateInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .pNext = nullptr,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = 0,
  };
  VK_CHECK_RESULT(vkCreateCommandPool(device.device_, &cmdPoolCreateInfo,
                                      nullptr, &render.cmdPool_));

  render.cmdBuffer_.resize(1);
  VkCommandBufferAllocateInfo cmdBufferCreateInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .pNext = nullptr,
      .commandPool = render.cmdPool_,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = static_cast<uint32_t>(render.cmdBuffer_.size()),
  };
  VK_CHECK_RESULT(vkAllocateCommandBuffers(device.device_,
                                           &cmdBufferCreateInfo,
                                           render.cmdBuffer_.data()));

  CreateExampleData();

  // We need to create a fence to be able, in the main loop, to wait for our
  // draw command(s) to finish before swapping the framebuffers
  VkFenceCreateInfo fenceCreateInfo{
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
  };
  VK_CHECK_RESULT(vkCreateFence(device.device_, &fenceCreateInfo,
                                nullptr, &render.fence_));

  // We need to create a semaphore to be able to wait, in the main loop, for our
  // framebuffer to be available for us before drawing.
  VkSemaphoreCreateInfo semaphoreCreateInfo{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
  };
  VK_CHECK_RESULT(vkCreateSemaphore(device.device_, &semaphoreCreateInfo,
                                    nullptr, &render.semaphore_));

  device.initialized_ = true;
  return true;
}

void DeleteSwapChain() {
  for (int i = 0; i < swapchain.swapchainLength_; i++) {
    vkDestroyFramebuffer(device.device_, swapchain.framebuffers_[i], nullptr);
    vkDestroyImageView(device.device_, swapchain.displayViews_[i], nullptr);
  }
  delete[] swapchain.framebuffers_;
  delete[] swapchain.displayViews_;

  vkDestroySwapchainKHR(device.device_, swapchain.swapchain_, nullptr);
}

void DeleteExampleData() {

  freeDemoData(exampleData.vg, &exampleData.data);
  nvgDeleteVk(exampleData.vg);
}

void DeleteExample(void) {
  DeleteExampleData();

  vkFreeCommandBuffers(device.device_, render.cmdPool_,
                       render.cmdBuffer_.size(), render.cmdBuffer_.data());

  vkDestroyCommandPool(device.device_, render.cmdPool_, nullptr);
  vkDestroyRenderPass(device.device_, render.renderPass_, nullptr);
  DeleteSwapChain();

  vkDestroyDevice(device.device_, nullptr);
  vkDestroyInstance(device.instance_, nullptr);
  device.initialized_ = false;
}

bool IsExampleReady(void) {
  return device.initialized_;
}

void ExampleBuildCmdBudder(VkCommandBuffer cmd_buffer, int fbindex) {

  std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
  double dt = std::chrono::duration_cast<std::chrono::milliseconds>(t - exampleData.prevt).count() / 1000.0;
  double st = std::chrono::duration_cast<std::chrono::milliseconds>(t - exampleData.startt).count() / 1000.0;
  exampleData.prevt = t;

  const VkCommandBufferBeginInfo cmd_buf_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
  };
  VK_CHECK_RESULT(vkBeginCommandBuffer(cmd_buffer, &cmd_buf_info));

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
  rp_begin.renderPass = render.renderPass_;
  rp_begin.framebuffer = swapchain.framebuffers_[fbindex];
  rp_begin.renderArea.offset.x = 0;
  rp_begin.renderArea.offset.y = 0;
  rp_begin.renderArea.extent = swapchain.displaySize_;
  rp_begin.clearValueCount = 2;
  rp_begin.pClearValues = clear_values;

  vkCmdBeginRenderPass(cmd_buffer, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

  updateGraph(&exampleData.fps, (float)dt);

    double pixrate= 1.5;
  nvgBeginFrame(exampleData.vg, swapchain.displaySize_.width / pixrate, swapchain.displaySize_.height / pixrate, pixrate);
  renderDemo(exampleData.vg, 0, 0, (float)swapchain.displaySize_.width / pixrate, (float)swapchain.displaySize_.height / pixrate, (float)st, 0, &exampleData.data);
  renderGraph(exampleData.vg, 5, 5, &exampleData.fps);

  nvgEndFrame(exampleData.vg);

  vkCmdEndRenderPass(cmd_buffer);
  vkEndCommandBuffer(cmd_buffer);
}

bool ExampleDrawFrame(void) {

  uint32_t nextIndex;
  // Get the framebuffer index we should draw in
  VK_CHECK_RESULT(vkAcquireNextImageKHR(device.device_, swapchain.swapchain_,
                                        UINT64_MAX, render.semaphore_,
                                        VK_NULL_HANDLE, &nextIndex));
  VK_CHECK_RESULT(vkResetFences(device.device_, 1, &render.fence_));

  ExampleBuildCmdBudder(render.cmdBuffer_[0], nextIndex);

  VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo submit_info = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .pNext = nullptr,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &render.semaphore_,
      .pWaitDstStageMask = &waitStageMask,
      .commandBufferCount = 1,
      .pCommandBuffers = &render.cmdBuffer_[0],
      .signalSemaphoreCount = 0,
      .pSignalSemaphores = nullptr};
  VK_CHECK_RESULT(vkQueueSubmit(device.queue_, 1, &submit_info, render.fence_));
  VkResult res;
  do {
    res = vkWaitForFences(device.device_, 1, &render.fence_, VK_TRUE, 100000000);
  } while (res == VK_TIMEOUT);

  VkResult result;
  VkPresentInfoKHR presentInfo{
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .pNext = nullptr,
      .swapchainCount = 1,
      .pSwapchains = &swapchain.swapchain_,
      .pImageIndices = &nextIndex,
      .waitSemaphoreCount = 0,
      .pWaitSemaphores = nullptr,
      .pResults = &result,
  };
  vkQueuePresentKHR(device.queue_, &presentInfo);
  return true;
}