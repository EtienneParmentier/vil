#include <command/commands.hpp>
#include <handles.hpp>
#include <shader.hpp>
#include <cb.hpp>
#include <util/span.hpp>
#include <util/util.hpp>
#include <util/ext.hpp>
#include <gui/gui.hpp>
#include <gui/util.hpp>
#include <gui/commandHook.hpp>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <vk/enumString.hpp>
#include <vk/format_utils.h>
#include <iomanip>

// TODO:
// - a lot of commands are still missing valid match() implementations.
//   some commands (bind, sync) will need contextual information, i.e.
//   an external match implementation. Or maybe just having 'prev'
//   links in addition to 'next' is already enough? probably not,
//   the commands itself also should not do the iteration, should not
//   know about other commands.
// - when the new match implementation is working, remove nameDesc

namespace vil {

// Command utility
template<typename C>
auto rawHandles(const C& handles) {
	using VkH = decltype(handle(*handles[0]));
	std::vector<VkH> ret;
	ret.reserve(handles.size());
	for(auto* h : handles) {
		ret.push_back(handle(*h));
	}

	return ret;
}

// checkUnset
template<typename H>
void checkReplace(H*& handlePtr, const CommandAllocHashMap<DeviceHandle*, DeviceHandle*>& map) {
	if(!handlePtr) {
		return;
	}

	auto it = map.find(handlePtr);
	if(it != map.end()) {
		handlePtr = static_cast<H*>(it->second);
	}
}

template<typename H>
void checkReplace(span<H*> handlePtr, const CommandAllocHashMap<DeviceHandle*, DeviceHandle*>& map) {
	for(auto& ptr : handlePtr) {
		checkReplace(ptr, map);
	}
}

// ArgumentsDesc
NameResult name(DeviceHandle* handle, NullName nullName) {
	if(!handle) {
		switch(nullName) {
			case NullName::null: return {NameType::null, "<null>"};
			case NullName::destroyed: return {NameType::null, "<destroyed>"};
			case NullName::empty: return {NameType::null, ""};

		}
	}

	auto name = vil::name(*handle);
	if(handle->name.empty()) {
		return {NameType::unnamed, name};
	}

	return {NameType::named, name};
}

// copy util
std::string printImageOffset(Image* img, const VkOffset3D& offset) {
	if(img && img->ci.imageType == VK_IMAGE_TYPE_1D) {
		return dlg::format("{}", offset.x);
	} else if(img && img->ci.imageType == VK_IMAGE_TYPE_2D) {
		return dlg::format("{}, {}", offset.x, offset.y);
	} else {
		return dlg::format("{}, {}, {}", offset.x, offset.y, offset.z);
	}
}

std::string printImageSubresLayers(Image* img, const VkImageSubresourceLayers& subres) {
	std::string subresStr;
	auto sepStr = "";
	if(!img || img->ci.mipLevels > 1) {
		subresStr = dlg::format("{}mip {}", sepStr, subres.mipLevel);
		sepStr = ", ";
	}

	if(!img || img->ci.arrayLayers > 1) {
		if(subres.layerCount > 1) {
			subresStr = dlg::format("{}layers {}..{}", sepStr,
				subres.baseArrayLayer, subres.baseArrayLayer + subres.layerCount - 1);
		} else {
			subresStr = dlg::format("{}layer {}", sepStr, subres.baseArrayLayer);
		}

		sepStr = ", ";
	}

	return subresStr;
}

std::string printImageRegion(Image* img, const VkOffset3D& offset,
		const VkImageSubresourceLayers& subres) {

	auto offsetStr = printImageOffset(img, offset);
	auto subresStr = printImageSubresLayers(img, subres);

	auto sep = subresStr.empty() ? "" : ", ";
	return dlg::format("({}{}{})", offsetStr, sep, subresStr);
}

std::string printBufferImageCopy(Image* image,
		const VkBufferImageCopy2KHR& copy, bool bufferToImage) {
	auto imgString = printImageRegion(image, copy.imageOffset, copy.imageSubresource);

	std::string sizeString;
	if(image && image->ci.imageType == VK_IMAGE_TYPE_1D) {
		sizeString = dlg::format("{}", copy.imageExtent.width);
	} else if(image && image->ci.imageType <= VK_IMAGE_TYPE_2D) {
		sizeString = dlg::format("{} x {}", copy.imageExtent.width,
			copy.imageExtent.height);
	} else {
		sizeString = dlg::format("{} x {} x {}", copy.imageExtent.width,
			copy.imageExtent.height, copy.imageExtent.depth);
	}

	auto bufString = dlg::format("offset {}", copy.bufferOffset);
	if(copy.bufferRowLength || copy.bufferImageHeight) {
		bufString += dlg::format(", rowLength {}, imageHeight {}",
			copy.bufferRowLength, copy.bufferImageHeight);
	}

	if(bufferToImage) {
		return dlg::format("({}) -> {} [{}]", bufString, imgString, sizeString);
	} else {
		return dlg::format("({}) -> {} [{}]", imgString, bufString, sizeString);
	}
}

// API
std::vector<const Command*> displayCommands(const Command* cmd,
		const Command* selected, Command::TypeFlags typeFlags) {
	// TODO: should use imgui list clipper, might have *a lot* of commands here.
	// But first we have to restrict what cmd->display can actually do.
	// Would also have to pre-filter commands for that. And stop at every
	// (expanded) parent command (but it's hard to tell whether they are
	// expanded).
	std::vector<const Command*> ret;
	while(cmd) {
		if((typeFlags & cmd->type())) {
			ImGui::Separator();
			if(auto reti = cmd->display(selected, typeFlags); !reti.empty()) {
				dlg_assert(ret.empty());
				ret = reti;
			}
		}

		cmd = cmd->next;
	}

	return ret;
}


// Command
std::vector<const Command*> Command::display(const Command* sel, TypeFlags typeFlags) const {
	if(!(typeFlags & this->type())) {
		return {};
	}

	int flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	if(sel == this) {
		flags |= ImGuiTreeNodeFlags_Selected;
	}

	auto idStr = dlg::format("{}:{}", nameDesc(), relID);
	ImGui::TreeNodeEx(idStr.c_str(), flags, "%s", toString().c_str());

	std::vector<const Command*> ret;
	if(ImGui::IsItemClicked()) {
		ret = {this};
	}

	return ret;
}

bool Command::isChild(const Command& cmd) const {
	auto* it = children();
	while(it) {
		if(it == &cmd) {
			return true;
		}

		it = it->next;
	}

	return false;
}

bool Command::isDescendant(const Command& cmd) const {
	auto* it = children();
	while(it) {
		if(it == &cmd || it->isDescendant(cmd)) {
			return true;
		}

		it = it->next;
	}

	return false;
}

float Command::match(const Command& cmd) const {
	return typeid(cmd) == typeid(*this) ? 1.f : 0.f;
}

// Commands
std::vector<const Command*> ParentCommand::display(const Command* selected,
		TypeFlags typeFlags, const Command* cmd) const {
	int flags = ImGuiTreeNodeFlags_OpenOnArrow;
	if(this == selected) {
		flags |= ImGuiTreeNodeFlags_Selected;
	}

	std::vector<const Command*> ret {};
	auto idStr = dlg::format("{}:{}", nameDesc(), relID);
	auto open = ImGui::TreeNodeEx(idStr.c_str(), flags, "%s", toString().c_str());
	if(ImGui::IsItemClicked()) {
		// don't select when only clicked on arrow
		if(ImGui::GetMousePos().x > ImGui::GetItemRectMin().x + 30) {
			ret = {this};
		}
	}

	if(open) {
		// we don't want as much space as tree nodes
		auto s = 0.3 * ImGui::GetTreeNodeToLabelSpacing();
		ImGui::Unindent(s);

		auto retc = displayCommands(cmd, selected, typeFlags);
		if(!retc.empty()) {
			dlg_assert(ret.empty());
			ret = std::move(retc);
			ret.insert(ret.begin(), this);
		}

		ImGui::Indent(s);
		ImGui::TreePop();
	}

	return ret;
}

std::vector<const Command*> ParentCommand::display(const Command* selected,
		TypeFlags typeFlags) const {
	return this->display(selected, typeFlags, children());
}

// BarrierCmdBase
std::string formatQueueFam(u32 fam) {
	if(fam == VK_QUEUE_FAMILY_IGNORED) {
		return "ignored";
	} else if(fam == VK_QUEUE_FAMILY_EXTERNAL) {
		return "external";
	} else if(fam == VK_QUEUE_FAMILY_FOREIGN_EXT) {
		return "foreign";
	}

	return std::to_string(fam);
}

void BarrierCmdBase::replace(const CommandAllocHashMap<DeviceHandle*, DeviceHandle*>& map) {
	checkReplace(buffers, map);
	checkReplace(images, map);
}

void BarrierCmdBase::displayInspector(Gui& gui) const {
	imGuiText("srcStage: {}", vk::flagNames(VkPipelineStageFlagBits(srcStageMask)));
	imGuiText("dstStage: {}", vk::flagNames(VkPipelineStageFlagBits(dstStageMask)));

	if(!memBarriers.empty()) {
		imGuiText("Memory Barriers");
		ImGui::Indent();
		for(auto i = 0u; i < memBarriers.size(); ++i) {
			auto& memb = memBarriers[i];
			imGuiText("srcAccess: {}", vk::flagNames(VkAccessFlagBits(memb.srcAccessMask)));
			imGuiText("dstAccess: {}", vk::flagNames(VkAccessFlagBits(memb.dstAccessMask)));
			ImGui::Separator();
		}
		ImGui::Unindent();
	}

	if(!bufBarriers.empty()) {
		imGuiText("Buffer Barriers");
		ImGui::Indent();
		for(auto i = 0u; i < bufBarriers.size(); ++i) {
			auto& memb = bufBarriers[i];
			refButtonD(gui, buffers[i]);
			imGuiText("offset: {}", memb.offset);
			imGuiText("size: {}", memb.size);
			imGuiText("srcAccess: {}", vk::flagNames(VkAccessFlagBits(memb.srcAccessMask)));
			imGuiText("dstAccess: {}", vk::flagNames(VkAccessFlagBits(memb.dstAccessMask)));
			imGuiText("srcQueueFamily: {}", formatQueueFam(memb.srcQueueFamilyIndex));
			imGuiText("dstQueueFamily: {}", formatQueueFam(memb.dstQueueFamilyIndex));
			ImGui::Separator();
		}
		ImGui::Unindent();
	}

	if(!imgBarriers.empty()) {
		imGuiText("Image Barriers");
		ImGui::Indent();
		for(auto i = 0u; i < imgBarriers.size(); ++i) {
			auto& imgb = imgBarriers[i];
			refButtonD(gui, images[i]);

			auto& subres = imgb.subresourceRange;
			imGuiText("aspectMask: {}", vk::flagNames(VkImageAspectFlagBits(subres.aspectMask)));
			imGuiText("baseArrayLayer: {}", subres.baseArrayLayer);
			imGuiText("layerCount: {}", subres.layerCount);
			imGuiText("baseMipLevel: {}", subres.baseMipLevel);
			imGuiText("levelCount: {}", subres.levelCount);

			imGuiText("srcAccess: {}", vk::flagNames(VkAccessFlagBits(imgb.srcAccessMask)));
			imGuiText("dstAccess: {}", vk::flagNames(VkAccessFlagBits(imgb.dstAccessMask)));
			imGuiText("oldLayout: {}", vk::name(imgb.oldLayout));
			imGuiText("newLayout: {}", vk::name(imgb.newLayout));
			imGuiText("srcQueueFamily: {}", formatQueueFam(imgb.srcQueueFamilyIndex));
			imGuiText("dstQueueFamily: {}", formatQueueFam(imgb.dstQueueFamilyIndex));
			ImGui::Separator();
		}
		ImGui::Unindent();
	}
}

struct Matcher {
	float match {};
	float total {};

	static Matcher noMatch() { return {0.f, -1.f}; }
};

template<typename T>
void add(Matcher& m, const T& a, const T& b, float weight = 1.0) {
	m.total += weight;
	m.match += (a == b) ? weight : 0.f;
}

template<typename T>
void addNonNull(Matcher& m, T* a, T* b, float weight = 1.0) {
	m.total += weight;
	m.match += (a == b && a != nullptr) ? weight : 0.f;
}

bool operator==(const VkMemoryBarrier& a, const VkMemoryBarrier& b) {
	return a.dstAccessMask == b.dstAccessMask &&
		a.srcAccessMask == b.srcAccessMask;
}

bool operator==(const VkImageMemoryBarrier& a, const VkImageMemoryBarrier& b) {
	bool queueTransferA =
		a.srcQueueFamilyIndex != VK_QUEUE_FAMILY_IGNORED &&
		a.dstQueueFamilyIndex != VK_QUEUE_FAMILY_IGNORED &&
		a.srcQueueFamilyIndex != a.dstQueueFamilyIndex;
	bool queueTransferB =
		b.srcQueueFamilyIndex != VK_QUEUE_FAMILY_IGNORED &&
		b.dstQueueFamilyIndex != VK_QUEUE_FAMILY_IGNORED &&
		b.srcQueueFamilyIndex != b.dstQueueFamilyIndex;

	if(queueTransferA || queueTransferB) {
		// TODO: respect other relevant fields as well
		return queueTransferA == queueTransferB &&
			a.srcQueueFamilyIndex == b.srcQueueFamilyIndex &&
			a.dstQueueFamilyIndex == b.dstQueueFamilyIndex;
	}

	return a.dstAccessMask == b.dstAccessMask &&
		a.srcAccessMask == b.srcAccessMask &&
		a.oldLayout == b.oldLayout &&
		a.newLayout == b.newLayout &&
		a.image == b.image &&
		a.subresourceRange.aspectMask == b.subresourceRange.aspectMask &&
		a.subresourceRange.baseArrayLayer == b.subresourceRange.baseArrayLayer &&
		a.subresourceRange.baseMipLevel == b.subresourceRange.baseMipLevel &&
		a.subresourceRange.layerCount == b.subresourceRange.layerCount &&
		a.subresourceRange.levelCount == b.subresourceRange.levelCount;
}

// TODO: should probably be match functions returning a float instead,
// consider offsets. Same for image barrier above
bool operator==(const VkBufferMemoryBarrier& a, const VkBufferMemoryBarrier& b) {
	bool queueTransferA =
		a.srcQueueFamilyIndex != VK_QUEUE_FAMILY_IGNORED &&
		a.dstQueueFamilyIndex != VK_QUEUE_FAMILY_IGNORED &&
		a.srcQueueFamilyIndex != a.dstQueueFamilyIndex;
	bool queueTransferB =
		b.srcQueueFamilyIndex != VK_QUEUE_FAMILY_IGNORED &&
		b.dstQueueFamilyIndex != VK_QUEUE_FAMILY_IGNORED &&
		b.srcQueueFamilyIndex != b.dstQueueFamilyIndex;

	if(queueTransferA || queueTransferB) {
		// TODO: respect other relevant fields as well
		return queueTransferA == queueTransferB &&
			a.srcQueueFamilyIndex == b.srcQueueFamilyIndex &&
			a.dstQueueFamilyIndex == b.dstQueueFamilyIndex;
	}

	return a.dstAccessMask == b.dstAccessMask &&
		a.srcAccessMask == b.srcAccessMask &&
		a.buffer == b.buffer &&
		a.size == b.size;
}

template<typename T>
void addSpanUnordered(Matcher& m, span<T> a, span<T> b, float weight = 1.0) {
	if(a.empty() && b.empty()) {
		m.match += weight;
		m.total += weight;
		return;
	}

	auto count = 0u;
	for(auto i = 0u; i < a.size(); ++i) {
		// check how many times we've seen it already
		auto numSeen = 0u;
		for(auto j = 0u; j < i; ++j) {
			if(a[j] == a[i]) {
				++numSeen;
			}
		}

		// find it in b
		for(auto j = 0u; j < b.size(); ++j) {
			if(a[i] == b[j] && numSeen-- == 0u) {
				++count;
				break;
			}
		}
	}

	m.match += (weight * count) / std::max(a.size(), b.size());
	m.total += weight;
}

float eval(const Matcher& m) {
	dlg_assertm(m.match <= m.total, "match {}, total {}", m.match, m.total);
	return m.total == 0.f ? 1.f : m.match / m.total;
}

// match ideas:
// - matching for bitmask flags
// - matching for sorted spans
// - multiplicative matching addition?
//   basically saying "if this doesn't match, the whole command shouldn't match,
//   even if everything else does"
//   Different than just using a high weight in that a match doesn't automatically
//   mean a match for the whole command.

Matcher BarrierCmdBase::doMatch(const BarrierCmdBase& cmd) const {
	Matcher m;
	add(m, this->srcStageMask, cmd.srcStageMask);
	add(m, this->dstStageMask, cmd.dstStageMask);

	addSpanUnordered(m, this->memBarriers, cmd.memBarriers);
	addSpanUnordered(m, this->bufBarriers, cmd.bufBarriers);
	addSpanUnordered(m, this->imgBarriers, cmd.imgBarriers);

	return m;
}

// WaitEventsCmd
void WaitEventsCmd::record(const Device& dev, VkCommandBuffer cb) const {
	auto vkEvents = rawHandles(this->events);
	dev.dispatch.CmdWaitEvents(cb,
		u32(vkEvents.size()), vkEvents.data(),
		this->srcStageMask, this->dstStageMask,
		u32(this->memBarriers.size()), this->memBarriers.data(),
		u32(this->bufBarriers.size()), this->bufBarriers.data(),
		u32(this->imgBarriers.size()), this->imgBarriers.data());

}

void WaitEventsCmd::replace(const CommandAllocHashMap<DeviceHandle*, DeviceHandle*>& map) {
	BarrierCmdBase::replace(map);
	checkReplace(events, map);
}

void WaitEventsCmd::displayInspector(Gui& gui) const {
	for(auto& event : events) {
		refButtonD(gui, event);
	}

	BarrierCmdBase::displayInspector(gui);
}

float WaitEventsCmd::match(const Command& base) const {
	auto* cmd = dynamic_cast<const WaitEventsCmd*>(&base);
	if(!cmd) {
		return 0.f;
	}

	auto m = doMatch(*cmd);
	addSpanUnordered(m, events, cmd->events);

	return eval(m);
}

// BarrierCmd
void BarrierCmd::record(const Device& dev, VkCommandBuffer cb) const {
	dev.dispatch.CmdPipelineBarrier(cb,
		this->srcStageMask, this->dstStageMask, this->dependencyFlags,
		u32(this->memBarriers.size()), this->memBarriers.data(),
		u32(this->bufBarriers.size()), this->bufBarriers.data(),
		u32(this->imgBarriers.size()), this->imgBarriers.data());

}

void BarrierCmd::displayInspector(Gui& gui) const {
	imGuiText("dependencyFlags: {}", vk::flagNames(VkDependencyFlagBits(dependencyFlags)));
	BarrierCmdBase::displayInspector(gui);
}

float BarrierCmd::match(const Command& base) const {
	auto* cmd = dynamic_cast<const BarrierCmd*>(&base);
	if(!cmd) {
		return 0.f;
	}

	auto m = doMatch(*cmd);
	add(m, dependencyFlags, cmd->dependencyFlags);

	return eval(m);
}

// BeginRenderPassCmd
unsigned BeginRenderPassCmd::subpassOfDescendant(const Command& cmd) const {
	auto subpass = this->children();
	for(auto i = 0u; subpass; ++i, subpass = subpass->next) {
		if(subpass->isDescendant(cmd)) {
			return i;
		}
	}

	return u32(-1);
}

std::string BeginRenderPassCmd::toString() const {
	auto [fbRes, fbName] = name(fb);
	auto [rpRes, rpName] = name(rp);
	if(fbRes == NameType::named && rpRes == NameType::named) {
		return dlg::format("BeginRenderPass({}, {})", rpName, fbName);
	} else if(rpRes == NameType::named) {
		return dlg::format("BeginRenderPass({})", rpName);
	} else {
		return "BeginRenderPass";
	}
}

void BeginRenderPassCmd::record(const Device& dev, VkCommandBuffer cb) const {
	if(this->subpassBeginInfo.pNext) {
		auto f = dev.dispatch.CmdBeginRenderPass2;
		dlg_assert(f);
		f(cb, &this->info, &this->subpassBeginInfo);
	} else {
		dev.dispatch.CmdBeginRenderPass(cb, &this->info, this->subpassBeginInfo.contents);
	}
}

std::vector<const Command*> BeginRenderPassCmd::display(const Command* selected,
		TypeFlags typeFlags) const {
	auto cmd = this->children_;
	auto first = static_cast<FirstSubpassCmd*>(nullptr);
	if(cmd) {
		// If we only have one subpass, don't give it an extra section
		// to make everything more compact.
		first = dynamic_cast<FirstSubpassCmd*>(cmd);
		dlg_assert(first);
		if(!first->next) {
			cmd = first->children_;
		}
	}

	auto ret = ParentCommand::display(selected, typeFlags, cmd);
	if(ret.size() > 1 && cmd != children_) {
		ret.insert(ret.begin() + 1, first);
	}

	return ret;
}

void BeginRenderPassCmd::replace(const CommandAllocHashMap<DeviceHandle*, DeviceHandle*>& map) {
	checkReplace(rp, map);
	checkReplace(fb, map);
	ParentCommand::replace(map);
}

void BeginRenderPassCmd::displayInspector(Gui& gui) const {
	refButtonD(gui, fb);
	refButtonD(gui, rp);

	// area
	imGuiText("offset: {}, {}", info.renderArea.offset.x, info.renderArea.offset.y);
	imGuiText("extent: {}, {}", info.renderArea.extent.width,
		info.renderArea.extent.height);

	// clear values
	if(rp) {
		for(auto i = 0u; i < clearValues.size(); ++i) {
			dlg_assert_or(i < rp->desc->attachments.size(), break);
			auto& clearValue = clearValues[i];
			auto& att = rp->desc->attachments[i];

			if(att.loadOp != VK_ATTACHMENT_LOAD_OP_CLEAR) {
				continue;
			}

			imGuiText("Attachment {} clear value:", i);
			ImGui::SameLine();

			if(FormatIsDepthOrStencil(att.format)) {
				imGuiText("Depth {}, Stencil {}",
					clearValue.depthStencil.depth,
					clearValue.depthStencil.stencil);
			} else {
				auto print = [](auto& val) {
					imGuiText("({}, {}, {}, {})", val[0], val[1], val[2], val[3]);
				};

				if(FormatIsSampledFloat(att.format)) {
					print(clearValue.color.float32);
				} else if(FormatIsInt(att.format)) {
					print(clearValue.color.int32);
				} else if(FormatIsUInt(att.format)) {
					print(clearValue.color.uint32);
				}
			}
		}
	}
}

bool same(const RenderPassDesc& a, const RenderPassDesc& b) {
	if(a.subpasses.size() != b.subpasses.size() ||
			a.attachments.size() != b.attachments.size()) {
		return false;
	}

	// compare attachments
	for(auto i = 0u; i < a.attachments.size(); ++i) {
		auto& attA = a.attachments[i];
		auto& attB = b.attachments[i];

		if(attA.format != attB.format ||
				attA.loadOp != attB.loadOp ||
				attA.storeOp != attB.storeOp ||
				attA.initialLayout != attB.initialLayout ||
				attA.finalLayout != attB.finalLayout ||
				attA.stencilLoadOp != attB.stencilLoadOp ||
				attA.stencilStoreOp != attB.stencilStoreOp ||
				attA.samples != attB.samples) {
			return false;
		}
	}

	// compare subpasses
	auto attRefsSame = [](const VkAttachmentReference2& a, const VkAttachmentReference2& b) {
		return a.attachment == b.attachment && (a.attachment == VK_ATTACHMENT_UNUSED ||
				a.aspectMask == b.aspectMask);
	};

	for(auto i = 0u; i < a.subpasses.size(); ++i) {
		auto& subA = a.subpasses[i];
		auto& subB = b.subpasses[i];

		if(subA.colorAttachmentCount != subB.colorAttachmentCount ||
				subA.preserveAttachmentCount != subB.preserveAttachmentCount ||
				bool(subA.pDepthStencilAttachment) != bool(subB.pDepthStencilAttachment) ||
				bool(subA.pResolveAttachments) != bool(subB.pResolveAttachments) ||
				subA.inputAttachmentCount != subB.inputAttachmentCount ||
				subA.pipelineBindPoint != subB.pipelineBindPoint) {
			return false;
		}

		for(auto j = 0u; j < subA.colorAttachmentCount; ++j) {
			if(!attRefsSame(subA.pColorAttachments[j], subB.pColorAttachments[j])) {
				return false;
			}
		}

		for(auto j = 0u; j < subA.inputAttachmentCount; ++j) {
			if(!attRefsSame(subA.pInputAttachments[j], subB.pInputAttachments[j])) {
				return false;
			}
		}

		for(auto j = 0u; j < subA.preserveAttachmentCount; ++j) {
			if(subA.pPreserveAttachments[j] != subB.pPreserveAttachments[j]) {
				return false;
			}
		}

		if(subA.pResolveAttachments) {
			for(auto j = 0u; j < subA.colorAttachmentCount; ++j) {
				if(!attRefsSame(subA.pResolveAttachments[j], subB.pResolveAttachments[j])) {
					return false;
				}
			}
		}

		if(subA.pDepthStencilAttachment &&
				!attRefsSame(*subA.pDepthStencilAttachment, *subB.pDepthStencilAttachment)) {
			return false;
		}
	}

	// TODO: compare dependencies?
	return true;
}

float BeginRenderPassCmd::match(const Command& base) const {
	auto* cmd = dynamic_cast<const BeginRenderPassCmd*>(&base);
	if(!cmd) {
		return 0.f;
	}

	// TODO
	// this will currently break when render passes or framebuffers are used
	// as temporary handles, created as needed and destroyed when submission is
	// done. We would have to keep the renderPassDesc and framebuffer description
	// (mainly the referenced image views) alive.

	// match render pass description
	if(!rp || !cmd->rp || !same(*rp->desc, *cmd->rp->desc)) {
		return 0.f;
	}

	if(!fb || !cmd->fb) {
		return 0.f;
	}

	dlg_assert_or(fb->attachments.size() == cmd->fb->attachments.size(), return 0.f);

	Matcher m;
	for(auto i = 0u; i < fb->attachments.size(); ++i) {
		auto va = fb->attachments[i];
		auto vb = cmd->fb->attachments[i];

		// special case: different images but both are of the same
		// swapchain, we treat them as being the same
		if(va != vb && va->img && vb->img && va->img->swapchain) {
			add(m, va->img->swapchain, vb->img->swapchain);
		} else {
			// the image views have to match, not the images to account
			// for different mips or layers
			add(m, va, vb);
		}
	}

	// TODO: consider render area, clearValues?

	return eval(m);
}

void NextSubpassCmd::record(const Device& dev, VkCommandBuffer cb) const {
	if(this->beginInfo.pNext || this->endInfo.pNext) {
		auto f = dev.dispatch.CmdNextSubpass2;
		f(cb, &this->beginInfo, &this->endInfo);
	} else {
		dev.dispatch.CmdNextSubpass(cb, this->beginInfo.contents);
	}
}

float NextSubpassCmd::match(const Command& base) const {
	auto* cmd = dynamic_cast<const NextSubpassCmd*>(&base);
	if(!cmd) {
		return 0.f;
	}

	return cmd->subpassID == subpassID ? 1.f : 0.f;
}

void EndRenderPassCmd::record(const Device& dev, VkCommandBuffer cb) const {
	if(this->endInfo.pNext) {
		auto f = dev.dispatch.CmdEndRenderPass2;
		f(cb, &this->endInfo);
	} else {
		dev.dispatch.CmdEndRenderPass(cb);
	}
}

// DrawCmdBase
DrawCmdBase::DrawCmdBase(CommandBuffer& cb, const GraphicsState& gfxState) {
	state = copy(cb, gfxState);
	// NOTE: only do this when pipe layout matches pcr layout
	pushConstants.data = copySpan(cb, cb.pushConstants().data);
}

void DrawCmdBase::displayGrahpicsState(Gui& gui, bool indices) const {
	if(indices) {
		dlg_assert(state.indices.buffer);
		imGuiText("Index Buffer: ");
		ImGui::SameLine();
		refButtonD(gui, state.indices.buffer);
		ImGui::SameLine();
		imGuiText("Offset {}, Type {}", state.indices.offset, vk::name(state.indices.type));
	}

	refButtonD(gui, state.pipe);

	imGuiText("Vertex buffers");
	for(auto& vertBuf : state.vertices) {
		if(!vertBuf.buffer) {
			imGuiText("null");
			continue;
		}

		refButtonD(gui, vertBuf.buffer);
		ImGui::SameLine();
		imGuiText("Offset {}", vertBuf.offset);
	}

	// dynamic state
	if(state.pipe && !state.pipe->dynamicState.empty()) {
		imGuiText("DynamicState");
		ImGui::Indent();

		// viewport
		if(state.pipe->dynamicState.count(VK_DYNAMIC_STATE_VIEWPORT)) {
			auto count = state.pipe->viewportState.viewportCount;
			dlg_assert(state.dynamic.viewports.size() >= count);
			if(count == 1) {
				auto& vp = state.dynamic.viewports[0];
				imGuiText("Viewport: pos ({}, {}), size ({}, {}), depth [{}, {}]",
					vp.x, vp.y, vp.width, vp.height, vp.minDepth, vp.maxDepth);
			} else if(count > 1) {
				imGuiText("Viewports");
				for(auto& vp : state.dynamic.viewports.first(count)) {
					ImGui::Bullet();
					imGuiText("pos ({}, {}), size ({}, {}), depth [{}, {}]",
						vp.x, vp.y, vp.width, vp.height, vp.minDepth, vp.maxDepth);
				}
			}
		}
		// scissor
		if(state.pipe->dynamicState.count(VK_DYNAMIC_STATE_SCISSOR)) {
			auto count = state.pipe->viewportState.scissorCount;
			dlg_assert(state.dynamic.scissors.size() >= count);
			if(count == 1) {
				auto& sc = state.dynamic.scissors[0];
				imGuiText("Scissor: offset ({}, {}), extent ({} {})",
					sc.offset.x, sc.offset.y, sc.extent.width, sc.extent.height);
			} else if(count > 1) {
				imGuiText("Scissors");
				for(auto& sc : state.dynamic.scissors.first(count)) {
					ImGui::Bullet();
					imGuiText("offset ({} {}), extent ({} {})",
						sc.offset.x, sc.offset.y, sc.extent.width, sc.extent.height);
				}
			}
		}

		// line width
		if(state.pipe->dynamicState.count(VK_DYNAMIC_STATE_LINE_WIDTH)) {
			imGuiText("Line width: {}", state.dynamic.lineWidth);
		}

		if(state.pipe->dynamicState.count(VK_DYNAMIC_STATE_DEPTH_BIAS)) {
			auto& db = state.dynamic.depthBias;
			imGuiText("Depth bias: constant {}, clamp {}, slope {}",
				db.constant, db.clamp, db.slope);
		}

		if(state.pipe->dynamicState.count(VK_DYNAMIC_STATE_BLEND_CONSTANTS)) {
			auto& bc = state.dynamic.blendConstants;
			imGuiText("Blend Constants: {} {} {} {}",
				bc[0], bc[1], bc[2], bc[3]);
		}

		if(state.pipe->dynamicState.count(VK_DYNAMIC_STATE_DEPTH_BOUNDS)) {
			imGuiText("Depth bounds: [{}, {}]",
				state.dynamic.depthBoundsMin, state.dynamic.depthBoundsMax);
		}

		if(state.pipe->dynamicState.count(VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK)) {
			imGuiText("Stencil compare mask front: {}{}", std::hex,
				state.dynamic.stencilFront.compareMask);
			imGuiText("Stencil compare mask back: {}{}", std::hex,
				state.dynamic.stencilBack.compareMask);
		}

		if(state.pipe->dynamicState.count(VK_DYNAMIC_STATE_STENCIL_WRITE_MASK)) {
			imGuiText("Stencil write mask front: {}{}", std::hex,
				state.dynamic.stencilFront.writeMask);
			imGuiText("Stencil write mask back: {}{}", std::hex,
				state.dynamic.stencilBack.writeMask);
		}

		if(state.pipe->dynamicState.count(VK_DYNAMIC_STATE_STENCIL_REFERENCE)) {
			imGuiText("Stencil reference front: {}{}", std::hex,
				state.dynamic.stencilFront.reference);
			imGuiText("Stencil reference back: {}{}", std::hex,
				state.dynamic.stencilBack.reference);
		}

		ImGui::Unindent();
	} else if(!state.pipe) {
		imGuiText("Can't display relevant dynamic state, pipeline was destroyed");
	} else if(state.pipe->dynamicState.empty()) {
		// imGuiText("No relevant dynamic state");
	}
}

void DrawCmdBase::replace(const CommandAllocHashMap<DeviceHandle*, DeviceHandle*>& map) {
	checkReplace(state.pipe, map);
	checkReplace(state.indices.buffer, map);

	checkReplace(state.rpi.fb, map);
	checkReplace(state.rpi.rp, map);

	for(auto& verts : state.vertices) {
		checkReplace(verts.buffer, map);
	}

	// we don't ever unset descriptor sets, they are accessed via
	// snapshots anyways and we need the original pointer as key into
	// the snapshot map
}

Matcher DrawCmdBase::doMatch(const DrawCmdBase& cmd, bool indexed) const {
	// different pipelines means the draw calls are fundamentally different,
	// no matter if similar data is bound.
	if(!state.pipe || !cmd.state.pipe || state.pipe != cmd.state.pipe) {
		return Matcher::noMatch();
	}

	Matcher m;
	for(auto i = 0u; i < state.pipe->vertexBindings.size(); ++i) {
		dlg_assert(i < state.vertices.size());
		dlg_assert(i < cmd.state.vertices.size());

		addNonNull(m, state.vertices[i].buffer, cmd.state.vertices[i].buffer);

		// Low weight on offset here, it can change frequently for dynamic
		// draw data. But the same buffer is a good indicator for similar
		// commands
		add(m, state.vertices[i].offset, cmd.state.vertices[i].offset, 0.1);
	}

	if(indexed) {
		addNonNull(m, state.indices.buffer, cmd.state.indices.buffer);
		add(m, state.indices.offset, cmd.state.indices.offset, 0.1);

		// different index types is an indicator for fundamentally different
		// commands.
		if(state.indices.type != cmd.state.indices.type) {
			return Matcher::noMatch();
		}
	}

	for(auto& pcr : state.pipe->layout->pushConstants) {
		dlg_assert_or(pcr.offset + pcr.size <= pushConstants.data.size(), continue);
		dlg_assert_or(pcr.offset + pcr.size <= cmd.pushConstants.data.size(), continue);

		m.total += pcr.size;
		if(std::memcmp(&pushConstants.data[pcr.offset],
				&cmd.pushConstants.data[pcr.offset], pcr.size) == 0u) {
			m.match += pcr.size;
		}
	}

	// - we consider the bound descriptors somewhere else since they might
	//   already have been unset from the command
	// - we don't consider the render pass instance here since that should
	//   already have been taken into account via the parent commands
	// TODO: consider dynamic state?

	return m;
}

// DrawCmd
void DrawCmd::record(const Device& dev, VkCommandBuffer cb) const {
	dev.dispatch.CmdDraw(cb, vertexCount, instanceCount, firstVertex, firstInstance);
}

std::string DrawCmd::toString() const {
	return dlg::format("Draw({}, {}, {}, {})",
		vertexCount, instanceCount, firstVertex, firstInstance);
}

void DrawCmd::displayInspector(Gui& gui) const {
	asColumns2({{
		{"vertexCount", "{}", vertexCount},
		{"instanceCount", "{}", instanceCount},
		{"firstVertex", "{}", firstVertex},
		{"firstInstance", "{}", firstInstance},
	}});

	DrawCmdBase::displayGrahpicsState(gui, false);
}

float DrawCmd::match(const Command& base) const {
	auto* cmd = dynamic_cast<const DrawCmd*>(&base);
	if(!cmd) {
		return 0.f;
	}

	// hard matching for now. Might need to relax this in the future.
	if(cmd->vertexCount != vertexCount ||
			cmd->instanceCount != instanceCount ||
			cmd->firstVertex != firstVertex ||
			cmd->firstInstance != firstInstance) {
		return 0.f;
	}

	return eval(doMatch(*cmd, false));
}

// DrawIndirectCmd
void DrawIndirectCmd::record(const Device& dev, VkCommandBuffer cb) const {
	if(indexed) {
		dev.dispatch.CmdDrawIndexedIndirect(cb, buffer->handle, offset, drawCount, stride);
	} else {
		dev.dispatch.CmdDrawIndirect(cb, buffer->handle, offset, drawCount, stride);
	}
}

void DrawIndirectCmd::displayInspector(Gui& gui) const {
	imGuiText("Indirect buffer");
	ImGui::SameLine();
	refButtonD(gui, buffer);
	ImGui::SameLine();
	imGuiText("Offset {}", offset);

	DrawCmdBase::displayGrahpicsState(gui, indexed);
}

void DrawIndirectCmd::replace(const CommandAllocHashMap<DeviceHandle*, DeviceHandle*>& map) {
	checkReplace(buffer, map);
	DrawCmdBase::replace(map);
}

std::string DrawIndirectCmd::toString() const {
	auto [bufNameRes, bufName] = name(buffer);
	auto cmdName = indexed ? "DrawIndexedIndirect" : "DrawIndirect";
	if(bufNameRes == NameType::named) {
		return dlg::format("{}({}, {})", cmdName, bufName, drawCount);
	} else if(drawCount > 1) {
		return dlg::format("{}(drawCount: {})", cmdName, drawCount);
	} else {
		return cmdName;
	}
}

float DrawIndirectCmd::match(const Command& base) const {
	auto* cmd = dynamic_cast<const DrawIndirectCmd*>(&base);
	if(!cmd) {
		return 0.f;
	}

	// hard matching on those; differences would indicate a totally
	// different command structure.
	if(cmd->indexed != this->indexed || cmd->stride != this->stride) {
		return 0.f;
	}

	auto m = doMatch(*cmd, indexed);
	if(m.total == -1.f) {
		return 0.f;
	}

	addNonNull(m, buffer, cmd->buffer);

	// we don't hard-match on drawCount since architectures that choose
	// this dynamically per-frame (e.g. for culling) are common
	add(m, drawCount, cmd->drawCount);
	add(m, offset, cmd->offset, 0.2);

	return eval(m);
}

// DrawIndexedCmd
void DrawIndexedCmd::record(const Device& dev, VkCommandBuffer cb) const {
	dev.dispatch.CmdDrawIndexed(cb, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

std::string DrawIndexedCmd::toString() const {
	return dlg::format("DrawIndexed({}, {}, {}, {}, {})",
		indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void DrawIndexedCmd::displayInspector(Gui& gui) const {
	asColumns2({{
		{"indexCount", "{}", indexCount},
		{"instanceCount", "{}", instanceCount},
		{"firstIndex", "{}", firstIndex},
		{"vertexOffset", "{}", vertexOffset},
		{"firstInstance", "{}", firstInstance},
	}});

	DrawCmdBase::displayGrahpicsState(gui, true);
}

float DrawIndexedCmd::match(const Command& base) const {
	auto* cmd = dynamic_cast<const DrawIndexedCmd*>(&base);
	if(!cmd) {
		return 0.f;
	}

	// hard matching for now. Might need to relax this in the future.
	if(cmd->indexCount != indexCount ||
			cmd->instanceCount != instanceCount ||
			cmd->firstIndex != firstIndex ||
			cmd->vertexOffset != vertexOffset ||
			cmd->firstInstance != firstInstance) {
		return 0.f;
	}

	return eval(doMatch(*cmd, true));
}

// DrawIndirectCountCmd
void DrawIndirectCountCmd::record(const Device& dev, VkCommandBuffer cb) const {
	if(indexed) {
		auto f = dev.dispatch.CmdDrawIndexedIndirectCount;
		f(cb, buffer->handle, offset, countBuffer->handle, countBufferOffset,
			maxDrawCount, stride);
	} else {
		auto f = dev.dispatch.CmdDrawIndirectCount;
		f(cb, buffer->handle, offset,
			countBuffer->handle, countBufferOffset, maxDrawCount, stride);
	}
}

std::string DrawIndirectCountCmd::toString() const {
	// NOTE: we intentionally don't display any extra information here
	// since that's hard to do inuitively
	return indexed ? "DrawIndexedIndirectCount" : "DrawIndirectCount";
}

void DrawIndirectCountCmd::displayInspector(Gui& gui) const {
	imGuiText("Indirect buffer:");
	ImGui::SameLine();
	refButtonD(gui, buffer);
	ImGui::SameLine();
	imGuiText("Offset {}, Stride {}", offset, stride);

	imGuiText("Count buffer:");
	ImGui::SameLine();
	refButtonD(gui, countBuffer);
	ImGui::SameLine();
	imGuiText("Offset {}", countBufferOffset);

	DrawCmdBase::displayGrahpicsState(gui, indexed);
}

void DrawIndirectCountCmd::replace(const CommandAllocHashMap<DeviceHandle*, DeviceHandle*>& map) {
	checkReplace(buffer, map);
	checkReplace(countBuffer, map);
	DrawCmdBase::replace(map);
}

float DrawIndirectCountCmd::match(const Command& base) const {
	auto* cmd = dynamic_cast<const DrawIndirectCountCmd*>(&base);
	if(!cmd) {
		return 0.f;
	}

	// hard matching on those; differences would indicate a totally
	// different command structure.
	if(cmd->indexed != this->indexed || cmd->stride != this->stride) {
		return 0.f;
	}

	auto m = doMatch(*cmd, indexed);
	if(m.total == -1.f) {
		return 0.f;
	}

	addNonNull(m, buffer, cmd->buffer);
	addNonNull(m, countBuffer, cmd->countBuffer);

	// we don't hard-match on maxDrawCount since architectures that choose
	// this dynamically per-frame (e.g. for culling) are common
	add(m, maxDrawCount, cmd->maxDrawCount);
	add(m, countBufferOffset, cmd->countBufferOffset, 0.2);
	add(m, offset, cmd->offset, 0.2);

	return eval(m);
}

// BindVertexBuffersCmd
void BindVertexBuffersCmd::record(const Device& dev, VkCommandBuffer cb) const {
	std::vector<VkBuffer> vkbuffers;
	std::vector<VkDeviceSize> vkoffsets;
	vkbuffers.reserve(buffers.size());
	vkoffsets.reserve(buffers.size());
	for(auto& b : buffers) {
		vkbuffers.push_back(b.buffer->handle);
		vkoffsets.push_back(b.offset);
	}

	dev.dispatch.CmdBindVertexBuffers(cb, firstBinding,
		u32(vkbuffers.size()), vkbuffers.data(), vkoffsets.data());
}

std::string BindVertexBuffersCmd::toString() const {
	if(buffers.size() == 1) {
		auto [buf0NameRes, buf0Name] = name(buffers[0].buffer);
		if(buf0NameRes == NameType::named) {
			return dlg::format("BindVertexBuffer({}: {})", firstBinding, buf0Name);
		} else {
			return dlg::format("BindVertexBuffer({})", firstBinding);
		}
	} else {
		return dlg::format("BindVertexBuffers({}..{})", firstBinding,
			firstBinding + buffers.size() - 1);
	}
}

void BindVertexBuffersCmd::displayInspector(Gui& gui) const {
	for(auto i = 0u; i < buffers.size(); ++i) {
		ImGui::Bullet();
		imGuiText("{}: ", firstBinding + i);
		ImGui::SameLine();
		refButtonD(gui, buffers[i].buffer);
	}
}

void BindVertexBuffersCmd::replace(const CommandAllocHashMap<DeviceHandle*, DeviceHandle*>& map) {
	for(auto& buf : buffers) {
		checkReplace(buf.buffer, map);
	}
}

// BindIndexBufferCmd
void BindIndexBufferCmd::record(const Device& dev, VkCommandBuffer cb) const {
	dev.dispatch.CmdBindIndexBuffer(cb, buffer->handle, offset, indexType);
}

void BindIndexBufferCmd::replace(const CommandAllocHashMap<DeviceHandle*, DeviceHandle*>& map) {
	checkReplace(buffer, map);
}

// BindDescriptorSetCmd
void BindDescriptorSetCmd::record(const Device& dev, VkCommandBuffer cb) const {
	auto vkds = rawHandles(sets);
	dev.dispatch.CmdBindDescriptorSets(cb, pipeBindPoint, pipeLayout->handle,
		firstSet, u32(vkds.size()), vkds.data(),
		u32(dynamicOffsets.size()), dynamicOffsets.data());
}

std::string BindDescriptorSetCmd::toString() const {
	if(sets.size() == 1) {
		auto [ds0Res, ds0Name] = name(sets[0]);
		if(ds0Res == NameType::named) {
			return dlg::format("BindDescriptorSet({}: {})", firstSet, ds0Name);
		} else {
			return dlg::format("BindDescriptorSet({})", firstSet);
		}
	} else {
		return dlg::format("BindDescriptorSets({}..{})",
			firstSet, firstSet + sets.size() - 1);
	}
}

void BindDescriptorSetCmd::displayInspector(Gui& gui) const {
	imGuiText("Bind point: {}", vk::name(pipeBindPoint));
	imGuiText("First set: {}", firstSet);

	refButtonD(gui, pipeLayout);

	// TODO: dynamic offsets

	for (auto* ds : sets) {
		ImGui::Bullet();

		if(!ds) {
			imGuiText("null or map");
		} else {
			refButton(gui, *ds);
		}
	}
}

void BindDescriptorSetCmd::replace(const CommandAllocHashMap<DeviceHandle*, DeviceHandle*>& map) {
	checkReplace(sets, map);
}

// DispatchCmdBase
DispatchCmdBase::DispatchCmdBase(CommandBuffer& cb, const ComputeState& compState) {
	state = copy(cb, compState);
	// NOTE: only do this when pipe layout matches pcr layout
	pushConstants.data = copySpan(cb, cb.pushConstants().data);
}

void DispatchCmdBase::displayComputeState(Gui& gui) const {
	refButtonD(gui, state.pipe);
}

void DispatchCmdBase::replace(const CommandAllocHashMap<DeviceHandle*, DeviceHandle*>& map) {
	checkReplace(state.pipe, map);

	// we don't ever unset descriptor sets, they are accessed via
	// snapshots anyways and we need the original pointer as key into
	// the snapshot map
}

Matcher DispatchCmdBase::doMatch(const DispatchCmdBase& cmd) const {
	// different pipelines means the draw calls are fundamentally different,
	// no matter if similar data is bound.
	if(!state.pipe || !cmd.state.pipe || state.pipe != cmd.state.pipe) {
		return Matcher::noMatch();
	}

	Matcher m;
	for(auto& pcr : state.pipe->layout->pushConstants) {
		dlg_assert_or(pcr.offset + pcr.size <= pushConstants.data.size(), continue);
		dlg_assert_or(pcr.offset + pcr.size <= cmd.pushConstants.data.size(), continue);

		m.total += pcr.size;
		if(std::memcmp(&pushConstants.data[pcr.offset],
				&cmd.pushConstants.data[pcr.offset], pcr.size) == 0u) {
			m.match += pcr.size;
		}
	}

	// - we consider the bound descriptors somewhere else since they might
	//   already have been unset from the command

	return m;
}

// DispatchCmd
void DispatchCmd::record(const Device& dev, VkCommandBuffer cb) const {
	dev.dispatch.CmdDispatch(cb, groupsX, groupsY, groupsZ);
}

std::string DispatchCmd::toString() const {
	return dlg::format("Dispatch({}, {}, {})", groupsX, groupsY, groupsZ);
}

void DispatchCmd::displayInspector(Gui& gui) const {
	imGuiText("Groups: {} {} {}", groupsX, groupsY, groupsZ);
	DispatchCmdBase::displayComputeState(gui);
}

float DispatchCmd::match(const Command& base) const {
	auto* cmd = dynamic_cast<const DispatchCmd*>(&base);
	if(!cmd) {
		return 0.f;
	}

	auto m = doMatch(*cmd);
	if(m.total == -1.f) {
		return 0.f;
	}

	// we don't hard-match on them since this may change for per-frame
	// varying workloads (in comparison to draw parameters, which rarely
	// change for per-frame stuff). The higher the dimension, the more unlikely
	// this gets though.
	add(m, groupsX, cmd->groupsX, 2.0);
	add(m, groupsY, cmd->groupsY, 4.0);
	add(m, groupsZ, cmd->groupsZ, 8.0);

	return eval(m);
}

// DispatchIndirectCmd
void DispatchIndirectCmd::record(const Device& dev, VkCommandBuffer cb) const {
	dev.dispatch.CmdDispatchIndirect(cb, buffer->handle, offset);
}

void DispatchIndirectCmd::displayInspector(Gui& gui) const {
	refButtonD(gui, buffer);
	DispatchCmdBase::displayComputeState(gui);
}

std::string DispatchIndirectCmd::toString() const {
	auto [bufNameRes, bufName] = name(buffer);
	if(bufNameRes == NameType::named) {
		return dlg::format("DispatchIndirect({})", bufName);
	}

	return "DispatchIndirect";
}

void DispatchIndirectCmd::replace(const CommandAllocHashMap<DeviceHandle*, DeviceHandle*>& map) {
	checkReplace(buffer, map);
	DispatchCmdBase::replace(map);
}

float DispatchIndirectCmd::match(const Command& base) const {
	auto* cmd = dynamic_cast<const DispatchIndirectCmd*>(&base);
	if(!cmd) {
		return 0.f;
	}

	auto m = doMatch(*cmd);
	if(m.total == -1.f) {
		return 0.f;
	}

	addNonNull(m, buffer, cmd->buffer);
	add(m, offset, cmd->offset, 0.1);

	return eval(m);
}

// DispatchBaseCmd
void DispatchBaseCmd::record(const Device& dev, VkCommandBuffer cb) const {
	auto f = dev.dispatch.CmdDispatchBase;
	f(cb, baseGroupX, baseGroupY, baseGroupZ, groupsX, groupsY, groupsZ);
}

std::string DispatchBaseCmd::toString() const {
	return dlg::format("DispatchBase({}, {}, {}, {}, {}, {})",
		baseGroupX, baseGroupY, baseGroupZ, groupsX, groupsY, groupsZ);
}

void DispatchBaseCmd::displayInspector(Gui& gui) const {
	imGuiText("Base: {} {} {}", baseGroupX, baseGroupY, baseGroupZ);
	imGuiText("Groups: {} {} {}", groupsX, groupsY, groupsZ);
	DispatchCmdBase::displayComputeState(gui);
}

float DispatchBaseCmd::match(const Command& base) const {
	auto* cmd = dynamic_cast<const DispatchBaseCmd*>(&base);
	if(!cmd) {
		return 0.f;
	}

	auto m = doMatch(*cmd);
	if(m.total == -1.f) {
		return 0.f;
	}

	// we don't hard-match on them since this may change for per-frame
	// varying workloads (in comparison to draw parameters, which rarely
	// change for per-frame stuff). The higher the dimension, the more unlikely
	// this gets though.
	add(m, groupsX, cmd->groupsX, 2.0);
	add(m, groupsY, cmd->groupsY, 4.0);
	add(m, groupsZ, cmd->groupsZ, 8.0);

	add(m, baseGroupX, cmd->baseGroupX, 2.0);
	add(m, baseGroupY, cmd->baseGroupY, 4.0);
	add(m, baseGroupZ, cmd->baseGroupZ, 8.0);

	return eval(m);
}

// CopyImageCmd
void CopyImageCmd::record(const Device& dev, VkCommandBuffer cb) const {
	if(dev.dispatch.CmdCopyImage2KHR) {
		VkCopyImageInfo2KHR info = {
			VK_STRUCTURE_TYPE_COPY_IMAGE_INFO_2_KHR,
			pNext,
			src->handle,
			srcLayout,
			dst->handle,
			dstLayout,
			u32(copies.size()),
			copies.data(),
		};

		dev.dispatch.CmdCopyImage2KHR(cb, &info);
	} else {
		dlg_assert(!pNext);
		auto copiesD = downgrade<VkImageCopy>(copies);
		dev.dispatch.CmdCopyImage(cb, src->handle, srcLayout, dst->handle, dstLayout,
			u32(copiesD.size()), copiesD.data());
	}
}

std::string CopyImageCmd::toString() const {
	auto [srcRes, srcName] = name(src);
	auto [dstRes, dstName] = name(dst);
	if(dstRes == NameType::named || srcRes == NameType::named) {
		return dlg::format("CopyImage({} -> {})", srcName, dstName);
	} else {
		return "CopyImage";
	}
}

void CopyImageCmd::replace(const CommandAllocHashMap<DeviceHandle*, DeviceHandle*>& map) {
	checkReplace(src, map);
	checkReplace(dst, map);
}

void CopyImageCmd::displayInspector(Gui& gui) const {
	refButtonD(gui, src);
	ImGui::SameLine();
	imGuiText(" -> ");
	ImGui::SameLine();
	refButtonD(gui, dst);

	ImGui::Spacing();
	imGuiText("Copies");

	for(auto& copy : copies) {
		auto srcRegion = printImageRegion(src, copy.srcOffset, copy.srcSubresource);
		auto dstRegion = printImageRegion(dst, copy.dstOffset, copy.dstSubresource);

		std::string sizeString;
		if(src && dst && src->ci.imageType == VK_IMAGE_TYPE_1D && dst->ci.imageType == VK_IMAGE_TYPE_1D) {
			sizeString = dlg::format("{}", copy.extent.width);
		} else if(src && dst && src->ci.imageType <= VK_IMAGE_TYPE_2D && dst->ci.imageType <= VK_IMAGE_TYPE_2D) {
			sizeString = dlg::format("{} x {}", copy.extent.width, copy.extent.height);
		} else {
			sizeString = dlg::format("{} x {} x {}", copy.extent.width, copy.extent.height, copy.extent.depth);
		}

		ImGui::Bullet();
		imGuiText("{} -> {} [{}]", srcRegion, dstRegion, sizeString);
	}
}

// CopyBufferToImageCmd
void CopyBufferToImageCmd::record(const Device& dev, VkCommandBuffer cb) const {
	if(dev.dispatch.CmdCopyBufferToImage2KHR) {
		VkCopyBufferToImageInfo2KHR info = {
			VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2_KHR,
			pNext,
			src->handle,
			dst->handle,
			dstLayout,
			u32(copies.size()),
			copies.data(),
		};

		dev.dispatch.CmdCopyBufferToImage2KHR(cb, &info);
	} else {
		dlg_assert(!pNext);
		auto copiesD = downgrade<VkBufferImageCopy>(copies);
		dev.dispatch.CmdCopyBufferToImage(cb, src->handle, dst->handle,
			dstLayout, u32(copiesD.size()), copiesD.data());
	}
}

void CopyBufferToImageCmd::displayInspector(Gui& gui) const {
	refButtonD(gui, src);
	ImGui::SameLine();
	imGuiText(" -> ");
	ImGui::SameLine();
	refButtonD(gui, dst);

	ImGui::Spacing();
	imGuiText("Copies");

	for(auto& copy : copies) {
		ImGui::Bullet();
		imGuiText("{}", printBufferImageCopy(dst, copy, true));
	}
}

std::string CopyBufferToImageCmd::toString() const {
	auto [srcRes, srcName] = name(src);
	auto [dstRes, dstName] = name(dst);
	if(dstRes == NameType::named || srcRes == NameType::named) {
		return dlg::format("CopyBufferToImage({} -> {})", srcName, dstName);
	} else {
		return "CopyBufferToImage";
	}
}

void CopyBufferToImageCmd::replace(const CommandAllocHashMap<DeviceHandle*, DeviceHandle*>& map) {
	checkReplace(src, map);
	checkReplace(dst, map);
}

// CopyImageToBufferCmd
void CopyImageToBufferCmd::record(const Device& dev, VkCommandBuffer cb) const {
	if(dev.dispatch.CmdCopyImageToBuffer2KHR) {
		VkCopyImageToBufferInfo2KHR info = {
			VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2_KHR,
			pNext,
			src->handle,
			srcLayout,
			dst->handle,
			u32(copies.size()),
			copies.data(),
		};

		dev.dispatch.CmdCopyImageToBuffer2KHR(cb, &info);
	} else {
		dlg_assert(!pNext);
		auto copiesD = downgrade<VkBufferImageCopy>(copies);
		dev.dispatch.CmdCopyImageToBuffer(cb, src->handle, srcLayout, dst->handle,
			u32(copiesD.size()), copiesD.data());
	}
}

void CopyImageToBufferCmd::displayInspector(Gui& gui) const {
	refButtonD(gui, src);
	ImGui::SameLine();
	imGuiText(" -> ");
	ImGui::SameLine();
	refButtonD(gui, dst);

	ImGui::Spacing();
	imGuiText("Copies");

	for(auto& copy : copies) {
		ImGui::Bullet();
		imGuiText("{}", printBufferImageCopy(src, copy, false));
	}
}

std::string CopyImageToBufferCmd::toString() const {
	auto [srcRes, srcName] = name(src);
	auto [dstRes, dstName] = name(dst);
	if(dstRes == NameType::named || srcRes == NameType::named) {
		return dlg::format("CopyImageToBuffer({} -> {})", srcName, dstName);
	} else {
		return "CopyImageToBuffer";
	}
}

void CopyImageToBufferCmd::replace(const CommandAllocHashMap<DeviceHandle*, DeviceHandle*>& map) {
	checkReplace(src, map);
	checkReplace(dst, map);
}

// BlitImageCmd
void BlitImageCmd::record(const Device& dev, VkCommandBuffer cb) const {
	if(dev.dispatch.CmdBlitImage2KHR) {
		VkBlitImageInfo2KHR info = {
			VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2_KHR,
			pNext,
			src->handle,
			srcLayout,
			dst->handle,
			dstLayout,
			u32(blits.size()),
			blits.data(),
			filter,
		};

		dev.dispatch.CmdBlitImage2KHR(cb, &info);
	} else {
		dlg_assert(!pNext);
		auto blitsD = downgrade<VkImageBlit>(blits);
		dev.dispatch.CmdBlitImage(cb, src->handle, srcLayout, dst->handle, dstLayout,
			u32(blitsD.size()), blitsD.data(), filter);
	}
}

void BlitImageCmd::displayInspector(Gui& gui) const {
	refButtonD(gui, src);
	ImGui::SameLine();
	imGuiText(" -> ");
	ImGui::SameLine();
	refButtonD(gui, dst);

	imGuiText("Filter {}", vk::name(filter));

	ImGui::Spacing();
	imGuiText("Blits");

	for(auto& blit : blits) {
		auto srcSubres = printImageSubresLayers(src, blit.srcSubresource);
		auto src0 = printImageOffset(src, blit.srcOffsets[0]);
		auto src1 = printImageOffset(src, blit.srcOffsets[1]);

		auto dstSubres = printImageSubresLayers(dst, blit.dstSubresource);
		auto dst0 = printImageOffset(dst, blit.dstOffsets[0]);
		auto dst1 = printImageOffset(dst, blit.dstOffsets[1]);

		auto srcSep = srcSubres.empty() ? "" : ": ";
		auto dstSep = dstSubres.empty() ? "" : ": ";

		ImGui::Bullet();
		imGuiText("({}{}({})..({}) -> ({}{}({})..({}))",
			srcSubres, srcSep, src0, src1,
			dstSubres, dstSep, dst0, dst1);
	}
}

std::string BlitImageCmd::toString() const {
	auto [srcRes, srcName] = name(src);
	auto [dstRes, dstName] = name(dst);
	if(dstRes == NameType::named || srcRes == NameType::named) {
		return dlg::format("BlitImage({} -> {})", srcName, dstName);
	} else {
		return "BlitImage";
	}
}

void BlitImageCmd::replace(const CommandAllocHashMap<DeviceHandle*, DeviceHandle*>& map) {
	checkReplace(src, map);
	checkReplace(dst, map);
}

// ResolveImageCmd
void ResolveImageCmd::record(const Device& dev, VkCommandBuffer cb) const {
	if(dev.dispatch.CmdResolveImage2KHR) {
		VkResolveImageInfo2KHR info = {
			VK_STRUCTURE_TYPE_RESOLVE_IMAGE_INFO_2_KHR,
			pNext,
			src->handle,
			srcLayout,
			dst->handle,
			dstLayout,
			u32(regions.size()),
			regions.data(),
		};

		dev.dispatch.CmdResolveImage2KHR(cb, &info);
	} else {
		dlg_assert(!pNext);
		auto regionsD = downgrade<VkImageResolve>(regions);
		dev.dispatch.CmdResolveImage(cb, src->handle, srcLayout,
			dst->handle, dstLayout, u32(regionsD.size()), regionsD.data());
	}
}

void ResolveImageCmd::displayInspector(Gui& gui) const {
	refButtonD(gui, src);
	ImGui::SameLine();
	imGuiText(" -> ");
	ImGui::SameLine();
	refButtonD(gui, dst);

	ImGui::Spacing();
	imGuiText("Regions");

	// Basically same as CopyImageCmd
	for(auto& copy : regions) {
		auto srcRegion = printImageRegion(src, copy.srcOffset, copy.srcSubresource);
		auto dstRegion = printImageRegion(dst, copy.dstOffset, copy.dstSubresource);

		std::string sizeString;
		if(src && dst && src->ci.imageType == VK_IMAGE_TYPE_1D && dst->ci.imageType == VK_IMAGE_TYPE_1D) {
			sizeString = dlg::format("{}", copy.extent.width);
		} else if(src && dst && src->ci.imageType <= VK_IMAGE_TYPE_2D && dst->ci.imageType <= VK_IMAGE_TYPE_2D) {
			sizeString = dlg::format("{} x {}", copy.extent.width, copy.extent.height);
		} else {
			sizeString = dlg::format("{} x {} x {}", copy.extent.width, copy.extent.height, copy.extent.depth);
		}

		ImGui::Bullet();
		imGuiText("{} -> {} [{}]", srcRegion, dstRegion, sizeString);
	}
}

std::string ResolveImageCmd::toString() const {
	auto [srcRes, srcName] = name(src);
	auto [dstRes, dstName] = name(dst);
	if(dstRes == NameType::named || srcRes == NameType::named) {
		return dlg::format("ResolveImage({} -> {})", srcName, dstName);
	} else {
		return "ResolveImage";
	}
}

void ResolveImageCmd::replace(const CommandAllocHashMap<DeviceHandle*, DeviceHandle*>& map) {
	checkReplace(src, map);
	checkReplace(dst, map);
}

// CopyBufferCmd
void CopyBufferCmd::record(const Device& dev, VkCommandBuffer cb) const {
	if(dev.dispatch.CmdCopyBuffer2KHR) {
		VkCopyBufferInfo2KHR info = {
			VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2_KHR,
			pNext,
			src->handle,
			dst->handle,
			u32(regions.size()),
			regions.data(),
		};

		dev.dispatch.CmdCopyBuffer2KHR(cb, &info);
	} else {
		dlg_assert(!pNext);
		auto regionsD = downgrade<VkBufferCopy>(regions);
		dev.dispatch.CmdCopyBuffer(cb, src->handle, dst->handle,
			u32(regionsD.size()), regionsD.data());
	}
}

void CopyBufferCmd::displayInspector(Gui& gui) const {
	refButtonD(gui, src);
	ImGui::SameLine();
	imGuiText(" -> ");
	ImGui::SameLine();
	refButtonD(gui, dst);

	ImGui::Spacing();
	imGuiText("Regions");

	for(auto& region : regions) {
		ImGui::Bullet();
		imGuiText("offsets {} -> {}, size {}", region.srcOffset, region.dstOffset, region.size);
	}
}

std::string CopyBufferCmd::toString() const {
	auto [srcRes, srcName] = name(src);
	auto [dstRes, dstName] = name(dst);
	if(dstRes == NameType::named || srcRes == NameType::named) {
		return dlg::format("CopyBuffer({} -> {})", srcName, dstName);
	} else {
		return "CopyBuffer";
	}
}

void CopyBufferCmd::replace(const CommandAllocHashMap<DeviceHandle*, DeviceHandle*>& map) {
	checkReplace(src, map);
	checkReplace(dst, map);
}

// UpdateBufferCmd
void UpdateBufferCmd::record(const Device& dev, VkCommandBuffer cb) const {
	dev.dispatch.CmdUpdateBuffer(cb, dst->handle, offset, data.size(), data.data());
}

void UpdateBufferCmd::replace(const CommandAllocHashMap<DeviceHandle*, DeviceHandle*>& map) {
	checkReplace(dst, map);
}

std::string UpdateBufferCmd::toString() const {
	auto [dstRes, dstName] = name(dst);
	if(dstRes == NameType::named) {
		return dlg::format("UpdateBuffer({})", dstName);
	} else {
		return "UpdateBuffer";
	}
}

void UpdateBufferCmd::displayInspector(Gui& gui) const {
	refButtonD(gui, dst);
	ImGui::SameLine();
	imGuiText("Offset {}", offset);

	// TODO: display data?
}

// FillBufferCmd
void FillBufferCmd::record(const Device& dev, VkCommandBuffer cb) const {
	dev.dispatch.CmdFillBuffer(cb, dst->handle, offset, size, data);
}

void FillBufferCmd::replace(const CommandAllocHashMap<DeviceHandle*, DeviceHandle*>& map) {
	checkReplace(dst, map);
}

std::string FillBufferCmd::toString() const {
	auto [dstRes, dstName] = name(dst);
	if(dstRes == NameType::named) {
		return dlg::format("FillBuffer({})", dstName);
	} else {
		return "FillBuffer";
	}
}

void FillBufferCmd::displayInspector(Gui& gui) const {
	refButtonD(gui, dst);
	ImGui::SameLine();
	imGuiText("Offset {}, Size {}", offset, size);

	imGuiText("Filled with {}{}", std::hex, data);
}

// ClearColorImageCmd
void ClearColorImageCmd::record(const Device& dev, VkCommandBuffer cb) const {
	dev.dispatch.CmdClearColorImage(cb, dst->handle, dstLayout, &color,
		u32(ranges.size()), ranges.data());
}

std::string ClearColorImageCmd::toString() const {
	auto [dstRes, dstName] = name(dst);
	if(dstRes == NameType::named) {
		return dlg::format("ClearColorImage({})", dstName);
	} else {
		return "ClearColorImage";
	}
}

void ClearColorImageCmd::replace(const CommandAllocHashMap<DeviceHandle*, DeviceHandle*>& map) {
	checkReplace(dst, map);
}

void ClearColorImageCmd::displayInspector(Gui& gui) const {
	refButtonD(gui, dst);
	// TODO: color, layout, ranges
}

// ClearDepthStencilImageCmd
void ClearDepthStencilImageCmd::record(const Device& dev, VkCommandBuffer cb) const {
	dev.dispatch.CmdClearDepthStencilImage(cb, dst->handle, dstLayout, &value,
		u32(ranges.size()), ranges.data());
}

std::string ClearDepthStencilImageCmd::toString() const {
	auto [dstRes, dstName] = name(dst);
	if(dstRes == NameType::named) {
		return dlg::format("ClearDepthStencilImage({})", dstName);
	} else {
		return "ClearColorImage";
	}
}

void ClearDepthStencilImageCmd::replace(const CommandAllocHashMap<DeviceHandle*, DeviceHandle*>& map) {
	checkReplace(dst, map);
}

void ClearDepthStencilImageCmd::displayInspector(Gui& gui) const {
	refButtonD(gui, dst);
	// TODO: value, layout, ranges
}

// Clear AttachhmentCmd
void ClearAttachmentCmd::record(const Device& dev, VkCommandBuffer cb) const {
	dev.dispatch.CmdClearAttachments(cb, u32(attachments.size()),
		attachments.data(), u32(rects.size()), rects.data());
}

void ClearAttachmentCmd::displayInspector(Gui& gui) const {
	// TODO: we probably need to refer to used render pass/fb here
	(void) gui;
}

void ClearAttachmentCmd::replace(const CommandAllocHashMap<DeviceHandle*, DeviceHandle*>& map) {
	checkReplace(rpi.fb, map);
	checkReplace(rpi.rp, map);
}

// SetEventCmd
void SetEventCmd::record(const Device& dev, VkCommandBuffer cb) const {
	dev.dispatch.CmdSetEvent(cb, event->handle, stageMask);
}

std::string SetEventCmd::toString() const {
	auto [nameRes, eventName] = name(event);
	if(nameRes == NameType::named) {
		return dlg::format("SetEvent({})", eventName);
	}

	return "SetEvent";
}

void SetEventCmd::replace(const CommandAllocHashMap<DeviceHandle*, DeviceHandle*>& map) {
	checkReplace(event, map);
}

void SetEventCmd::displayInspector(Gui& gui) const {
	refButtonD(gui, event);
	imGuiText("Stages: {}", vk::flagNames(VkPipelineStageFlagBits(stageMask)));
}

// ResetEventCmd
void ResetEventCmd::record(const Device& dev, VkCommandBuffer cb) const {
	dev.dispatch.CmdResetEvent(cb, event->handle, stageMask);
}

std::string ResetEventCmd::toString() const {
	auto [nameRes, eventName] = name(event);
	if(nameRes == NameType::named) {
		return dlg::format("ResetEvent({})", eventName);
	}

	return "ResetEvent";
}

void ResetEventCmd::replace(const CommandAllocHashMap<DeviceHandle*, DeviceHandle*>& map) {
	checkReplace(event, map);
}

void ResetEventCmd::displayInspector(Gui& gui) const {
	refButtonD(gui, event);
	imGuiText("Stages: {}", vk::flagNames(VkPipelineStageFlagBits(stageMask)));
}

// ExecuteCommandsCmd
void ExecuteCommandsCmd::record(const Device& dev, VkCommandBuffer cb) const {
	std::vector<VkCommandBuffer> vkcbs;
	auto child = children_;
	while(child) {
		auto* echild = dynamic_cast<ExecuteCommandsChildCmd*>(child);
		dlg_assert(echild);
		dlg_assert(echild->record_->cb);
		vkcbs.push_back(echild->record_->cb->handle());

		child = child->next;
	}

	dev.dispatch.CmdExecuteCommands(cb, u32(vkcbs.size()), vkcbs.data());
}

std::vector<const Command*> ExecuteCommandsCmd::display(const Command* selected,
		TypeFlags typeFlags) const {
	auto cmd = this->children_;
	auto first = static_cast<ExecuteCommandsChildCmd*>(nullptr);
	if(cmd) {
		// If we only have one subpass, don't give it an extra section
		// to make everything more compact.
		first = dynamic_cast<ExecuteCommandsChildCmd*>(cmd);
		dlg_assert(first);
		if(!first->next) {
			cmd = first->record_->commands;
		}
	}

	auto ret = ParentCommand::display(selected, typeFlags, cmd);
	if(ret.size() > 1 && cmd != this->children_) {
		ret.insert(ret.begin() + 1, first);
	}

	return ret;
}

std::string ExecuteCommandsChildCmd::toString() const {
	auto [cbRes, cbName] = name(record_->cb);
	if(cbRes == NameType::named) {
		return dlg::format("{}: {}", id_, cbName);
	} else {
		return dlg::format("{}", id_);
	}
}

void ExecuteCommandsCmd::displayInspector(Gui& gui) const {
	auto echild = dynamic_cast<ExecuteCommandsChildCmd*>(children_);
	while(echild) {
		// TODO: could link to command buffer (if still valid/linked)
		auto label = dlg::format("View Recording {}", echild->id_);
		if(ImGui::Button(label.c_str())) {
			// We can convert raw pointer into IntrusivePtr here since
			// we know that it's still alive; it's kept alive by our
			// parent CommandRecord (secondaries)
			gui.cbGui().select(IntrusivePtr<CommandRecord>(echild->record_));
			gui.activateTab(Gui::Tab::commandBuffer);
		}

		echild = dynamic_cast<ExecuteCommandsChildCmd*>(echild->next);
	}
}

// BeginDebugUtilsLabelCmd
void BeginDebugUtilsLabelCmd::record(const Device& dev, VkCommandBuffer cb) const {
	VkDebugUtilsLabelEXT label {};
	label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
	label.pLabelName = this->name;
	std::memcpy(&label.color, this->color.data(), sizeof(label.color));
	dev.dispatch.CmdBeginDebugUtilsLabelEXT(cb, &label);
}

// EndDebugUtilsLabelCmd
void EndDebugUtilsLabelCmd::record(const Device& dev, VkCommandBuffer cb) const {
	dev.dispatch.CmdEndDebugUtilsLabelEXT(cb);
}

// BindPipelineCmd
void BindPipelineCmd::record(const Device& dev, VkCommandBuffer cb) const {
	dev.dispatch.CmdBindPipeline(cb, bindPoint, pipe->handle);
}

void BindPipelineCmd::displayInspector(Gui& gui) const {
	dlg_assert(pipe->type == bindPoint);
	if(bindPoint == VK_PIPELINE_BIND_POINT_COMPUTE) {
		refButtonD(gui, static_cast<ComputePipeline*>(pipe));
	} else if(bindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS) {
		refButtonD(gui, static_cast<GraphicsPipeline*>(pipe));
	}
}

std::string BindPipelineCmd::toString() const {
	auto bp = (bindPoint == VK_PIPELINE_BIND_POINT_COMPUTE) ? "compute" : "graphics";
	auto [nameRes, pipeName] = name(pipe);
	if(nameRes == NameType::named) {
		return dlg::format("BindPipeline({}, {})", bp, pipeName);
	} else {
		return dlg::format("BindPipeline({})", bp);
	}
}

void BindPipelineCmd::replace(const CommandAllocHashMap<DeviceHandle*, DeviceHandle*>& map) {
	checkReplace(pipe, map);
}

void PushConstantsCmd::record(const Device& dev, VkCommandBuffer cb) const {
	dev.dispatch.CmdPushConstants(cb, pipeLayout->handle, stages, offset,
		u32(values.size()), values.data());
}

void SetViewportCmd::record(const Device& dev, VkCommandBuffer cb) const {
	dev.dispatch.CmdSetViewport(cb, first, u32(viewports.size()), viewports.data());
}

void SetScissorCmd::record(const Device& dev, VkCommandBuffer cb) const {
	dev.dispatch.CmdSetScissor(cb, first, u32(scissors.size()), scissors.data());
}

void SetLineWidthCmd::record(const Device& dev, VkCommandBuffer cb) const {
	dev.dispatch.CmdSetLineWidth(cb, width);
}

void SetDepthBiasCmd::record(const Device& dev, VkCommandBuffer cb) const {
	dev.dispatch.CmdSetDepthBias(cb, state.constant, state.clamp, state.slope);
}

void SetBlendConstantsCmd::record(const Device& dev, VkCommandBuffer cb) const {
	dev.dispatch.CmdSetBlendConstants(cb, values.data());
}

void SetStencilCompareMaskCmd::record(const Device& dev, VkCommandBuffer cb) const {
	dev.dispatch.CmdSetStencilCompareMask(cb, faceMask, value);
}

void SetStencilWriteMaskCmd::record(const Device& dev, VkCommandBuffer cb) const {
	dev.dispatch.CmdSetStencilWriteMask(cb, faceMask, value);
}

void SetStencilReferenceCmd::record(const Device& dev, VkCommandBuffer cb) const {
	dev.dispatch.CmdSetStencilReference(cb, faceMask, value);
}

void SetDepthBoundsCmd::record(const Device& dev, VkCommandBuffer cb) const {
	dev.dispatch.CmdSetDepthBounds(cb, min, max);
}

// BeginQuery
void BeginQueryCmd::record(const Device& dev, VkCommandBuffer cb) const {
	dev.dispatch.CmdBeginQuery(cb, pool->handle, query, flags);
}

void BeginQueryCmd::replace(const CommandAllocHashMap<DeviceHandle*, DeviceHandle*>& map) {
	checkReplace(pool, map);
}

// EndQuery
void EndQueryCmd::record(const Device& dev, VkCommandBuffer cb) const {
	dev.dispatch.CmdEndQuery(cb, pool->handle, query);
}

void EndQueryCmd::replace(const CommandAllocHashMap<DeviceHandle*, DeviceHandle*>& map) {
	checkReplace(pool, map);
}

// ResetQuery
void ResetQueryPoolCmd::record(const Device& dev, VkCommandBuffer cb) const {
	dev.dispatch.CmdResetQueryPool(cb, pool->handle, first, count);
}

void ResetQueryPoolCmd::replace(const CommandAllocHashMap<DeviceHandle*, DeviceHandle*>& map) {
	checkReplace(pool, map);
}

// WriteTimestamp
void WriteTimestampCmd::record(const Device& dev, VkCommandBuffer cb) const {
	dev.dispatch.CmdWriteTimestamp(cb, stage, pool->handle, query);
}

void WriteTimestampCmd::replace(const CommandAllocHashMap<DeviceHandle*, DeviceHandle*>& map) {
	checkReplace(pool, map);
}

// CopyQueryPool
void CopyQueryPoolResultsCmd::record(const Device& dev, VkCommandBuffer cb) const {
	dev.dispatch.CmdCopyQueryPoolResults(cb, pool->handle, first, count,
		dstBuffer->handle, dstOffset, stride, flags);
}

void CopyQueryPoolResultsCmd::replace(const CommandAllocHashMap<DeviceHandle*, DeviceHandle*>& map) {
	checkReplace(pool, map);
	checkReplace(dstBuffer, map);
}

// PushDescriptorSet
void PushDescriptorSetCmd::record(const Device& dev, VkCommandBuffer cb) const {
	dev.dispatch.CmdPushDescriptorSetKHR(cb, bindPoint, pipeLayout->handle,
		set, u32(descriptorWrites.size()), descriptorWrites.data());
}

// PushDescriptorSetWithTemplate
void PushDescriptorSetWithTemplateCmd::record(const Device& dev, VkCommandBuffer cb) const {
	dev.dispatch.CmdPushDescriptorSetWithTemplateKHR(cb, updateTemplate->handle,
		pipeLayout->handle, set, static_cast<const void*>(data.data()));
}

} // namespace vil