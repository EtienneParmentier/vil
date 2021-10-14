#pragma once

#include <util/vec.hpp>
#include <gui/render.hpp>
#include <optional>

namespace vil {

// Implements an imgui ImageViewer, including zooming/panning,
// min/max/aspect/layer/slice/mip selection and texel value readback.
// Can be used for any VkImage object (created by us or by the application),
// will internally create ImageViews and descriptors for it as needed.
class ImageViewer {
public:
	enum Flags {
		preserveReadbacks = (1 << 0u), // still consider previous readbacks valid
		preserveSelection = (1 << 1u), // preserve aspect/level/min/max values
		preserveZoomPan = (1 << 2u), // preserve zoom/panning of the image

		// whether the selected image supports transferSrc.
		// (lowkey deprecated though)
		supportsTransferSrc = (1 << 3u),
	};

	static constexpr auto useSamplingCopy = true;

public:
	void init(Gui& gui);

	// Selects the new image with the given properties to view.
	// - initialLayout: the layout of the image before displaying it here
	// - finalLayout: the layout the image should have afterwards
	// - tryPreserveSelected: Whether the old selection (zoom/pan, aspect,
	//   min/max/layer) should be preserved, if possible.
	void select(VkImage, VkExtent3D, VkImageType, VkFormat,
		const VkImageSubresourceRange&, VkImageLayout initialLayout,
		VkImageLayout finalLayout, u32 /*Flags*/ flags);

	void reset();
	void unselect();
	void display(Draw& draw);
	const auto& imageDraw() const { return imageDraw_; }

private:
	// Called during recording before the image is rendered via imgui.
	// Will perform transitions, if needed, and draw the display area background.
	void recordPreImage(VkCommandBuffer cb);
	void drawBackground(VkCommandBuffer cb);

	// Called during recording after the image was rendered via imgui.
	// Will perform transitions, if needed.
	void recordPostImage(Draw& draw);

	void doCopy(VkCommandBuffer cb, Draw& draw, VkImageLayout oldLayout);
	void doSample(VkCommandBuffer cb, Draw& draw, VkImageLayout oldLayout);
	void copyComplete(Draw&);

	void createData();

private:
	// general, logical info
	Draw* draw_ {}; // current draw
	Gui* gui_ {};

	// readback data
	struct Readback {
		OwnBuffer own;
		Draw* pending {};

		bool valid {};
		VkOffset2D texel {};
		float layer {};
		unsigned level {};

		VkDescriptorSet opDS {};
	};

	std::vector<Readback> readbacks_;
	std::optional<unsigned> lastReadback_ {};
	VkOffset2D readTexelOffset_ {};

	// displayed image information and selection
	DrawGuiImage imageDraw_ {};
	VkImageAspectFlagBits aspect_ {};
	Vec2f canvasOffset_ {};
	Vec2f canvasSize_ {};
	bool panning_ {};

	VkExtent3D extent_ {};
	VkImageType imgType_ {};
	VkFormat format_ {};
	VkImageSubresourceRange subresRange_ {};

	Vec2f offset_ {}; // in uv coords
	float scale_ {1.f};

	VkImage src_ {};
	VkImageLayout initialImageLayout_ {};
	VkImageLayout finalImageLayout_ {};
	bool copyTexel_ {true};

	// drawing data
	// reference-counted image view and descriptor set because we need
	// to keep them alive until all guidraw submissions using them are
	// finished.
	struct DrawData {
		Gui* gui {};
		VkImageView view {};
		VkDescriptorSet ds {};
		u32 refCount {};

		~DrawData();
	};

	IntrusivePtr<DrawData> data_;
};

} // namespace vil

