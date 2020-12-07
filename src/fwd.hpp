#pragma once

// Make sure to *never* include them
#define VK_NO_PROTOTYPES

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace nytl {

// span
constexpr const std::size_t dynamic_extent = std::size_t(-1);
template<typename T, std::size_t N = dynamic_extent> class span;
template<typename T, std::size_t N = dynamic_extent>
using Span = span<T, N>;

// flags
template<typename T, typename U = std::underlying_type_t<T>> class Flags;

} // namespace nytl

namespace fuen {

// C++20: replace with std::span
using nytl::span;
using nytl::Flags;

class Gui;
struct RenderBuffer;

struct Device;
struct Instance;
struct Queue;
struct Swapchain;
struct Image;
struct ImageView;
struct Framebuffer;
struct RenderPass;
struct CommandBuffer;
struct Buffer;
struct DeviceMemory;
struct Fence;
struct Event;
struct Semaphore;
struct CommandPool;
struct DescriptorPool;
struct DescriptorSet;
struct DescriptorSetLayout;
struct ShaderModule;
struct Pipeline;
struct ComputePipeline;
struct GraphicsPipeline;
struct PipelineLayout;
struct Sampler;
struct MemoryResource;
struct RenderPassDesc;

struct Command;
struct SectionCommand;

struct RenderData;
struct DisplayWindow;
struct Overlay;
struct Draw;

struct SpirvData;

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using i16 = std::int16_t;
using i32 = std::int32_t;

struct DescriptorSetRef {
	DescriptorSet* ds;
	u32 binding {};
	u32 elem {};
};

} // namespace fuen

#define VK_CHECK(x) do {\
		auto result = (x);\
		dlg_assert(result == VK_SUCCESS); \
	} while(0)