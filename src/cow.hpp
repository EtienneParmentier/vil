#pragma once

#include <fwd.hpp>
#include <variant>
#include <optional>
#include <util/ownbuf.hpp>

// We sometimes want to reference buffer or image data later on - e.g.
// when showing inputs to a shader in the command gui.
// Instead of always copying all the data, we just create a cow - Copy On Write.
// Images or buffers that have associated cows will resolve them - i.e.
// make a proper copy of their content - before they are written.
// NOTE: WIP, this is still a concept in an early stage

namespace vil {

struct CopiedImage {
	Device* dev {};
	VkImage image {};
	VkDeviceMemory memory {};
	VkExtent3D extent {};
	u32 layerCount {};
	u32 levelCount {};
	VkImageAspectFlags aspectMask {};
	VkDeviceSize neededMemory {};
	VkFormat format {};

	CopiedImage() = default;
	[[nodiscard]] bool init(Device& dev, VkFormat, const VkExtent3D&,
		u32 layers, u32 levels, VkImageAspectFlags aspects, u32 srcQueueFam);
	~CopiedImage();

	VkImageSubresourceRange subresRange() const {
		return {aspectMask, 0, levelCount, 0, layerCount};
	}
};

struct CowImageRange;
struct CowBufferRange;
struct ImageRangeCopy;
struct BufferRangeCopy;

struct CowResolveOp {
	// To be signaled when operation is finished.
	// Will be returned to the pool when completed.
	VkFence fence {};
	VkCommandBuffer cb {};
	Queue* queue {};

	// They point directly into the respective ImageRangeCopy/BufferRangeCopy
	// objects
	std::vector<ImageRangeCopy*> imgCopies;
	std::vector<BufferRangeCopy*> bufCopies;

	// for imageToBuffer copies
	std::vector<VkImageView> imageViews;
	std::vector<VkDescriptorSet> descriptorSets;
};

struct ImageRangeCopy {
	CowResolveOp* op {};
	CopiedImage img;
};

struct BufferRangeCopy {
	CowResolveOp* op {};
	OwnBuffer buf;
};

struct CowImageRange {
	std::variant<std::monostate, BufferRangeCopy, ImageRangeCopy> copy;
	std::atomic<u32> refCount {}; // TODO(PERF): do we need atomic here?
	bool imageAsBuffer {};

	Image* source {};
	VkImageSubresourceRange range {};

	u32 queueFams {}; // bitset
	union {
		VkImageUsageFlagBits addImageFlags {};
		VkBufferUsageFlags addBufferFlags;
	};

	CowImageRange();
	~CowImageRange();
};

struct CowBufferRange {
	std::optional<BufferRangeCopy> copy;
	std::atomic<u32> refCount {}; // TODO(PERF): do we need atomic here?

	Buffer* source {};
	VkDeviceSize offset {};
	VkDeviceSize size {};

	u32 queueFams {}; // bitset
	VkBufferUsageFlags addFlags {};

	CowBufferRange();
	~CowBufferRange();
};

void initLocked(Device&, CowResolveOp&);
void recordResolve(Device&, CowResolveOp&, CowBufferRange&);
void recordResolve(Device&, CowResolveOp&, CowImageRange&);
void finishLocked(Device&, CowResolveOp&);

// Returns whether the given handles support cows.
// Certain resource properties makes it very hard to track writing.
bool allowCowLocked(const Image& img);
bool allowCowLocked(const Buffer& buf);

// Common utility
void initAndCopy(Device& dev, VkCommandBuffer cb, OwnBuffer& dst,
	VkBufferUsageFlags addFlags, Buffer& src,
	VkDeviceSize srcOffset, VkDeviceSize size, u32 queueFamsBitset);
void performCopy(Device& dev, VkCommandBuffer cb, const Buffer& src,
	VkDeviceSize srcOffset, OwnBuffer& dst, VkDeviceSize dstOffset,
	VkDeviceSize size);
void initAndCopy(Device& dev, VkCommandBuffer cb, CopiedImage& dst, Image& src,
	VkImageLayout srcLayout, VkImageSubresourceRange srcSubres,
	u32 srcQueueFam);
void initAndSampleCopy(Device& dev, VkCommandBuffer cb,
	OwnBuffer& dst, Image& src, VkImageLayout srcLayout,
	const VkImageSubresourceRange& srcSubres, u32 queueFamsBitset,
	std::vector<VkImageView>& imgViews, std::vector<VkDescriptorSet>& dss);

} // namespace vil
