#pragma once

#include <string.h>
#include "nanovg.h"

enum NVGcreateFlags {
  // Flag indicating if geometry based anti-aliasing is used (may not be needed when using MSAA).
  NVG_ANTIALIAS = 1 << 0,
  // Flag indicating if strokes should be drawn using stencil buffer. The rendering will be a little
  // slower, but path overlaps (i.e. self-intersecting or sharp turns) will be drawn just once.
  NVG_STENCIL_STROKES = 1 << 1,
  // Flag indicating that additional debug checks are done.
  NVG_DEBUG = 1 << 2,
};

typedef struct VKNVGCreateInfo {
  VkDevice device;
  VkRenderPass renderpass;
  VkCommandBuffer cmd_buffer;
  VkPhysicalDeviceProperties physical_device_properties;
  VkPhysicalDeviceMemoryProperties memory_properties;

  const VkAllocationCallbacks *allocator; //Allocator for vulkan. can be null
} VKNVGCreateInfo;
#ifdef __cplusplus
extern "C" {
#endif
NVGcontext *nvgCreateVk(VKNVGCreateInfo create_info, int flags);
void nvgDeleteVk(NVGcontext *ctx);

void nvgvkSetCommandBufferStore(struct NVGcontext *ctx, VkCommandBuffer cmd_buffer);

#ifdef __cplusplus
}
#endif

#ifdef NANOVG_VULKAN_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#if !defined(__cplusplus) || defined(NANOVG_VK_NO_nullptrPTR)
#define nullptr NULL
#endif

#define NVGVK_CHECK_RESULT(f)    \
  {                              \
    VkResult res = (f);          \
    if (res != VK_SUCCESS) {     \
      assert(res == VK_SUCCESS); \
    }                            \
  }

enum VKNVGshaderType {
  NSVG_SHADER_FILLGRAD,
  NSVG_SHADER_FILLIMG,
  NSVG_SHADER_SIMPLE,
  NSVG_SHADER_IMG
};

typedef struct VKNVGtexture {
  VkSampler sampler;

  VkImage image;
  VkImageLayout imageLayout;
  VkImageView view;

  VkDeviceMemory mem;
  int32_t width, height;
  int type; //enum NVGtexture
  int flags;
} VKNVGtexture;

enum VKNVGcallType {
  VKNVG_NONE = 0,
  VKNVG_FILL,
  VKNVG_CONVEXFILL,
  VKNVG_STROKE,
  VKNVG_TRIANGLES,
};

typedef struct VKNVGcall {
  int type;
  int image;
  int pathOffset;
  int pathCount;
  int triangleOffset;
  int triangleCount;
  int uniformOffset;
  NVGcompositeOperationState compositOperation;
} VKNVGcall;

typedef struct VKNVGpath {
  int fillOffset;
  int fillCount;
  int strokeOffset;
  int strokeCount;
} VKNVGpath;

typedef struct VKNVGfragUniforms {
  float scissorMat[12]; // matrices are actually 3 vec4s
  float paintMat[12];
  struct NVGcolor innerCol;
  struct NVGcolor outerCol;
  float scissorExt[2];
  float scissorScale[2];
  float extent[2];
  float radius;
  float feather;
  float strokeMult;
  float strokeThr;
  int texType;
  int type;
} VKNVGfragUniforms;

typedef struct VKNVGBuffer {
  VkBuffer buffer;
  VkDeviceMemory mem;
  VkDeviceSize size;
} VKNVGBuffer;

enum VKNVGstencilType {
  VKNVG_STENCIL_NONE = 0,
  VKNVG_STENCIL_FILL,
};
typedef struct VKNVGCreatePipelineKey {
  bool stencil_fill;
  bool stencil_test;
  bool edge_aa;
  bool edge_aa_shader;
  VkPrimitiveTopology topology;
  NVGcompositeOperationState compositOperation;
} VKNVGCreatePipelineKey;

typedef struct VKNVGPipeline {
  VKNVGCreatePipelineKey create_key;
  VkPipeline pipeline;
} VKNVGPipeline;

typedef struct VKNVGDepthSimplePipeline {
  VkPipeline pipeline;
  VkDescriptorSetLayout desc_layout;
  VkPipelineLayout pipeline_layout;
} VKNVGDepthSimplePipeline;

typedef struct VKNVGcontext {
  VKNVGCreateInfo create_info;

  VkCommandBuffer cmd_buffer;

  int fragSize;
  int flags;

  //own resources
  VKNVGtexture *textures;
  int ntextures;
  int ctextures;

  VkDescriptorSetLayout desc_layout;
  VkPipelineLayout pipeline_layout;

  VKNVGPipeline *pipelines;
  int cpipelines;
  int npipelines;

  float view[2];

  // Per frame buffers
  VKNVGcall *calls;
  int ccalls;
  int ncalls;
  VKNVGpath *paths;
  int cpaths;
  int npaths;
  struct NVGvertex *verts;
  int cverts;
  int nverts;

  VkDescriptorPool desc_pool;
  int cdesc_pool;

  unsigned char *uniforms;
  int cuniforms;
  int nuniforms;
  VKNVGBuffer vertex_buffer;
  VKNVGBuffer vert_uniform_buffer;
  VKNVGBuffer frag_uniform_buffer;
  VKNVGPipeline *current_pipeline;

  VkShaderModule fill_frag_shader;
  VkShaderModule fill_frag_shader_aa;
  VkShaderModule fill_vert_shader;

  VKNVGDepthSimplePipeline depth_simple_pipeline;
} VKNVGcontext;

static int vknvg_maxi(int a, int b) { return a > b ? a : b; }

static void vknvg_xformToMat3x4(float *m3, float *t) {
  m3[0] = t[0];
  m3[1] = t[1];
  m3[2] = 0.0f;
  m3[3] = 0.0f;
  m3[4] = t[2];
  m3[5] = t[3];
  m3[6] = 0.0f;
  m3[7] = 0.0f;
  m3[8] = t[4];
  m3[9] = t[5];
  m3[10] = 1.0f;
  m3[11] = 0.0f;
}

static NVGcolor vknvg_premulColor(NVGcolor c) {
  c.r *= c.a;
  c.g *= c.a;
  c.b *= c.a;
  return c;
}

static VKNVGtexture *vknvg_findTexture(VKNVGcontext *vk, int id) {
  if (id > vk->ntextures || id <= 0) {
    return nullptr;
  }
  VKNVGtexture *tex = vk->textures + id - 1;
  return tex;
}
static VKNVGtexture *vknvg_allocTexture(VKNVGcontext *vk) {
  VKNVGtexture *tex = nullptr;
  int i;

  for (i = 0; i < vk->ntextures; i++) {
    if (vk->textures[i].image == VK_NULL_HANDLE) {
      tex = &vk->textures[i];
      break;
    }
  }
  if (tex == nullptr) {
    if (vk->ntextures + 1 > vk->ctextures) {
      VKNVGtexture *textures;
      int ctextures = vknvg_maxi(vk->ntextures + 1, 4) + vk->ctextures / 2; // 1.5x Overallocate
      textures = (VKNVGtexture *)realloc(vk->textures, sizeof(VKNVGtexture) * ctextures);
      if (textures == nullptr) {
        return nullptr;
      }
      vk->textures = textures;
      vk->ctextures = ctextures;
    }
    tex = &vk->textures[vk->ntextures++];
  }
  memset(tex, 0, sizeof(*tex));
  return tex;
}
static int vknvg_textureId(VKNVGcontext *vk, VKNVGtexture *tex) {
  ptrdiff_t id = tex - vk->textures;
  if (id < 0 || id > vk->ntextures) {
    return 0;
  }
  return (int)id + 1;
}
static int vknvg_deleteTexture(VKNVGcontext *vk, VKNVGtexture *tex) {
  VkDevice device = vk->create_info.device;
  const VkAllocationCallbacks *allocator = vk->create_info.allocator;
  if (tex) {
    if (tex->view != VK_NULL_HANDLE) {
      vkDestroyImageView(device, tex->view, allocator);
      tex->view = VK_NULL_HANDLE;
    }
    if (tex->sampler != VK_NULL_HANDLE) {
      vkDestroySampler(device, tex->sampler, allocator);
      tex->sampler = VK_NULL_HANDLE;
    }
    if (tex->image != VK_NULL_HANDLE) {
      vkDestroyImage(device, tex->image, allocator);
      tex->image = VK_NULL_HANDLE;
    }
    if (tex->mem != VK_NULL_HANDLE) {
      vkFreeMemory(device, tex->mem, allocator);
      tex->mem = VK_NULL_HANDLE;
    }
    return 1;
  }
  return 0;
}

static VKNVGPipeline *vknvg_allocPipeline(VKNVGcontext *vk) {
  VKNVGPipeline *ret = nullptr;
  if (vk->npipelines + 1 > vk->cpipelines) {
    VKNVGPipeline *pipelines;
    int cpipelines = vknvg_maxi(vk->npipelines + 1, 128) + vk->cpipelines / 2; // 1.5x Overallocate
    pipelines = (VKNVGPipeline *)realloc(vk->pipelines, sizeof(VKNVGPipeline) * cpipelines);
    if (pipelines == nullptr)
      return nullptr;
    vk->pipelines = pipelines;
    vk->cpipelines = cpipelines;
  }
  ret = &vk->pipelines[vk->npipelines++];
  memset(ret, 0, sizeof(VKNVGPipeline));
  return ret;
}
static int vknvg_compareCreatePipelineKey(const VKNVGCreatePipelineKey *a, const VKNVGCreatePipelineKey *b) {
  if (a->topology != b->topology) {
    return a->topology - b->topology;
  }
  if (a->stencil_fill != b->stencil_fill) {
    return a->stencil_fill - b->stencil_fill;
  }

  if (a->stencil_test != b->stencil_test) {
    return a->stencil_test - b->stencil_test;
  }
  if (a->edge_aa != b->edge_aa) {
	  return a->edge_aa - b->edge_aa;
  }
  if (a->edge_aa_shader != b->edge_aa_shader) {
    return a->edge_aa_shader - b->edge_aa_shader;
  }

  if (a->compositOperation.srcRGB != b->compositOperation.srcRGB) {
    return a->compositOperation.srcRGB - b->compositOperation.srcRGB;
  }
  if (a->compositOperation.srcAlpha != b->compositOperation.srcAlpha) {
    return a->compositOperation.srcAlpha - b->compositOperation.srcAlpha;
  }
  if (a->compositOperation.dstRGB != b->compositOperation.dstRGB) {
    return a->compositOperation.dstRGB - b->compositOperation.dstRGB;
  }
  if (a->compositOperation.dstAlpha != b->compositOperation.dstAlpha) {
    return a->compositOperation.dstAlpha - b->compositOperation.dstAlpha;
  }
  return 0;
}

static VKNVGPipeline *vknvg_findPipeline(VKNVGcontext *vk, VKNVGCreatePipelineKey *pipelinekey) {
  VKNVGPipeline *pipeline = nullptr;
  for (int i = 0; i < vk->npipelines; i++) {
    if (vknvg_compareCreatePipelineKey(&vk->pipelines[i].create_key, pipelinekey) == 0) {
      pipeline = &vk->pipelines[i];
      break;
    }
  }
  return pipeline;
}

static VkResult vknvg_memory_type_from_properties(VkPhysicalDeviceMemoryProperties memory_properties, uint32_t typeBits, VkFlags requirements_mask, uint32_t *typeIndex) {
  // Search memtypes to find first index with those properties
  for (uint32_t i = 0; i < memory_properties.memoryTypeCount; i++) {
    if ((typeBits & 1) == 1) {
      // Type is available, does it match user properties?
      if ((memory_properties.memoryTypes[i].propertyFlags & requirements_mask) == requirements_mask) {
        *typeIndex = i;
        return VK_SUCCESS;
      }
    }
    typeBits >>= 1;
  }
  // No memory types matched, return failure
  return VK_ERROR_FORMAT_NOT_SUPPORTED;
}

static int vknvg_convertPaint(VKNVGcontext *vk, VKNVGfragUniforms *frag, NVGpaint *paint,
                              NVGscissor *scissor, float width, float fringe, float strokeThr) {
  VKNVGtexture *tex = nullptr;
  float invxform[6];

  memset(frag, 0, sizeof(*frag));

  frag->innerCol = vknvg_premulColor(paint->innerColor);
  frag->outerCol = vknvg_premulColor(paint->outerColor);

  if (scissor->extent[0] < -0.5f || scissor->extent[1] < -0.5f) {
    memset(frag->scissorMat, 0, sizeof(frag->scissorMat));
    frag->scissorExt[0] = 1.0f;
    frag->scissorExt[1] = 1.0f;
    frag->scissorScale[0] = 1.0f;
    frag->scissorScale[1] = 1.0f;
  } else {
    nvgTransformInverse(invxform, scissor->xform);
    vknvg_xformToMat3x4(frag->scissorMat, invxform);
    frag->scissorExt[0] = scissor->extent[0];
    frag->scissorExt[1] = scissor->extent[1];
    frag->scissorScale[0] = sqrtf(scissor->xform[0] * scissor->xform[0] + scissor->xform[2] * scissor->xform[2]) / fringe;
    frag->scissorScale[1] = sqrtf(scissor->xform[1] * scissor->xform[1] + scissor->xform[3] * scissor->xform[3]) / fringe;
  }

  memcpy(frag->extent, paint->extent, sizeof(frag->extent));
  frag->strokeMult = (width * 0.5f + fringe * 0.5f) / fringe;
  frag->strokeThr = strokeThr;

  if (paint->image != 0) {
    tex = vknvg_findTexture(vk, paint->image);
    if (tex == nullptr)
      return 0;
    if ((tex->flags & NVG_IMAGE_FLIPY) != 0) {
      float m1[6], m2[6];
      nvgTransformTranslate(m1, 0.0f, frag->extent[1] * 0.5f);
      nvgTransformMultiply(m1, paint->xform);
      nvgTransformScale(m2, 1.0f, -1.0f);
      nvgTransformMultiply(m2, m1);
      nvgTransformTranslate(m1, 0.0f, -frag->extent[1] * 0.5f);
      nvgTransformMultiply(m1, m2);
      nvgTransformInverse(invxform, m1);
    } else {
      nvgTransformInverse(invxform, paint->xform);
    }
    frag->type = NSVG_SHADER_FILLIMG;

    if (tex->type == NVG_TEXTURE_RGBA)
      frag->texType = (tex->flags & NVG_IMAGE_PREMULTIPLIED) ? 0 : 1;
    else
      frag->texType = 2;
    //		printf("frag->texType = %d\n", frag->texType);
  } else {
    frag->type = NSVG_SHADER_FILLGRAD;
    frag->radius = paint->radius;
    frag->feather = paint->feather;
    nvgTransformInverse(invxform, paint->xform);
  }

  vknvg_xformToMat3x4(frag->paintMat, invxform);

  return 1;
}

static VKNVGBuffer vknvg_createBuffer(VkDevice device, VkPhysicalDeviceMemoryProperties memory_properties, const VkAllocationCallbacks *allocator, VkBufferUsageFlags usage, VkMemoryPropertyFlagBits memory_type, void *data, uint32_t size) {

  const VkBufferCreateInfo buf_create_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, size, usage};

  VkBuffer buffer;
  NVGVK_CHECK_RESULT(vkCreateBuffer(device, &buf_create_info, allocator, &buffer));
  VkMemoryRequirements mem_reqs = {0};
  vkGetBufferMemoryRequirements(device, buffer, &mem_reqs);

  uint32_t memoryTypeIndex;
  VkResult res = vknvg_memory_type_from_properties(memory_properties, mem_reqs.memoryTypeBits, memory_type, &memoryTypeIndex);
  assert(res == VK_SUCCESS);
  VkMemoryAllocateInfo mem_alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, mem_reqs.size, memoryTypeIndex};

  VkDeviceMemory mem;
  NVGVK_CHECK_RESULT(vkAllocateMemory(device, &mem_alloc, nullptr, &mem));

  void *mapped;
  NVGVK_CHECK_RESULT(vkMapMemory(device, mem, 0, mem_alloc.allocationSize, 0, &mapped));
  memcpy(mapped, data, size);
  vkUnmapMemory(device, mem);
  NVGVK_CHECK_RESULT(vkBindBufferMemory(device, buffer, mem, 0));
  VKNVGBuffer buf = {buffer, mem,mem_alloc.allocationSize};
  return buf;
}

static void vknvg_destroyBuffer(VkDevice device, const VkAllocationCallbacks *allocator, VKNVGBuffer *buffer) {

  vkDestroyBuffer(device, buffer->buffer, allocator);
  vkFreeMemory(device, buffer->mem, allocator);
}

static void vknvg_UpdateBuffer(VkDevice device, const VkAllocationCallbacks *allocator, VKNVGBuffer *buffer, VkPhysicalDeviceMemoryProperties memory_properties,  VkBufferUsageFlags usage, VkMemoryPropertyFlagBits memory_type, void *data, uint32_t size) {

	if (buffer->size < size) {
		vknvg_destroyBuffer(device, allocator, buffer);
		*buffer = vknvg_createBuffer(device,memory_properties,allocator,usage,memory_type,data,size);
	}
	else {
		void *mapped;
		NVGVK_CHECK_RESULT(vkMapMemory(device, buffer->mem, 0, size, 0, &mapped));
		memcpy(mapped, data, size);
		vkUnmapMemory(device, buffer->mem);
	}

}


static VkShaderModule vknvg_createShaderModule(VkDevice device, const void *code, size_t size, const VkAllocationCallbacks *allocator) {

  VkShaderModuleCreateInfo moduleCreateInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, size, (const uint32_t *)code};
  VkShaderModule module;
  NVGVK_CHECK_RESULT(vkCreateShaderModule(device, &moduleCreateInfo, allocator, &module));
  return module;
}
static VkBlendFactor vknvg_NVGblendFactorToVkBlendFactor(enum NVGblendFactor factor) {
  switch (factor) {
  case NVG_ZERO:
    return VK_BLEND_FACTOR_ZERO;
  case NVG_ONE:
    return VK_BLEND_FACTOR_ONE;
  case NVG_SRC_COLOR:
    return VK_BLEND_FACTOR_SRC_COLOR;
  case NVG_ONE_MINUS_SRC_COLOR:
    return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
  case NVG_DST_COLOR:
    return VK_BLEND_FACTOR_DST_COLOR;
  case NVG_ONE_MINUS_DST_COLOR:
    return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
  case NVG_SRC_ALPHA:
    return VK_BLEND_FACTOR_SRC_ALPHA;
  case NVG_ONE_MINUS_SRC_ALPHA:
    return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  case NVG_DST_ALPHA:
    return VK_BLEND_FACTOR_DST_ALPHA;
  case NVG_ONE_MINUS_DST_ALPHA:
    return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
  case NVG_SRC_ALPHA_SATURATE:
    return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
  default:
    return VK_BLEND_FACTOR_MAX_ENUM;
  }
}

static VkPipelineColorBlendAttachmentState vknvg_compositOperationToColorBlendAttachmentState(NVGcompositeOperationState compositeOperation) {
  VkPipelineColorBlendAttachmentState state = {0};
  state.blendEnable = VK_TRUE;
  state.colorBlendOp = VK_BLEND_OP_ADD;
  state.alphaBlendOp = VK_BLEND_OP_ADD;
  state.colorWriteMask = 0xf;

  state.srcColorBlendFactor = vknvg_NVGblendFactorToVkBlendFactor((enum NVGblendFactor)compositeOperation.srcRGB);
  state.srcAlphaBlendFactor = vknvg_NVGblendFactorToVkBlendFactor((enum NVGblendFactor)compositeOperation.srcAlpha);
  state.dstColorBlendFactor = vknvg_NVGblendFactorToVkBlendFactor((enum NVGblendFactor)compositeOperation.dstRGB);
  state.dstAlphaBlendFactor = vknvg_NVGblendFactorToVkBlendFactor((enum NVGblendFactor)compositeOperation.dstAlpha);

  if (state.srcColorBlendFactor == VK_BLEND_FACTOR_MAX_ENUM ||
      state.srcAlphaBlendFactor == VK_BLEND_FACTOR_MAX_ENUM ||
      state.dstColorBlendFactor == VK_BLEND_FACTOR_MAX_ENUM ||
      state.dstAlphaBlendFactor == VK_BLEND_FACTOR_MAX_ENUM) {
    //default blend if failed convert
    state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  }

  return state;
}

static VkDescriptorSetLayout vknvg_createDescriptorSetLayout(VkDevice device, const VkAllocationCallbacks *allocator) {
  const VkDescriptorSetLayoutBinding layout_binding[3] = {
      {
          0,
          VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          1,
          VK_SHADER_STAGE_VERTEX_BIT,
          nullptr,
      },
      {
          1,
          VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          1,
          VK_SHADER_STAGE_FRAGMENT_BIT,
          nullptr,
      },
      {
          2,
          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          1,
          VK_SHADER_STAGE_FRAGMENT_BIT,
          nullptr,
      }};
  const VkDescriptorSetLayoutCreateInfo descriptor_layout = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 3, layout_binding};

  VkDescriptorSetLayout desc_layout;
  NVGVK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptor_layout, allocator, &desc_layout));

  return desc_layout;
}

static VkDescriptorPool vknvg_createDescriptorPool(VkDevice device, uint32_t count, const VkAllocationCallbacks *allocator) {

  const VkDescriptorPoolSize type_count[3] = {
      {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 2 * count},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4 * count},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * count},
  };
  const VkDescriptorPoolCreateInfo descriptor_pool = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, count * 2, 3, type_count};
  VkDescriptorPool desc_pool;
  NVGVK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptor_pool, allocator, &desc_pool));
  return desc_pool;
}
static VkPipelineLayout vknvg_createPipelineLayout(VkDevice device, VkDescriptorSetLayout desc_layout, const VkAllocationCallbacks *allocator) {
  VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pipelineLayoutCreateInfo.setLayoutCount = 1;
  pipelineLayoutCreateInfo.pSetLayouts = &desc_layout;

  VkPipelineLayout pipeline_layout;

  NVGVK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, allocator,
                                            &pipeline_layout));

  return pipeline_layout;
}

static VkPipelineDepthStencilStateCreateInfo initializeDepthStencilCreateInfo(VKNVGCreatePipelineKey *pipelinekey) {

  VkPipelineDepthStencilStateCreateInfo ds = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  ds.depthWriteEnable = VK_FALSE;
  ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  ds.depthBoundsTestEnable = VK_FALSE;
  ds.stencilTestEnable = VK_FALSE;
  ds.back.failOp = VK_STENCIL_OP_KEEP;
  ds.back.passOp = VK_STENCIL_OP_KEEP;
  ds.back.compareOp = VK_COMPARE_OP_ALWAYS;
  if (pipelinekey->stencil_fill) {
    ds.stencilTestEnable = VK_TRUE;
    ds.front.compareOp = VK_COMPARE_OP_ALWAYS;
    ds.front.failOp = VK_STENCIL_OP_KEEP;
    ds.front.depthFailOp = VK_STENCIL_OP_KEEP;
    ds.front.passOp = VK_STENCIL_OP_INCREMENT_AND_WRAP;
    ds.front.reference = 0x0;
    ds.front.writeMask = 0xff;
    ds.back = ds.front;
    ds.back.passOp = VK_STENCIL_OP_DECREMENT_AND_WRAP;
  } else if (pipelinekey->stencil_test) {
    ds.stencilTestEnable = VK_TRUE;
    if (pipelinekey->edge_aa) {
      ds.front.compareOp = VK_COMPARE_OP_EQUAL;
      ds.front.reference = 0x0;
      ds.front.compareMask = 0xff;
      ds.front.writeMask = 0xff;
      ds.front.failOp = VK_STENCIL_OP_KEEP;
      ds.front.depthFailOp = VK_STENCIL_OP_KEEP;
      ds.front.passOp = VK_STENCIL_OP_KEEP;
      ds.back = ds.front;
    } else {
      ds.front.compareOp = VK_COMPARE_OP_NOT_EQUAL;
      ds.front.reference = 0x0;
      ds.front.compareMask = 0xff;
      ds.front.writeMask = 0xff;
      ds.front.failOp = VK_STENCIL_OP_ZERO;
      ds.front.depthFailOp = VK_STENCIL_OP_ZERO;
      ds.front.passOp = VK_STENCIL_OP_ZERO;
      ds.back = ds.front;
    }
  }

  return ds;
}
static VKNVGPipeline *vknvg_createPipeline(VKNVGcontext *vk, VKNVGCreatePipelineKey *pipelinekey) {

  VkDevice device = vk->create_info.device;
  VkPipelineLayout pipeline_layout = vk->pipeline_layout;
  VkRenderPass renderpass = vk->create_info.renderpass;
  const VkAllocationCallbacks *allocator = vk->create_info.allocator;

  VkDescriptorSetLayout desc_layout = vk->desc_layout;
  VkShaderModule vert_shader = vk->fill_vert_shader;
  VkShaderModule frag_shader = vk->fill_frag_shader;
  VkShaderModule frag_shader_aa = vk->fill_frag_shader_aa;

  VkVertexInputBindingDescription vi_bindings[1] = {{0}};
  vi_bindings[0].binding = 0;
  vi_bindings[0].stride = sizeof(NVGvertex);
  vi_bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  VkVertexInputAttributeDescription vi_attrs[2] = {
      {0},
  };
  vi_attrs[0].binding = 0;
  vi_attrs[0].location = 0;
  vi_attrs[0].format = VK_FORMAT_R32G32_SFLOAT;
  vi_attrs[0].offset = 0;
  vi_attrs[1].binding = 0;
  vi_attrs[1].location = 1;
  vi_attrs[1].format = VK_FORMAT_R32G32_SFLOAT;
  vi_attrs[1].offset = (2 * sizeof(float));

  VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vi.vertexBindingDescriptionCount = 1;
  vi.pVertexBindingDescriptions = vi_bindings;
  vi.vertexAttributeDescriptionCount = 2;
  vi.pVertexAttributeDescriptions = vi_attrs;

  VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ia.topology = pipelinekey->topology;

  VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_BACK_BIT;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.depthClampEnable = VK_FALSE;
  rs.rasterizerDiscardEnable = VK_FALSE;
  rs.depthBiasEnable = VK_FALSE;
  rs.lineWidth = 1.0f;

  VkPipelineColorBlendAttachmentState colorblend = vknvg_compositOperationToColorBlendAttachmentState(pipelinekey->compositOperation);

  if (pipelinekey->stencil_fill) {
    rs.cullMode = VK_CULL_MODE_NONE;
    colorblend.colorWriteMask = 0;
  }

  VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  cb.attachmentCount = 1;
  cb.pAttachments = &colorblend;

  VkPipelineViewportStateCreateInfo vp = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vp.viewportCount = 1;
  vp.scissorCount = 1;

  VkDynamicState dynamicStateEnables[VK_DYNAMIC_STATE_RANGE_SIZE] = {
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR};

  VkPipelineDynamicStateCreateInfo dynamicState = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamicState.dynamicStateCount = 2;
  dynamicState.pDynamicStates = dynamicStateEnables;

  VkPipelineDepthStencilStateCreateInfo ds = initializeDepthStencilCreateInfo(pipelinekey);

  VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  ms.pSampleMask = nullptr;
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineShaderStageCreateInfo shaderStages[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
  shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  shaderStages[0].module = vert_shader;
  shaderStages[0].pName = "main";

  shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  shaderStages[1].module = frag_shader;
  shaderStages[1].pName = "main";
  if (pipelinekey->edge_aa_shader) {
    shaderStages[1].module = frag_shader_aa;
  }

  VkGraphicsPipelineCreateInfo pipelineCreateInfo = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  pipelineCreateInfo.layout = pipeline_layout;
  pipelineCreateInfo.stageCount = 2;
  pipelineCreateInfo.pStages = shaderStages;
  pipelineCreateInfo.pVertexInputState = &vi;
  pipelineCreateInfo.pInputAssemblyState = &ia;
  pipelineCreateInfo.pRasterizationState = &rs;
  pipelineCreateInfo.pColorBlendState = &cb;
  pipelineCreateInfo.pMultisampleState = &ms;
  pipelineCreateInfo.pViewportState = &vp;
  pipelineCreateInfo.pDepthStencilState = &ds;
  pipelineCreateInfo.renderPass = renderpass;
  pipelineCreateInfo.pDynamicState = &dynamicState;

  VkPipeline pipeline;
  NVGVK_CHECK_RESULT(vkCreateGraphicsPipelines(device, 0, 1, &pipelineCreateInfo, allocator, &pipeline));

  VKNVGPipeline *ret = vknvg_allocPipeline(vk);

  ret->create_key = *pipelinekey;
  ret->pipeline = pipeline;
  return ret;
}

static VkPipeline vknvg_bindPipeline(VKNVGcontext *vk, VkCommandBuffer cmd_buffer, VKNVGCreatePipelineKey *pipelinekey) {
  VKNVGPipeline *pipeline = vknvg_findPipeline(vk, pipelinekey);
  if (!pipeline) {
    pipeline = vknvg_createPipeline(vk, pipelinekey);
  }
  if (pipeline != vk->current_pipeline) {
    vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
    vk->current_pipeline = pipeline;
  }
  return pipeline->pipeline;
}

static int vknvg_UpdateTexture(VkDevice device, VKNVGtexture *tex, int dx, int dy, int w, int h, const unsigned char *data) {

  VkMemoryRequirements mem_reqs;
  vkGetImageMemoryRequirements(device, tex->image, &mem_reqs);
  VkImageSubresource subres = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
  VkSubresourceLayout layout;
  void *bindptr;
  /* Get the subresource layout so we know what the row pitch is */
  vkGetImageSubresourceLayout(device, tex->image, &subres, &layout);
  NVGVK_CHECK_RESULT(vkMapMemory(device, tex->mem, 0, mem_reqs.size, 0, &bindptr));
  int comp_size = (tex->type == NVG_TEXTURE_RGBA) ? 4 : 1;
  for (int y = 0; y < h; ++y) {
    char *src = (char *)data + ((dy + y) * (tex->width * comp_size)) + dx;
    char *dest = (char *)bindptr + ((dy + y) * layout.rowPitch) + dx;
    memcpy(dest, src, w * comp_size);
  }
  vkUnmapMemory(device, tex->mem);
  return 1;
}

static int vknvg_maxVertCount(const NVGpath *paths, int npaths) {
  int i, count = 0;
  for (i = 0; i < npaths; i++) {
    count += paths[i].nfill;
    count += paths[i].nstroke;
  }
  return count;
}

static VKNVGcall *vknvg_allocCall(VKNVGcontext *vk) {
  VKNVGcall *ret = nullptr;
  if (vk->ncalls + 1 > vk->ccalls) {
    VKNVGcall *calls;
    int ccalls = vknvg_maxi(vk->ncalls + 1, 128) + vk->ccalls / 2; // 1.5x Overallocate
    calls = (VKNVGcall *)realloc(vk->calls, sizeof(VKNVGcall) * ccalls);
    if (calls == nullptr)
      return nullptr;
    vk->calls = calls;
    vk->ccalls = ccalls;
  }
  ret = &vk->calls[vk->ncalls++];
  memset(ret, 0, sizeof(VKNVGcall));
  return ret;
}

static int vknvg_allocPaths(VKNVGcontext *vk, int n) {
  int ret = 0;
  if (vk->npaths + n > vk->cpaths) {
    VKNVGpath *paths;
    int cpaths = vknvg_maxi(vk->npaths + n, 128) + vk->cpaths / 2; // 1.5x Overallocate
    paths = (VKNVGpath *)realloc(vk->paths, sizeof(VKNVGpath) * cpaths);
    if (paths == nullptr)
      return -1;
    vk->paths = paths;
    vk->cpaths = cpaths;
  }
  ret = vk->npaths;
  vk->npaths += n;
  return ret;
}

static int vknvg_allocVerts(VKNVGcontext *vk, int n) {
  int ret = 0;
  if (vk->nverts + n > vk->cverts) {
    NVGvertex *verts;
    int cverts = vknvg_maxi(vk->nverts + n, 4096) + vk->cverts / 2; // 1.5x Overallocate
    verts = (NVGvertex *)realloc(vk->verts, sizeof(NVGvertex) * cverts);
    if (verts == nullptr)
      return -1;
    vk->verts = verts;
    vk->cverts = cverts;
  }
  ret = vk->nverts;
  vk->nverts += n;
  return ret;
}

static int vknvg_allocFragUniforms(VKNVGcontext *vk, int n) {
  int ret = 0, structSize = vk->fragSize;
  if (vk->nuniforms + n > vk->cuniforms) {
    unsigned char *uniforms;
    int cuniforms = vknvg_maxi(vk->nuniforms + n, 128) + vk->cuniforms / 2; // 1.5x Overallocate
    uniforms = (unsigned char *)realloc(vk->uniforms, structSize * cuniforms);
    if (uniforms == nullptr)
      return -1;
    vk->uniforms = uniforms;
    vk->cuniforms = cuniforms;
  }
  ret = vk->nuniforms * structSize;
  vk->nuniforms += n;
  return ret;
}
static VKNVGfragUniforms *vknvg_fragUniformPtr(VKNVGcontext *vk, int i) {
  return (VKNVGfragUniforms *)&vk->uniforms[i];
}

static void vknvg_vset(NVGvertex *vtx, float x, float y, float u, float v) {
  vtx->x = x;
  vtx->y = y;
  vtx->u = u;
  vtx->v = v;
}

static void vknvg_setUniforms(VKNVGcontext *vk, VkDescriptorSet desc_set, int uniformOffset, int image) {
  VkDevice device = vk->create_info.device;

  VkWriteDescriptorSet writes[3] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};

  VkDescriptorBufferInfo vert_uniform_buffer_info = {0};
  vert_uniform_buffer_info.buffer = vk->vert_uniform_buffer.buffer;
  vert_uniform_buffer_info.offset = 0;
  vert_uniform_buffer_info.range = sizeof(vk->view);

  writes[0].dstSet = desc_set;
  writes[0].descriptorCount = 1;
  writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  writes[0].pBufferInfo = &vert_uniform_buffer_info;
  writes[0].dstArrayElement = 0;
  writes[0].dstBinding = 0;

  VkDescriptorBufferInfo uniform_buffer_info = {0};
  uniform_buffer_info.buffer = vk->frag_uniform_buffer.buffer;
  uniform_buffer_info.offset = uniformOffset;
  uniform_buffer_info.range = sizeof(VKNVGfragUniforms);

  writes[1].dstSet = desc_set;
  writes[1].descriptorCount = 1;
  writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  writes[1].pBufferInfo = &uniform_buffer_info;
  writes[1].dstBinding = 1;

  VkDescriptorImageInfo image_info;
  if (image != 0) {
    VKNVGtexture *tex = vknvg_findTexture(vk, image);

    image_info.imageLayout = tex->imageLayout;
    image_info.imageView = tex->view;
    image_info.sampler = tex->sampler;

    writes[2].dstSet = desc_set;
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].pImageInfo = &image_info;
  } else {
    //fixme
    VKNVGtexture *tex = vknvg_findTexture(vk, 1);
    image_info.imageLayout = tex->imageLayout;
    image_info.imageView = tex->view;
    image_info.sampler = tex->sampler;

    writes[2].dstSet = desc_set;
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].pImageInfo = &image_info;
  }

  vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
}

static void vknvg_fill(VKNVGcontext *vk, VKNVGcall *call) {
  VKNVGpath *paths = &vk->paths[call->pathOffset];
  int i, npaths = call->pathCount;

  VkDevice device = vk->create_info.device;
  VkCommandBuffer cmd_buffer = vk->cmd_buffer;

  VKNVGCreatePipelineKey pipelinekey = {0};
  pipelinekey.compositOperation = call->compositOperation;
  pipelinekey.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
  pipelinekey.stencil_fill = true;
  pipelinekey.edge_aa_shader = vk->flags & NVG_ANTIALIAS;

  vknvg_bindPipeline(vk, cmd_buffer, &pipelinekey);

  VkDescriptorSetAllocateInfo alloc_info[1] = {
      {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, vk->desc_pool, 1, &vk->desc_layout},
  };
  VkDescriptorSet desc_set;
  NVGVK_CHECK_RESULT(vkAllocateDescriptorSets(device, alloc_info, &desc_set));
  vknvg_setUniforms(vk, desc_set, call->uniformOffset, call->image); //fixme
  vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->pipeline_layout, 0, 1, &desc_set, 0, nullptr);

  for (i = 0; i < npaths; i++) {
    const VkDeviceSize offsets[1] = {paths[i].fillOffset * sizeof(NVGvertex)};
    vkCmdBindVertexBuffers(cmd_buffer, 0, 1, &vk->vertex_buffer.buffer, offsets);
    vkCmdDraw(cmd_buffer, paths[i].fillCount, 1, 0, 0);
  }

  VkDescriptorSet desc_set2;
  NVGVK_CHECK_RESULT(vkAllocateDescriptorSets(device, alloc_info, &desc_set2));
  vknvg_setUniforms(vk, desc_set2, call->uniformOffset + vk->fragSize, call->image);
  vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->pipeline_layout, 0, 1, &desc_set2, 0, nullptr);

  if (vk->flags & NVG_ANTIALIAS) {

    pipelinekey.compositOperation = call->compositOperation;
    pipelinekey.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    pipelinekey.stencil_fill = false;
    pipelinekey.stencil_test = true;
    pipelinekey.edge_aa = true;
    vknvg_bindPipeline(vk, cmd_buffer, &pipelinekey);
    // Draw fringes
    for (int i = 0; i < npaths; ++i) {
      const VkDeviceSize offsets[1] = {paths[i].strokeOffset * sizeof(NVGvertex)};
      vkCmdBindVertexBuffers(cmd_buffer, 0, 1, &vk->vertex_buffer.buffer, offsets);
      vkCmdDraw(cmd_buffer, paths[i].strokeCount, 1, 0, 0);
    }
  }

  pipelinekey.compositOperation = call->compositOperation;
  pipelinekey.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  pipelinekey.stencil_fill = false;
  pipelinekey.stencil_test = true;
  pipelinekey.edge_aa = false;
  vknvg_bindPipeline(vk, cmd_buffer, &pipelinekey);

  const VkDeviceSize offsets[1] = {call->triangleOffset * sizeof(NVGvertex)};
  vkCmdBindVertexBuffers(cmd_buffer, 0, 1, &vk->vertex_buffer.buffer, offsets);
  vkCmdDraw(cmd_buffer, call->triangleCount, 1, 0, 0);
}

static void vknvg_convexFill(VKNVGcontext *vk, VKNVGcall *call) {
  VKNVGpath *paths = &vk->paths[call->pathOffset];
  int npaths = call->pathCount;

  VkDevice device = vk->create_info.device;
  VkCommandBuffer cmd_buffer = vk->cmd_buffer;

  VKNVGCreatePipelineKey pipelinekey = {0};
  pipelinekey.compositOperation = call->compositOperation;
  pipelinekey.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
  pipelinekey.edge_aa_shader = vk->flags & NVG_ANTIALIAS;

  vknvg_bindPipeline(vk, cmd_buffer, &pipelinekey);

  VkDescriptorSetAllocateInfo alloc_info[1] = {
      {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, vk->desc_pool, 1, &vk->desc_layout},
  };
  VkDescriptorSet desc_set;
  NVGVK_CHECK_RESULT(vkAllocateDescriptorSets(device, alloc_info, &desc_set));
  vknvg_setUniforms(vk, desc_set, call->uniformOffset, call->image);

  vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->pipeline_layout, 0, 1, &desc_set, 0, nullptr);

  for (int i = 0; i < npaths; ++i) {
    const VkDeviceSize offsets[1] = {paths[i].fillOffset * sizeof(NVGvertex)};
    vkCmdBindVertexBuffers(cmd_buffer, 0, 1, &vk->vertex_buffer.buffer, offsets);
    vkCmdDraw(cmd_buffer, paths[i].fillCount, 1, 0, 0);
  }
  if (vk->flags & NVG_ANTIALIAS) {
    pipelinekey.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    vknvg_bindPipeline(vk, cmd_buffer, &pipelinekey);

    // Draw fringes
    for (int i = 0; i < npaths; ++i) {
      const VkDeviceSize offsets[1] = {paths[i].strokeOffset * sizeof(NVGvertex)};
      vkCmdBindVertexBuffers(cmd_buffer, 0, 1, &vk->vertex_buffer.buffer, offsets);
      vkCmdDraw(cmd_buffer, paths[i].strokeCount, 1, 0, 0);
    }
  }
}

static void vknvg_stroke(VKNVGcontext *vk, VKNVGcall *call) {
  VkDevice device = vk->create_info.device;
  VkCommandBuffer cmd_buffer = vk->cmd_buffer;

  VKNVGpath *paths = &vk->paths[call->pathOffset];
  int npaths = call->pathCount;

  if (vk->flags & NVG_STENCIL_STROKES) {

    VkDescriptorSetAllocateInfo alloc_info[1] = {
        {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, vk->desc_pool, 1, &vk->desc_layout},
    };
    VkDescriptorSet desc_set;
    NVGVK_CHECK_RESULT(vkAllocateDescriptorSets(device, alloc_info, &desc_set));
    vknvg_setUniforms(vk, desc_set, call->uniformOffset, call->image);
    vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->pipeline_layout, 0, 1, &desc_set, 0, nullptr);
    VKNVGCreatePipelineKey pipelinekey = {0};
    pipelinekey.compositOperation = call->compositOperation;
    pipelinekey.stencil_fill = false;
    pipelinekey.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	pipelinekey.edge_aa_shader = vk->flags & NVG_ANTIALIAS;
    vknvg_bindPipeline(vk, cmd_buffer, &pipelinekey);

    for (int i = 0; i < npaths; ++i) {
      const VkDeviceSize offsets[1] = {paths[i].strokeOffset * sizeof(NVGvertex)};
      vkCmdBindVertexBuffers(cmd_buffer, 0, 1, &vk->vertex_buffer.buffer, offsets);
      vkCmdDraw(cmd_buffer, paths[i].strokeCount, 1, 0, 0);
    }

    pipelinekey.stencil_fill = false;
    pipelinekey.stencil_test = true;
    pipelinekey.edge_aa = true;
    vknvg_bindPipeline(vk, cmd_buffer, &pipelinekey);
    for (int i = 0; i < npaths; ++i) {
      const VkDeviceSize offsets[1] = {paths[i].strokeOffset * sizeof(NVGvertex)};
      vkCmdBindVertexBuffers(cmd_buffer, 0, 1, &vk->vertex_buffer.buffer, offsets);
      vkCmdDraw(cmd_buffer, paths[i].strokeCount, 1, 0, 0);
    }

    pipelinekey.stencil_fill = true;
    pipelinekey.stencil_test = true;
    pipelinekey.edge_aa_shader = false;
	pipelinekey.edge_aa = false;
    for (int i = 0; i < npaths; ++i) {
      const VkDeviceSize offsets[1] = {paths[i].strokeOffset * sizeof(NVGvertex)};
      vkCmdBindVertexBuffers(cmd_buffer, 0, 1, &vk->vertex_buffer.buffer, offsets);
      vkCmdDraw(cmd_buffer, paths[i].strokeCount, 1, 0, 0);
    }
  } else {

    VKNVGCreatePipelineKey pipelinekey = {0};
    pipelinekey.compositOperation = call->compositOperation;
    pipelinekey.stencil_fill = false;
    pipelinekey.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	pipelinekey.edge_aa_shader = vk->flags & NVG_ANTIALIAS;

    vknvg_bindPipeline(vk, cmd_buffer, &pipelinekey);
    VkDescriptorSetAllocateInfo alloc_info[1] = {
        {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, vk->desc_pool, 1, &vk->desc_layout},
    };
    VkDescriptorSet desc_set;
    NVGVK_CHECK_RESULT(vkAllocateDescriptorSets(device, alloc_info, &desc_set));
    vknvg_setUniforms(vk, desc_set, call->uniformOffset, call->image);
    vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->pipeline_layout, 0, 1, &desc_set, 0, nullptr);
    // Draw Strokes

    for (int i = 0; i < npaths; ++i) {
      const VkDeviceSize offsets[1] = {paths[i].strokeOffset * sizeof(NVGvertex)};
      vkCmdBindVertexBuffers(cmd_buffer, 0, 1, &vk->vertex_buffer.buffer, offsets);
      vkCmdDraw(cmd_buffer, paths[i].strokeCount, 1, 0, 0);
    }
  }
}

static void vknvg_triangles(VKNVGcontext *vk, VKNVGcall *call) {
  if (call->triangleCount == 0) {
    return;
  }
  VkDevice device = vk->create_info.device;
  VkCommandBuffer cmd_buffer = vk->cmd_buffer;

  VKNVGCreatePipelineKey pipelinekey = {0};
  pipelinekey.compositOperation = call->compositOperation;
  pipelinekey.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  pipelinekey.stencil_fill = false;
  pipelinekey.edge_aa_shader = vk->flags & NVG_ANTIALIAS;

  vknvg_bindPipeline(vk, cmd_buffer, &pipelinekey);
  VkDescriptorSetAllocateInfo alloc_info[1] = {
      {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, vk->desc_pool, 1, &vk->desc_layout},
  };
  VkDescriptorSet desc_set;
  NVGVK_CHECK_RESULT(vkAllocateDescriptorSets(device, alloc_info, &desc_set));
  vknvg_setUniforms(vk, desc_set, call->uniformOffset, call->image);
  vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->pipeline_layout, 0, 1, &desc_set, 0, nullptr);

  const VkDeviceSize offsets[1] = {call->triangleOffset * sizeof(NVGvertex)};
  vkCmdBindVertexBuffers(cmd_buffer, 0, 1, &vk->vertex_buffer.buffer, offsets);

  vkCmdDraw(cmd_buffer, call->triangleCount, 1, 0, 0);
}
///==================================================================================================================
static int vknvg_renderCreate(void *uptr) {
  VKNVGcontext *vk = (VKNVGcontext *)uptr;
  VkDevice device = vk->create_info.device;
  VkRenderPass renderpass = vk->create_info.renderpass;
  const VkAllocationCallbacks *allocator = vk->create_info.allocator;

  static const unsigned char fill_vert_shader[] = {
#include "shader/fill_vert_shader_hex.txt"
  };

  static const unsigned char fill_frag_shader[] = {
#include "shader/fill_frag_shader_hex.txt"
  };
  static const unsigned char fill_frag_shader_aa[] = {
#include "shader/fill_edge_aa_frag_shader_hex.txt"
  };

  vk->fill_vert_shader = vknvg_createShaderModule(device, fill_vert_shader, sizeof(fill_vert_shader), allocator);
  vk->fill_frag_shader = vknvg_createShaderModule(device, fill_frag_shader, sizeof(fill_frag_shader), allocator);
  vk->fill_frag_shader_aa = vknvg_createShaderModule(device, fill_frag_shader_aa, sizeof(fill_frag_shader_aa), allocator);
  int align = vk->create_info.physical_device_properties.limits.minUniformBufferOffsetAlignment;

  vk->fragSize = sizeof(VKNVGfragUniforms) + align - sizeof(VKNVGfragUniforms) % align;

  vk->desc_layout = vknvg_createDescriptorSetLayout(device, allocator);
  vk->pipeline_layout = vknvg_createPipelineLayout(device, vk->desc_layout, allocator);
  return 1;
}

static int vknvg_renderCreateTexture(void *uptr, int type, int w, int h, int imageFlags, const unsigned char *data) {
  VKNVGcontext *vk = (VKNVGcontext *)uptr;
  VKNVGtexture *tex = vknvg_allocTexture(vk);
  if (!tex) {
    return 0;
  }

  VkDevice device = vk->create_info.device;
  const VkAllocationCallbacks *allocator = vk->create_info.allocator;

  VkImageCreateInfo image_create_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  image_create_info.pNext = nullptr;
  image_create_info.imageType = VK_IMAGE_TYPE_2D;
  if (type == NVG_TEXTURE_RGBA) {
    image_create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
  } else {
    image_create_info.format = VK_FORMAT_R8_UNORM;
  }

  image_create_info.extent.width = w;
  image_create_info.extent.height = h;
  image_create_info.extent.depth = 1;
  image_create_info.mipLevels = 1;
  image_create_info.arrayLayers = 1;
  image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_create_info.tiling = VK_IMAGE_TILING_LINEAR;
  image_create_info.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
  image_create_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
  image_create_info.queueFamilyIndexCount = 0;
  image_create_info.pQueueFamilyIndices = nullptr;
  image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_create_info.flags = 0;

  VkMemoryAllocateInfo mem_alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mem_alloc.allocationSize = 0;

  VkImage mappableImage;
  VkDeviceMemory mappableMemory;

  NVGVK_CHECK_RESULT(vkCreateImage(device, &image_create_info, allocator, &mappableImage));

  VkMemoryRequirements mem_reqs;
  vkGetImageMemoryRequirements(device, mappableImage, &mem_reqs);

  mem_alloc.allocationSize = mem_reqs.size;

  VkResult res = vknvg_memory_type_from_properties(vk->create_info.memory_properties, mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &mem_alloc.memoryTypeIndex);
  assert(res == VK_SUCCESS);

  NVGVK_CHECK_RESULT(vkAllocateMemory(device, &mem_alloc, allocator, &mappableMemory));

  NVGVK_CHECK_RESULT(vkBindImageMemory(device, mappableImage, mappableMemory, 0));

  VkSamplerCreateInfo samplerCreateInfo = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  if (imageFlags & NVG_IMAGE_NEAREST) {
    samplerCreateInfo.magFilter = VK_FILTER_NEAREST;
    samplerCreateInfo.minFilter = VK_FILTER_NEAREST;
  } else {
    samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
    samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
  }
  samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  if (imageFlags & NVG_IMAGE_REPEATX) {
    samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
  } else {
    samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  }
  samplerCreateInfo.mipLodBias = 0.0;
  samplerCreateInfo.anisotropyEnable = VK_FALSE;
  samplerCreateInfo.maxAnisotropy = 1;
  samplerCreateInfo.compareEnable = VK_FALSE;
  samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
  samplerCreateInfo.minLod = 0.0;
  samplerCreateInfo.maxLod = 0.0;
  samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

  /* create sampler */
  NVGVK_CHECK_RESULT(vkCreateSampler(device, &samplerCreateInfo, allocator, &tex->sampler));

  VkImageViewCreateInfo view_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  view_info.pNext = nullptr;
  view_info.image = mappableImage;
  view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_info.format = image_create_info.format;
  view_info.components.r = VK_COMPONENT_SWIZZLE_R;
  view_info.components.g = VK_COMPONENT_SWIZZLE_G;
  view_info.components.b = VK_COMPONENT_SWIZZLE_B;
  view_info.components.a = VK_COMPONENT_SWIZZLE_A;
  view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  view_info.subresourceRange.baseMipLevel = 0;
  view_info.subresourceRange.levelCount = 1;
  view_info.subresourceRange.baseArrayLayer = 0;
  view_info.subresourceRange.layerCount = 1;

  VkImageView image_view;
  NVGVK_CHECK_RESULT(vkCreateImageView(device, &view_info, allocator, &image_view));

  tex->height = h;
  tex->width = w;
  tex->image = mappableImage;
  tex->view = image_view;
  tex->mem = mappableMemory;
  tex->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  tex->type = type;
  tex->flags = imageFlags;
  if (data) {
    vknvg_UpdateTexture(device, tex, 0, 0, w, h, data);
  }

  return vknvg_textureId(vk, tex);
}
static int vknvg_renderDeleteTexture(void *uptr, int image) {

  VKNVGcontext *vk = (VKNVGcontext *)uptr;

  VKNVGtexture *tex = vknvg_findTexture(vk, image);

  return vknvg_deleteTexture(vk, tex);
}
static int vknvg_renderUpdateTexture(void *uptr, int image, int x, int y, int w, int h, const unsigned char *data) {
  VKNVGcontext *vk = (VKNVGcontext *)uptr;

  VKNVGtexture *tex = vknvg_findTexture(vk, image);
  vknvg_UpdateTexture(vk->create_info.device, tex, x, y, w, h, data);
  return 1;
}
static int vknvg_renderGetTextureSize(void *uptr, int image, int *w, int *h) {
  VKNVGcontext *vk = (VKNVGcontext *)uptr;
  VKNVGtexture *tex = vknvg_findTexture(vk, image);
  if (tex) {
    *w = tex->width;
    *h = tex->height;
    return 1;
  }
  return 0;
}
static void vknvg_renderViewport(void *uptr, int width, int height, float devicePixelRatio) {
  VKNVGcontext *vk = (VKNVGcontext *)uptr;
  vk->view[0] = (float)width;
  vk->view[1] = (float)height;
}
static void vknvg_renderCancel(void *uptr) {
  VKNVGcontext *vk = (VKNVGcontext *)uptr;

  vk->nverts = 0;
  vk->npaths = 0;
  vk->ncalls = 0;
  vk->nuniforms = 0;
}

static void vknvg_renderFlush(void *uptr) {
  VKNVGcontext *vk = (VKNVGcontext *)uptr;
  VkDevice device = vk->create_info.device;
  VkCommandBuffer cmd_buffer = vk->cmd_buffer;
  VkRenderPass renderpass = vk->create_info.renderpass;
  VkPhysicalDeviceMemoryProperties memory_properties = vk->create_info.memory_properties;
  const VkAllocationCallbacks *allocator = vk->create_info.allocator;

  int i;
  if (vk->ncalls > 0) {
    vknvg_UpdateBuffer(device, allocator, &vk->vertex_buffer, memory_properties, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, vk->verts, vk->nverts * sizeof(vk->verts[0]));
    vknvg_UpdateBuffer(device, allocator, &vk->frag_uniform_buffer, memory_properties, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, vk->uniforms, vk->nuniforms * vk->fragSize);
    vknvg_UpdateBuffer(device, allocator, &vk->vert_uniform_buffer, memory_properties, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, vk->view, sizeof(vk->view));
    vk->current_pipeline = nullptr;

    if (vk->ncalls > vk->cdesc_pool) {
      vkDestroyDescriptorPool(device, vk->desc_pool, allocator);
      vk->desc_pool = vknvg_createDescriptorPool(device, vk->ncalls, allocator);
      vk->cdesc_pool = vk->ncalls;
    } else {
      vkResetDescriptorPool(device, vk->desc_pool, 0);
    }

    VkViewport viewport;
    viewport.width = vk->view[0];
    viewport.height = vk->view[1];
    viewport.minDepth = (float)0.0f;
    viewport.maxDepth = (float)1.0f;
    viewport.x = 0;
    viewport.y = 0;
    vkCmdSetViewport(cmd_buffer, 0, 1, &viewport);

    VkRect2D scissor;
    scissor.extent.width = vk->view[0];
    scissor.extent.height = vk->view[1];
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    vkCmdSetScissor(cmd_buffer, 0, 1, &scissor);

    for (i = 0; i < vk->ncalls; i++) {
      VKNVGcall *call = &vk->calls[i];
      if (call->type == VKNVG_FILL)
        vknvg_fill(vk, call);
      else if (call->type == VKNVG_CONVEXFILL)
        vknvg_convexFill(vk, call);
      else if (call->type == VKNVG_STROKE)
        vknvg_stroke(vk, call);
      else if (call->type == VKNVG_TRIANGLES) {
        vknvg_triangles(vk, call);
      }
    }
  }
  // Reset calls
  vk->nverts = 0;
  vk->npaths = 0;
  vk->ncalls = 0;
  vk->nuniforms = 0;
}
static void vknvg_renderFill(void *uptr, NVGpaint *paint, NVGcompositeOperationState compositeOperation, NVGscissor *scissor, float fringe,
                             const float *bounds, const NVGpath *paths, int npaths) {

  VKNVGcontext *vk = (VKNVGcontext *)uptr;
  VKNVGcall *call = vknvg_allocCall(vk);
  NVGvertex *quad;
  VKNVGfragUniforms *frag;
  int i, maxverts, offset;

  if (call == NULL)
    return;

  call->type = VKNVG_FILL;
  call->triangleCount = 4;
  call->pathOffset = vknvg_allocPaths(vk, npaths);
  if (call->pathOffset == -1)
    goto error;
  call->pathCount = npaths;
  call->image = paint->image;
  call->compositOperation = compositeOperation;

  if (npaths == 1 && paths[0].convex) {
    call->type = VKNVG_CONVEXFILL;
    call->triangleCount = 0; // Bounding box fill quad not needed for convex fill
  }

  // Allocate vertices for all the paths.
  maxverts = vknvg_maxVertCount(paths, npaths) + call->triangleCount;
  offset = vknvg_allocVerts(vk, maxverts);
  if (offset == -1)
    goto error;

  for (i = 0; i < npaths; i++) {
    VKNVGpath *copy = &vk->paths[call->pathOffset + i];
    const NVGpath *path = &paths[i];
    memset(copy, 0, sizeof(VKNVGpath));
    if (path->nfill > 0) {
      copy->fillOffset = offset;
      copy->fillCount = path->nfill;
      memcpy(&vk->verts[offset], path->fill, sizeof(NVGvertex) * path->nfill);
      offset += path->nfill;
    }
    if (path->nstroke > 0) {
      copy->strokeOffset = offset;
      copy->strokeCount = path->nstroke;
      memcpy(&vk->verts[offset], path->stroke, sizeof(NVGvertex) * path->nstroke);
      offset += path->nstroke;
    }
  }

  // Setup uniforms for draw calls
  if (call->type == VKNVG_FILL) {
    // Quad
    call->triangleOffset = offset;
    quad = &vk->verts[call->triangleOffset];
    vknvg_vset(&quad[0], bounds[2], bounds[3], 0.5f, 1.0f);
    vknvg_vset(&quad[1], bounds[2], bounds[1], 0.5f, 1.0f);
    vknvg_vset(&quad[2], bounds[0], bounds[3], 0.5f, 1.0f);
    vknvg_vset(&quad[3], bounds[0], bounds[1], 0.5f, 1.0f);

    call->uniformOffset = vknvg_allocFragUniforms(vk, 2);
    if (call->uniformOffset == -1)
      goto error;
    // Simple shader for stencil
    frag = vknvg_fragUniformPtr(vk, call->uniformOffset);
    memset(frag, 0, sizeof(*frag));
    frag->strokeThr = -1.0f;
    frag->type = NSVG_SHADER_SIMPLE;
    // Fill shader
    vknvg_convertPaint(vk, vknvg_fragUniformPtr(vk, call->uniformOffset + vk->fragSize), paint, scissor, fringe, fringe, -1.0f);
  } else {
    call->uniformOffset = vknvg_allocFragUniforms(vk, 1);
    if (call->uniformOffset == -1)
      goto error;
    // Fill shader
    vknvg_convertPaint(vk, vknvg_fragUniformPtr(vk, call->uniformOffset), paint, scissor, fringe, fringe, -1.0f);
  }

  return;

error:
  // We get here if call alloc was ok, but something else is not.
  // Roll back the last call to prevent drawing it.
  if (vk->ncalls > 0)
    vk->ncalls--;
}

static void vknvg_renderStroke(void *uptr, NVGpaint *paint, NVGcompositeOperationState compositeOperation, NVGscissor *scissor, float fringe,
                               float strokeWidth, const NVGpath *paths, int npaths) {
  VKNVGcontext *vk = (VKNVGcontext *)uptr;
  VKNVGcall *call = vknvg_allocCall(vk);
  int i, maxverts, offset;

  if (call == NULL)
    return;

  call->type = VKNVG_STROKE;
  call->pathOffset = vknvg_allocPaths(vk, npaths);
  if (call->pathOffset == -1)
    goto error;
  call->pathCount = npaths;
  call->image = paint->image;
  call->compositOperation = compositeOperation;

  // Allocate vertices for all the paths.
  maxverts = vknvg_maxVertCount(paths, npaths);
  offset = vknvg_allocVerts(vk, maxverts);
  if (offset == -1)
    goto error;

  for (i = 0; i < npaths; i++) {
    VKNVGpath *copy = &vk->paths[call->pathOffset + i];
    const NVGpath *path = &paths[i];
    memset(copy, 0, sizeof(VKNVGpath));
    if (path->nstroke) {
      copy->strokeOffset = offset;
      copy->strokeCount = path->nstroke;
      memcpy(&vk->verts[offset], path->stroke, sizeof(NVGvertex) * path->nstroke);
      offset += path->nstroke;
    }
  }

  if (vk->flags & NVG_STENCIL_STROKES) {
    // Fill shader
    call->uniformOffset = vknvg_allocFragUniforms(vk, 2);
    if (call->uniformOffset == -1)
      goto error;

    vknvg_convertPaint(vk, vknvg_fragUniformPtr(vk, call->uniformOffset), paint, scissor, strokeWidth, fringe, -1.0f);
    vknvg_convertPaint(vk, vknvg_fragUniformPtr(vk, call->uniformOffset + vk->fragSize), paint, scissor, strokeWidth, fringe, 1.0f - 0.5f / 255.0f);

  } else {
    // Fill shader
    call->uniformOffset = vknvg_allocFragUniforms(vk, 1);
    if (call->uniformOffset == -1)
      goto error;
    vknvg_convertPaint(vk, vknvg_fragUniformPtr(vk, call->uniformOffset), paint, scissor, strokeWidth, fringe, -1.0f);
  }

  return;

error:
  // We get here if call alloc was ok, but something else is not.
  // Roll back the last call to prevent drawing it.
  if (vk->ncalls > 0)
    vk->ncalls--;
}

static void vknvg_renderTriangles(void *uptr, NVGpaint *paint, NVGcompositeOperationState compositeOperation, NVGscissor *scissor,
                                  const NVGvertex *verts, int nverts) {
  VKNVGcontext *vk = (VKNVGcontext *)uptr;

  VKNVGcall *call = vknvg_allocCall(vk);
  VKNVGfragUniforms *frag;

  if (call == nullptr)
    return;

  call->type = VKNVG_TRIANGLES;
  call->image = paint->image;
  call->compositOperation = compositeOperation;

  // Allocate vertices for all the paths.
  call->triangleOffset = vknvg_allocVerts(vk, nverts);
  if (call->triangleOffset == -1)
    goto error;
  call->triangleCount = nverts;

  memcpy(&vk->verts[call->triangleOffset], verts, sizeof(NVGvertex) * nverts);

  // Fill shader
  call->uniformOffset = vknvg_allocFragUniforms(vk, 1);
  if (call->uniformOffset == -1)
    goto error;
  frag = vknvg_fragUniformPtr(vk, call->uniformOffset);
  vknvg_convertPaint(vk, frag, paint, scissor, 1.0f, 1.0f, -1.0f);
  frag->type = NSVG_SHADER_IMG;

  return;

error:
  // We get here if call alloc was ok, but something else is not.
  // Roll back the last call to prevent drawing it.
  if (vk->ncalls > 0)
    vk->ncalls--;
}

static void vknvg_renderDelete(void *uptr) {

  VKNVGcontext *vk = (VKNVGcontext *)uptr;

  VkDevice device = vk->create_info.device;
  const VkAllocationCallbacks *allocator = vk->create_info.allocator;

  for (int i = 0; i < vk->ntextures; i++) {
    if (vk->textures[i].image != VK_NULL_HANDLE) {
      vknvg_deleteTexture(vk, &vk->textures[i]);
    }
  }

  vknvg_destroyBuffer(device, allocator, &vk->vertex_buffer);
  vknvg_destroyBuffer(device, allocator, &vk->frag_uniform_buffer);
  vknvg_destroyBuffer(device, allocator, &vk->vert_uniform_buffer);

  vkDestroyShaderModule(device, vk->fill_vert_shader, allocator);
  vkDestroyShaderModule(device, vk->fill_frag_shader, allocator);
  vkDestroyShaderModule(device, vk->fill_frag_shader_aa, allocator);

  vkDestroyDescriptorPool(device, vk->desc_pool, allocator);
  vkDestroyDescriptorSetLayout(device, vk->desc_layout, allocator);
  vkDestroyPipelineLayout(device, vk->pipeline_layout, allocator);

  for (int i = 0; i < vk->npipelines; i++) {
    vkDestroyPipeline(device, vk->pipelines[i].pipeline, allocator);
  }

  free(vk->textures);
  free(vk);
}

NVGcontext *nvgCreateVk(VKNVGCreateInfo create_info, int flags) {
  NVGparams params;
  NVGcontext *ctx = nullptr;
  VKNVGcontext *vk = (VKNVGcontext *)malloc(sizeof(VKNVGcontext));
  if (vk == nullptr)
    goto error;
  memset(vk, 0, sizeof(VKNVGcontext));

  memset(&params, 0, sizeof(params));
  params.renderCreate = vknvg_renderCreate;
  params.renderCreateTexture = vknvg_renderCreateTexture;
  params.renderDeleteTexture = vknvg_renderDeleteTexture;
  params.renderUpdateTexture = vknvg_renderUpdateTexture;
  params.renderGetTextureSize = vknvg_renderGetTextureSize;
  params.renderViewport = vknvg_renderViewport;
  params.renderCancel = vknvg_renderCancel;
  params.renderFlush = vknvg_renderFlush;
  params.renderFill = vknvg_renderFill;
  params.renderStroke = vknvg_renderStroke;
  params.renderTriangles = vknvg_renderTriangles;
  params.renderDelete = vknvg_renderDelete;
  params.userPtr = vk;
  params.edgeAntiAlias = flags & NVG_ANTIALIAS ? 1 : 0;

  vk->flags = flags;
  vk->create_info = create_info;
  vk->cmd_buffer = create_info.cmd_buffer;

  ctx = nvgCreateInternal(&params);
  if (ctx == nullptr)
    goto error;

  return ctx;

error:
  // 'gl' is freed by nvgDeleteInternal.
  if (ctx != nullptr)
    nvgDeleteInternal(ctx);
  return nullptr;
}
void nvgDeleteVk(NVGcontext *ctx) {
  nvgDeleteInternal(ctx);
}

void nvgvkSetCommandBufferStore(struct NVGcontext *ctx, VkCommandBuffer cmd_buffer) {
  VKNVGcontext *vk = (struct VKNVGcontext *)nvgInternalParams(ctx)->userPtr;
  vk->cmd_buffer = cmd_buffer;
}

#if !defined(__cplusplus) || defined(NANOVG_VK_NO_nullptrPTR)
#undef nullptr
#endif
#endif