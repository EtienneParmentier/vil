#include <gui/vertexViewer.hpp>
#include <gui/commandHook.hpp>
#include <gui/util.hpp>
#include <util/f16.hpp>
#include <util/transform.hpp>
#include <util/util.hpp>
#include <device.hpp>
#include <shader.hpp>
#include <pipe.hpp>
#include <command/desc.hpp>
#include <command/commands.hpp>
#include <imgui/imgui.h>
#include <vk/format_utils.h>
#include <vk/enumString.hpp>
#include <spirv_reflect.h>

#include <vertices.vert.spv.h>
#include <vertices.frag.spv.h>

namespace vil {

constexpr const auto pi = 3.14159265359;
constexpr auto fov = float(0.48 * pi);
constexpr auto near = -0.01f;
constexpr auto far = -10000.f;

// util
DrawParams getDrawParams(const Command& cmd, const CommandHookState& state) {
	DrawParams ret;

	if(auto* dcmd = dynamic_cast<const DrawCmd*>(&cmd); dcmd) {
		ret.offset = dcmd->firstVertex;
		ret.drawCount = dcmd->vertexCount;
	} else if(auto* dcmd = dynamic_cast<const DrawIndexedCmd*>(&cmd); dcmd) {
		ret.offset = dcmd->firstIndex;
		ret.vertexOffset = dcmd->vertexOffset;
		ret.drawCount = dcmd->indexCount;
		ret.indexType = dcmd->state.indices.type;
	} else if(auto* dcmd = dynamic_cast<const DrawIndirectCmd*>(&cmd); dcmd) {
		// dlg_assert(hook.copyIndirectCmd);
		// dlg_assert(data->state);

		auto& ic = state.indirectCopy;
		auto span = ic.data();
		dlg_assert(!span.empty());

		if(dcmd->indexed) {
			auto ecmd = read<VkDrawIndexedIndirectCommand>(span);
			ret.offset = ecmd.firstIndex;
			ret.drawCount = ecmd.indexCount;
			ret.vertexOffset = ecmd.vertexOffset;
			ret.indexType = dcmd->state.indices.type;
		} else {
			auto ecmd = read<VkDrawIndirectCommand>(span);
			ret.offset = ecmd.firstVertex;
			ret.drawCount = ecmd.vertexCount;
		}

	} else {
		// TODO: DrawIndirectCount
		dlg_info("Vertex viewer unimplemented for command type");
		return {};
	}

	return ret;
}

template<typename T>
std::string printFormat(u32 count, const Vec4d& val) {
	auto ret = std::string {};
	auto sep = "";
	for(auto i = 0u; i < count; ++i) {
		ret += dlg::format("{}{}", sep, T(val[i]));
		sep = ", ";
	}

	return ret;
}

std::string readFormat(VkFormat format, span<const std::byte> src) {
	u32 numChannels = FormatChannelCount(format);
	u32 componentSize = FormatElementSize(format) / numChannels;

	// TODO: not all formats covered
	//   support compresssed formats! See ioFormat in util.cpp

	Vec4d val = read(format, src);

	if(FormatIsFloat(format)) {
		switch(componentSize) {
			case 2: return printFormat<f16>(numChannels, val);
			case 4: return printFormat<float>(numChannels, val);
			case 8: return printFormat<double>(numChannels, val);
			default: break;
		}
	} else if(FormatIsUInt(format) || FormatIsUScaled(format)) {
		switch(componentSize) {
			case 1: return printFormat<u8>(numChannels, val);
			case 2: return printFormat<u16>(numChannels, val);
			case 4: return printFormat<u32>(numChannels, val);
			case 8: return printFormat<u64>(numChannels, val);
			default: break;
		}
	} else if(FormatIsInt(format) || FormatIsSScaled(format)) {
		switch(componentSize) {
			case 1: return printFormat<i8>(numChannels, val);
			case 2: return printFormat<i16>(numChannels, val);
			case 4: return printFormat<i32>(numChannels, val);
			case 8: return printFormat<i64>(numChannels, val);
			default: break;
		}
	} else if(FormatIsUNorm(format)) {
		switch(componentSize) {
			case 1: return printFormat<u8> (numChannels, val);
			case 2: return printFormat<u16>(numChannels, val);
			default: break;
		}
	} else if(FormatIsSNorm(format)) {
		switch(componentSize) {
			case 1: return printFormat<i8> (numChannels, val);
			case 2: return printFormat<i16>(numChannels, val);
			default: break;
		}
	} else if(format == VK_FORMAT_E5B9G9R9_UFLOAT_PACK32) {
		auto rgb = e5b9g9r9ToRgb(read<u32>(src));
		return dlg::format("{}", rgb[0], rgb[1], rgb[2]);
	}

	dlg_warn("Format {} not supported", vk::name(format));
	return "<Unsupported format>";
}

// NOTE: this could probably be improved
bool perspectiveHeuristic(span<const Vec4f> clipSpaceVerts) {
	if(clipSpaceVerts.empty()) {
		dlg_warn("no data for orthogonal/perspective heuristic");
		return false;
	}

	float firstW = clipSpaceVerts[0][3];
	bool nonConstW = false;
	bool nonOneW = false;

	for(auto i = 0u; i < std::min<u32>(20, u32(clipSpaceVerts.size())); ++i) {
		if(std::abs(clipSpaceVerts[i][3] - 1.f) > 0.001) {
			nonOneW = true;
		}

		if(std::abs(clipSpaceVerts[i][3] - firstW) > 0.001) {
			nonConstW = true;
		}
	}

	if(nonConstW != nonOneW) {
		dlg_warn("conflicting data for orthogonal/perspective heuristic");
	}

	return nonOneW;
}

AABB3f bounds(VkFormat format, ReadBuf data, u32 stride) {
	// dlg_assert(data.size() % stride == 0u);
	dlg_assert(data.size() >= stride);

	auto inf = std::numeric_limits<float>::infinity();
	auto min = Vec3f{inf, inf, inf};
	auto max = Vec3f{-inf, -inf, -inf};

	while(data.size() >= stride) {
		auto rem = data.subspan(stride);
		auto pos = Vec3f(read(format, data));

		min = vec::cw::min(min, pos);
		max = vec::cw::max(max, pos);
		data = rem;
	}

	// can probaby happen due to copied buffer truncation
	dlg_assertm(data.empty(), "Unexpected (unaligned) amount of vertex data");

	AABB3f ret;
	ret.pos = 0.5f * (min + max);
	ret.extent = 0.5f * (max - min);

	return ret;
}

u32 indexSize(VkIndexType type) {
	// NOTE: When extending here, also extend readIndex
	switch(type) {
		case VK_INDEX_TYPE_UINT16: return 2;
		case VK_INDEX_TYPE_UINT32: return 4;
		case VK_INDEX_TYPE_UINT8_EXT: return 1;
		case VK_INDEX_TYPE_MAX_ENUM:
		case VK_INDEX_TYPE_NONE_KHR:
			return 0;
	}

	return 0;
}

u32 readIndex(VkIndexType type, ReadBuf& data) {
	switch(type) {
		case VK_INDEX_TYPE_UINT16: return read<u16>(data);
		case VK_INDEX_TYPE_UINT32: return read<u32>(data);
		case VK_INDEX_TYPE_UINT8_EXT: return read<u8>(data);
		case VK_INDEX_TYPE_MAX_ENUM:
		case VK_INDEX_TYPE_NONE_KHR:
			dlg_error("invalid index type");
			return 0;
	}

	dlg_error("invalid index type");
	return 0;
}

AABB3f bounds(VkFormat vertFormat, ReadBuf vertData, u32 vertStride,
		VkIndexType indexType, ReadBuf indexData) {
	auto indSize = indexSize(indexType);
	dlg_assert(indSize > 0);
	dlg_assert(indexData.size() % indSize == 0u);

	auto inf = std::numeric_limits<float>::infinity();
	auto min = Vec3f{inf, inf, inf};
	auto max = Vec3f{-inf, -inf, -inf};

	while(indexData.size() >= indSize) {
		auto ind = readIndex(indexType, indexData);
		dlg_assert_or(ind * vertStride < vertData.size(), continue);

		auto vertBuf = vertData.subspan(ind * vertStride);
		auto pos = Vec3f(read(vertFormat, vertBuf));

		min = vec::cw::min(min, pos);
		max = vec::cw::max(max, pos);
	}

	AABB3f ret;
	ret.pos = 0.5f * (min + max);
	ret.extent = 0.5f * (max - min);

	return ret;
}

AABB3f bounds(span<const Vec4f> points, bool useW) {
	auto inf = std::numeric_limits<float>::infinity();
	auto min = Vec3f{inf, inf, inf};
	auto max = Vec3f{-inf, -inf, -inf};

	for(auto& point : points) {
		Vec3f pos3 = Vec3f(point);
		if(useW) {
			pos3.z = point[3];
		}

		min = vec::cw::min(min, pos3);
		max = vec::cw::max(max, pos3);
	}

	AABB3f ret;
	ret.pos = 0.5f * (min + max);
	ret.extent = 0.5f * (max - min);

	return ret;
}

// VertexViewer
VertexViewer::~VertexViewer() {
	if(!dev_) {
		return;
	}

	for(auto& pipe : pipes_) {
		dev_->dispatch.DestroyPipeline(dev_->handle, pipe.pipe, nullptr);
	}

	dev_->dispatch.DestroyPipelineLayout(dev_->handle, pipeLayout_, nullptr);
	dev_->dispatch.DestroyShaderModule(dev_->handle, vertShader_, nullptr);
	dev_->dispatch.DestroyShaderModule(dev_->handle, fragShader_, nullptr);
}

void VertexViewer::init(Device& dev, VkRenderPass rp) {
	dev_ = &dev;
	rp_ = rp;

	// pipeline layout
	// We just allocate the full push constant range that all implementations
	// must support.
	VkPushConstantRange pcrs[1] = {};
	pcrs[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	pcrs[0].offset = 0;
	pcrs[0].size = 128;

	VkPipelineLayoutCreateInfo plci {};
	plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	// plci.setLayoutCount = 1;
	// plci.pSetLayouts = &dsLayout_;
	plci.pushConstantRangeCount = 1;
	plci.pPushConstantRanges = pcrs;
	VK_CHECK(dev.dispatch.CreatePipelineLayout(dev.handle, &plci, nullptr, &pipeLayout_));
	nameHandle(dev, this->pipeLayout_, "VertexViewer:pipeLayout");

	// load shaders
	VkShaderModuleCreateInfo vertShaderInfo {};
	vertShaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	vertShaderInfo.codeSize = sizeof(vertices_vert_spv_data);
	vertShaderInfo.pCode = vertices_vert_spv_data;
	VK_CHECK(dev.dispatch.CreateShaderModule(dev.handle, &vertShaderInfo, NULL, &vertShader_));

	VkShaderModuleCreateInfo fragShaderInfo {};
	fragShaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	fragShaderInfo.codeSize = sizeof(vertices_frag_spv_data);
	fragShaderInfo.pCode = vertices_frag_spv_data;
	VK_CHECK(dev.dispatch.CreateShaderModule(dev.handle, &fragShaderInfo, NULL, &fragShader_));

}

VkPipeline VertexViewer::createPipe(VkFormat format, u32 stride,
		VkPrimitiveTopology topology) {
	auto& dev = *this->dev_;

	// store them for destruction later on
	std::array<VkPipelineShaderStageCreateInfo, 2> stages {};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vertShader_;
	stages[0].pName = "main";

	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = fragShader_;
	stages[1].pName = "main";

	VkVertexInputBindingDescription bindingDesc[1] = {};
	bindingDesc[0].stride = stride;
	bindingDesc[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription attribDesc[1] = {};
	attribDesc[0].location = 0;
	attribDesc[0].binding = bindingDesc[0].binding;
	attribDesc[0].format = format;
	attribDesc[0].offset = 0;

	VkPipelineVertexInputStateCreateInfo vertexInfo {};
	vertexInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInfo.vertexBindingDescriptionCount = 1;
	vertexInfo.pVertexBindingDescriptions = bindingDesc;
	vertexInfo.vertexAttributeDescriptionCount = 1;
	vertexInfo.pVertexAttributeDescriptions = attribDesc;

	VkPipelineInputAssemblyStateCreateInfo iaInfo {};
	iaInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	iaInfo.topology = topology;

	VkPipelineViewportStateCreateInfo viewportInfo {};
	viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportInfo.viewportCount = 1;
	viewportInfo.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rasterInfo {};
	rasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	// TODO: Allow to toggle? Or draw both?
	rasterInfo.polygonMode = dev.nonSolidFill ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
	rasterInfo.cullMode = VK_CULL_MODE_NONE;
	rasterInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterInfo.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo msInfo {};
	msInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	msInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState colorAttach[1] {};
	colorAttach[0].blendEnable = VK_TRUE;
	colorAttach[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	colorAttach[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	colorAttach[0].colorBlendOp = VK_BLEND_OP_ADD;
	colorAttach[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorAttach[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorAttach[0].alphaBlendOp = VK_BLEND_OP_ADD;
	colorAttach[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
		VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineDepthStencilStateCreateInfo depthInfo {};
	depthInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

	VkPipelineColorBlendStateCreateInfo blendInfo {};
	blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blendInfo.attachmentCount = 1;
	blendInfo.pAttachments = colorAttach;

	VkPipelineDepthStencilStateCreateInfo depthStencil {};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = true;
	depthStencil.depthWriteEnable = true;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	depthStencil.stencilTestEnable = false;

	VkDynamicState dynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynState {};
	dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynState.dynamicStateCount = 2;
	dynState.pDynamicStates = dynStates;

	VkGraphicsPipelineCreateInfo gpi[1] {};

	gpi[0].sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	gpi[0].flags = 0;
	gpi[0].stageCount = u32(stages.size());
	gpi[0].pStages = stages.data();
	gpi[0].pVertexInputState = &vertexInfo;
	gpi[0].pInputAssemblyState = &iaInfo;
	gpi[0].pViewportState = &viewportInfo;
	gpi[0].pRasterizationState = &rasterInfo;
	gpi[0].pMultisampleState = &msInfo;
	gpi[0].pDepthStencilState = &depthInfo;
	gpi[0].pColorBlendState = &blendInfo;
	gpi[0].pDepthStencilState = &depthStencil;
	gpi[0].pDynamicState = &dynState;
	gpi[0].layout = dev.renderData->pipeLayout;
	gpi[0].renderPass = rp_;
	gpi[0].flags = VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;

	VkPipeline pipe;
	VK_CHECK(dev.dispatch.CreateGraphicsPipelines(dev.handle,
		VK_NULL_HANDLE, 1, gpi, nullptr, &pipe));

	auto& ourPipe = pipes_.emplace_back();
	ourPipe.pipe = pipe;
	ourPipe.topology = topology;
	ourPipe.format = format;
	ourPipe.stride = stride;

	return pipe;
}

void VertexViewer::imGuiDraw(const DrawData& data) {
	auto& dev = *this->dev_;
	auto cb = data.cb;

	// try to find position by heuristics
	// TODO: cache this! But should likely not be implemented here in first place.
	// TODO: implement a serious heuristic. Inspect the spv code,
	//   and try to find which input influences the Position output
	if(data.vertexInfo.vertexAttributeDescriptionCount == 0u) {
		dlg_info("Can't display vertices, no vertex attribs");
		return;
	}

	dlg_assert(data.vertexInfo.vertexBindingDescriptionCount > 0);

	auto& attrib = data.vertexInfo.pVertexAttributeDescriptions[0];
	auto& binding = data.vertexInfo.pVertexBindingDescriptions[attrib.binding];

	dlg_assert_or(binding.binding < data.vertexBuffers.size(), return);
	auto& vbuf = data.vertexBuffers[binding.binding];
	auto voffset = VkDeviceSize(vbuf.offset + attrib.offset);

	// try to find matching pipeline
	VkPipeline foundPipe {};
	for(auto& pipe : pipes_) {
		if(pipe.format == attrib.format &&
				pipe.stride == binding.stride &&
				pipe.topology == data.topology) {
			foundPipe = pipe.pipe;
		}
	}

	if(!foundPipe) {
		foundPipe = createPipe(attrib.format, binding.stride, data.topology);
	}

	auto displaySize = ImGui::GetIO().DisplaySize;

	VkRect2D scissor {};
	scissor.offset.x = std::max<int>(data.canvasOffset.x, 0);
	scissor.offset.y = std::max<int>(data.canvasOffset.y, 0);
	scissor.extent.width = std::min<int>(
		data.canvasSize.x + data.canvasOffset.x - scissor.offset.x,
		displaySize.x - data.canvasOffset.x);
	scissor.extent.height = std::min<int>(
		data.canvasSize.y + data.canvasOffset.y - scissor.offset.y,
		displaySize.y - data.canvasOffset.y);
	dev.dispatch.CmdSetScissor(cb, 0, 1, &scissor);

	VkViewport viewport {};
	viewport.width = scissor.extent.width;
	viewport.height = scissor.extent.height;
	viewport.x = scissor.offset.x;
	viewport.y = scissor.offset.y;
	viewport.maxDepth = 1.f;
	dev.dispatch.CmdSetViewport(cb, 0, 1, &viewport);

	dev.dispatch.CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, foundPipe);

	struct {
		Mat4f matrix;
		u32 useW;
		float scale;
	} pcData = {
		viewProjMtx_,
		data.useW,
		data.scale,
	};

	dev.dispatch.CmdPushConstants(cb, pipeLayout_,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		0, sizeof(pcData), &pcData);

	dev.dispatch.CmdBindVertexBuffers(cb, 0, 1, &vbuf.buffer, &voffset);

	// clear canvas color
	VkClearAttachment clearAtt {};
	clearAtt.clearValue.color = {0.f, 0.f, 0.f, 1.f};
	clearAtt.colorAttachment = 0u;
	clearAtt.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

	VkClearRect clearRect {};
	clearRect.rect = scissor;
	clearRect.layerCount = 1u;

	dev.dispatch.CmdClearAttachments(cb, 1u, &clearAtt, 1u, &clearRect);

	if(data.params.indexType) {
		dlg_assert_or(data.indexBuffer.buffer, return);
		dlg_assert_or(data.indexBuffer.size, return);

		dev.dispatch.CmdBindIndexBuffer(cb, data.indexBuffer.buffer,
			0, *data.params.indexType);
		dev.dispatch.CmdDrawIndexed(cb, data.params.drawCount, 1,
			data.params.offset, data.params.vertexOffset, 0);
	} else {
		dev.dispatch.CmdDraw(cb, data.params.drawCount, 1, data.params.offset, 0);
	}
}

void VertexViewer::updateInput(float dt) {
	// update input
	auto& io = ImGui::GetIO();

	if(ImGui::IsItemHovered()) {
		if(io.MouseDown[0]) {
			auto mousePos = Vec2f{io.MousePos.x, io.MousePos.y};
			if(rotating_) {
				auto delta = mousePos - lastMousPos_;
				(void) delta; // TODO: move cam

				constexpr const auto fac = 0.005f;
				constexpr const auto limitPitch = true;

				yaw_ = std::fmod(yaw_ - fac * delta.x, 2 * pi);
				pitch_ -= fac * delta.y;

				if(limitPitch) {
					auto pitchEps = 0.05;
					pitch_ = std::clamp<float>(pitch_, -pi / 2 + pitchEps, pi / 2 - pitchEps);
				}

				cam_.rot = Quaternion::yxz(yaw_, pitch_, 0.f);
			}

			lastMousPos_ = mousePos;
		}

		rotating_ = io.MouseDown[0];
	}

	if(ImGui::IsItemFocused()) {
		auto right = apply(cam_.rot, Vec3f{1.f, 0.f, 0.f});
		auto up = Vec3f{0.f, 1.f, 0.f};
		auto fwd = apply(cam_.rot, Vec3f{0.f, 0.f, -1.f});
		Vec3f accel {};

		constexpr auto moveRight = swa_key_d;
		constexpr auto moveLeft = swa_key_a;
		constexpr auto moveFwd = swa_key_w;
		constexpr auto moveBwd = swa_key_s;
		constexpr auto moveUp = swa_key_q;
		constexpr auto moveDown = swa_key_e;

		if(io.KeysDown[moveRight]) {
			accel += right;
		}
		if(io.KeysDown[moveLeft]) {
			accel += -right;
		}
		if(io.KeysDown[moveFwd]) {
			accel += fwd;
		}
		if(io.KeysDown[moveBwd]) {
			accel += -fwd;
		}
		if(io.KeysDown[moveUp]) {
			accel += up;
		}
		if(io.KeysDown[moveDown]) {
			accel += -up;
		}

		auto fac = speed_;
		if(io.KeyShift) {
			fac *= 5.f;
		}
		if(io.KeyCtrl) {
			fac *= 0.1f;
		}

		accel *= fac;
		cam_.pos += dt * accel;

		// TODO: not sure this is the right way to do this.
		// need to inform application that we have captured keyboard
		// input right now (when input comes from application)
		io.WantCaptureKeyboard = true;
	}

	auto rect = ImGui::GetItemRectSize();
	auto aspect = rect.x / rect.y;

	auto projMtx = perspective(fov, aspect, near, far);
	flipY(projMtx);

	auto viewMtx = viewMatrix(cam_);
	viewProjMtx_ = projMtx * viewMtx;
}

void VertexViewer::displayInput(Draw& draw, const DrawCmdBase& cmd,
		const CommandHookState& state, float dt) {
	// TODO: display binding information
	// TODO: how to display indices?
	// TODO: only show vertex range used for draw call

	dlg_assert_or(cmd.state.pipe, return);
	if(state.vertexBufCopies.size() < cmd.state.pipe->vertexBindings.size()) {
		if(!state.errorMessage.empty()) {
			imGuiText("Error: {}", state.errorMessage);
		} else {
			ImGui::Text("Error: not enough vertex buffers bound");
		}

		return;
	}


	auto& pipe = *cmd.state.pipe;

	SpvReflectShaderModule* vertStage = nullptr;
	for(auto& stage : pipe.stages) {
		if(stage.stage == VK_SHADER_STAGE_VERTEX_BIT) {
			vertStage = &nonNull(nonNull(stage.spirv).reflection);
			break;
		}
	}

	if(!vertStage) {
		// TODO: yeah this can happen with mesh shaders now
		ImGui::Text("Grahpics Pipeline has no vertex stage :o");
		return;
	}

	// match bindings to input variables into
	// (pipe.vertexAttrib, vertStage->input_variables) id pairs
	std::vector<std::pair<u32, u32>> attribs;
	for(auto a = 0u; a < pipe.vertexAttribs.size(); ++a) {
		auto& attrib = pipe.vertexAttribs[a];
		for(auto i = 0u; i < vertStage->input_variable_count; ++i) {
			auto& iv = *vertStage->input_variables[i];
			if(iv.location == attrib.location) {
				attribs.push_back({a, i});
			}
		}
	}

	// TODO sort attribs by input location?

	auto flags = ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable;
	if(ImGui::BeginChild("vertexTable", {0.f, 200.f})) {
		if(attribs.empty()) {
			ImGui::Text("No Vertex input");
		} else if(ImGui::BeginTable("Vertices", int(attribs.size()), flags)) {
			for(auto& attrib : attribs) {
				auto& iv = *vertStage->input_variables[attrib.second];
				ImGui::TableSetupColumn(iv.name);
			}

			ImGui::TableHeadersRow();
			ImGui::TableNextRow();

			auto finished = false;
			auto id = 0u;
			while(!finished && id < 100) {
				for(auto& [aID, _] : attribs) {
					auto& attrib = pipe.vertexAttribs[aID];
					ImGui::TableNextColumn();

					auto& binding = pipe.vertexBindings[attrib.binding];
					auto& buf = state.vertexBufCopies[attrib.binding];
					auto off = binding.inputRate == VK_VERTEX_INPUT_RATE_VERTEX ?
						id * binding.stride : 0u;
					off += attrib.offset;

					// TODO: compressed support?
					auto size = FormatElementSize(attrib.format);

					if(off + size > buf.data().size()) {
						finished = true;
						break;
					}

					auto* ptr = buf.data().data() + off;
					auto str = readFormat(attrib.format, {ptr, size});

					imGuiText("{}", str);
				}

				++id;
				ImGui::TableNextRow();
			}

			ImGui::EndTable();
		}
	}

	ImGui::EndChild();

	// 2: viewer
	auto params = getDrawParams(cmd, state);

	if(ImGui::Button("Recenter")) {
		auto& attrib = pipe.vertexAttribs[0];
		auto& binding = pipe.vertexBindings[attrib.binding];

		AABB3f vertBounds;
		auto vertData = state.vertexBufCopies[binding.binding].data();
		vertData = vertData.subspan(attrib.offset);
		if(params.indexType) {
			vertData = vertData.subspan(params.vertexOffset * binding.stride);
			auto indData = state.indexBufCopy.data();
			auto offset = indexSize(*params.indexType) * params.offset;
			auto size = indexSize(*params.indexType) * params.drawCount;
			indData = indData.subspan(offset, size);
			vertBounds = bounds(attrib.format, vertData, binding.stride,
				*params.indexType, indData);
		} else {
			auto offset = params.offset * binding.stride;
			auto size = params.drawCount * binding.stride;
			vertData = vertData.subspan(offset, size);
			vertBounds = bounds(attrib.format, vertData, binding.stride);
		}

		speed_ = vertBounds.extent.x + vertBounds.extent.y + vertBounds.extent.z;
		centerCamOnBounds(vertBounds);
	}

	if(ImGui::BeginChild("vertexViewer")) {
		auto avail = ImGui::GetContentRegionAvail();
		auto pos = ImGui::GetCursorScreenPos();

		drawData_.cb = draw.cb;
		drawData_.params = params;
		drawData_.vertexInfo = pipe.vertexInputState;
		drawData_.canvasOffset = {pos.x, pos.y};
		drawData_.canvasSize = {avail.x, avail.y};
		drawData_.topology = pipe.inputAssemblyState.topology;
		drawData_.vertexBuffers = {};
		drawData_.scale = 1.f;
		drawData_.useW = false;

		for(auto& buf : state.vertexBufCopies) {
			drawData_.vertexBuffers.push_back({buf.buffer.buf, 0u, buf.buffer.size});
		}

		if(params.indexType) {
			drawData_.indexBuffer = {state.indexBufCopy.buffer.buf, 0u, state.indexBufCopy.buffer.size};
		}

		auto cb = [](const ImDrawList*, const ImDrawCmd* cmd) {
			auto* self = static_cast<VertexViewer*>(cmd->UserCallbackData);
			self->imGuiDraw(self->drawData_);
		};

		ImGui::GetWindowDrawList()->AddCallback(cb, this);
		ImGui::InvisibleButton("Canvas", avail);
		updateInput(dt);

		dlg_assert(!draw.usedHookState);
		draw.usedHookState = IntrusivePtr<CommandHookState>(const_cast<CommandHookState*>(&state));
	}

	ImGui::EndChild();
}

u32 topologyOutputCount(VkPrimitiveTopology topo, i32 in) {
	switch(topo) {
		case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
		case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
		case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
			return u32(in);

		case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
			return u32(2 * std::max(0, in - 1));

		case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
			return u32(in / 2);
		case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY:
			return u32(2 * std::max(0, in - 3));

		case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
		case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
			return u32(3 * std::max(0, in - 2));

		case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY:
			return u32(in / 2);
		case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY:
		case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST:
			dlg_warn("Not implemeneted"); // ugh, no idea
			return 0u;

		default:
			dlg_error("Invalid topology {}", u32(topo));
			return 0u;
	}
}

void VertexViewer::displayOutput(Draw& draw, const DrawCmdBase& cmd,
		const CommandHookState& state, float dt) {
	dlg_assert_or(cmd.state.pipe, return);

	if(!cmd.state.pipe->xfbPatched) {
		imGuiText("Error: couldn't inject transform feedback code to shader");
		return;
	} else if(!state.transformFeedback.buffer.size) {
		if(!state.errorMessage.empty()) {
			imGuiText("Transform feedback error: {}", state.errorMessage);
		} else {
			ImGui::Text("Error: no transform feedback");
		}

		return;
	}

	u32 vertexCount {};
	if(auto* dcmd = dynamic_cast<const DrawCmd*>(&cmd); dcmd) {
		vertexCount = dcmd->vertexCount * dcmd->instanceCount;
	} else if(auto* dcmd = dynamic_cast<const DrawIndexedCmd*>(&cmd); dcmd) {
		vertexCount = dcmd->indexCount * dcmd->instanceCount;
	} else if(auto* dcmd = dynamic_cast<const DrawIndirectCmd*>(&cmd); dcmd) {
		dlg_assert(dev_->commandHook->copyIndirectCmd);
		dlg_assert(state.indirectCopy.buffer.size);

		auto& ic = state.indirectCopy;
		auto span = ic.data();
		if(dcmd->indexed) {
			auto ecmd = read<VkDrawIndexedIndirectCommand>(span);
			vertexCount = ecmd.indexCount * ecmd.instanceCount;
		} else {
			auto ecmd = read<VkDrawIndirectCommand>(span);
			vertexCount = ecmd.vertexCount * ecmd.instanceCount;
		}

	} else {
		// TODO: DrawIndirectCount
		imGuiText("Vertex viewer unimplemented for command type");
		return;
	}

	vertexCount = topologyOutputCount(cmd.state.pipe->inputAssemblyState.topology, vertexCount);
	vertexCount = std::min(vertexCount, u32(state.transformFeedback.buffer.size / 16u));

	// 1: table
	auto flags = ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable;
	if(ImGui::BeginChild("vertexTable", {0.f, 200.f})) {
		if(ImGui::BeginTable("Vertices", 1, flags)) {
			ImGui::TableSetupColumn("Builtin Position");
			ImGui::TableHeadersRow();
			ImGui::TableNextRow();

			auto xfbData = state.transformFeedback.data();
			auto vCount = std::min(vertexCount, u32(xfbData.size() / 16u));

			for(auto i = 0u; i < std::min(u32(100u), vCount); ++i) {
				ImGui::TableNextColumn();

				auto pos = read<Vec4f>(xfbData);
				imGuiText("{}", pos);

				ImGui::TableNextRow();
			}

			ImGui::EndTable();
		}
	}

	ImGui::EndChild();

	// 2: viewer
	// NOTE: strictly speaking the reinterepret_cast is UB but it's
	// all just trivial types so who cares
	auto bspan = state.transformFeedback.data();
	auto ptr = reinterpret_cast<const Vec4f*>(bspan.data());
	auto verts = span{ptr, vertexCount};

	// TODO: don't evaluate this every frame, just in the beginning
	// and when the Recenter button is pressed.
	const bool useW = perspectiveHeuristic(verts);

	if(ImGui::Button("Recenter")) {
		auto vertBounds = bounds(verts, useW);
		speed_ = vertBounds.extent.x + vertBounds.extent.y + vertBounds.extent.z;
		centerCamOnBounds(vertBounds);
	}

	if(ImGui::BeginChild("vertexViewer")) {
		auto avail = ImGui::GetContentRegionAvail();
		auto pos = ImGui::GetCursorScreenPos();

		const static VkVertexInputBindingDescription vertexBinding {
			0u, sizeof(Vec4f), VK_VERTEX_INPUT_RATE_VERTEX,
		};

		const static VkVertexInputAttributeDescription vertexAttrib {
			0u, 0u, VK_FORMAT_R32G32B32A32_SFLOAT, 0u,
		};

		auto& xfbBuf = state.transformFeedback.buffer;
		drawData_.cb = draw.cb;
		drawData_.params = {};
		drawData_.params.drawCount = vertexCount;
		drawData_.indexBuffer = {};
		drawData_.vertexBuffers = {{{xfbBuf.buf, 0u, xfbBuf.size}}};
		drawData_.vertexInfo.vertexAttributeDescriptionCount = 1u;
		drawData_.vertexInfo.pVertexAttributeDescriptions = &vertexAttrib;
		drawData_.vertexInfo.vertexBindingDescriptionCount = 1u;
		drawData_.vertexInfo.pVertexBindingDescriptions = &vertexBinding;
		drawData_.canvasOffset = {pos.x, pos.y};
		drawData_.canvasSize = {avail.x, avail.y};
		drawData_.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		drawData_.useW = useW;
		drawData_.scale = 1.f; // 100.f; // TODO

		auto cb = [](const ImDrawList*, const ImDrawCmd* cmd) {
			auto* self = static_cast<VertexViewer*>(cmd->UserCallbackData);
			self->imGuiDraw(self->drawData_);
		};

		ImGui::GetWindowDrawList()->AddCallback(cb, this);
		ImGui::InvisibleButton("Canvas", avail);
		updateInput(dt);

		// we read from the buffer that is potentially written again
		// by the hook so we need barriers.
		dlg_assert(!draw.usedHookState);
		draw.usedHookState = IntrusivePtr<CommandHookState>(const_cast<CommandHookState*>(&state));
	}

	ImGui::EndChild();
}

void VertexViewer::centerCamOnBounds(const AABB3f& bounds) {
	auto mxy = std::max(bounds.extent.y, bounds.extent.x);
	auto l = mxy / std::tan(0.5f * fov);

	// cam looks along -z by default
	cam_.pos = bounds.pos + Vec3f{0.f, 0.f, bounds.extent.z} + l * Vec3f{0.f, 0.f, 1.f};
	cam_.rot = Quaternion {};

	yaw_ = {};
	pitch_ = {};
}

} // namespace vil
