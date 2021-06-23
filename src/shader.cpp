#include <shader.hpp>
#include <device.hpp>
#include <data.hpp>
#include <util/spirv.hpp>
#include <util/util.hpp>
#include <vk/enumString.hpp>
#include <util/spirv_reflect.h>
#include <spirv-cross/spirv_cross.hpp>

namespace vil {

// util
std::string extractString(span<const u32> spirv) {
	std::string ret;
	for(auto w : spirv) {
		for (auto j = 0u; j < 4; j++, w >>= 8) {
			char c = w & 0xff;
			if(c == '\0') {
				return ret;
			}

			ret += c;
		}
	}

	dlg_error("Unterminated SPIR-V string");
	return {};
}

ShaderSpecialization createShaderSpecialization(const VkSpecializationInfo* info) {
	ShaderSpecialization ret;
	if(!info) {
		return ret;
	}

	auto data = static_cast<const std::byte*>(info->pData);
	ret.entries = {info->pMapEntries, info->pMapEntries + info->mapEntryCount};
	ret.data = {data, data + info->dataSize};
	return ret;
}

bool operator==(const ShaderSpecialization& a, const ShaderSpecialization& b) {
	if(a.entries.size() != b.entries.size()) {
		return false;
	}

	// since they have the same number of entries, for equality we only
	// have to show that each entry in a has an equivalent in b.
	for(auto& ea : a.entries) {
		auto found = false;
		for(auto& eb : b.entries) {
			if(ea.constantID != eb.constantID) {
				continue;
			}

			if(ea.size == eb.size) {
				dlg_assert(ea.offset + ea.size <= a.data.size());
				dlg_assert(eb.offset + eb.size <= b.data.size());

				// NOTE: keep in mind this might be more strict than
				// an equality check e.g. for floating point NaNs. But that
				// shouldn't be a problem for any use of this function.
				auto cmp = std::memcmp(
					a.data.data() + ea.offset,
					b.data.data() + eb.offset,
					ea.size);
				found = (cmp == 0u);
			}

			break;
		}

		if(!found) {
			return false;
		}
	}

	return true;
}

bool isOpInSection8(spv11::Op op) {
	switch(op) {
		case spv11::Op::OpDecorate:
		case spv11::Op::OpMemberDecorate:
		case spv11::Op::OpDecorationGroup:
		case spv11::Op::OpGroupDecorate:
		case spv11::Op::OpGroupMemberDecorate:
		case spv11::Op::OpDecorateId:
		case spv11::Op::OpDecorateString:
		case spv11::Op::OpMemberDecorateString:
			return true;
		default:
			return false;
	}
}

u32 baseTypeSize(const spc::SPIRType& type, XfbCapture& cap) {
	dlg_assert(!type.pointer);

	switch(type.basetype) {
		case spc::SPIRType::Float:
		case spc::SPIRType::Half:
		case spc::SPIRType::Double:
			cap.type = XfbCapture::typeFloat;
			break;

		case spc::SPIRType::UInt:
		case spc::SPIRType::UInt64:
		case spc::SPIRType::UByte:
		case spc::SPIRType::UShort:
			cap.type = XfbCapture::typeUint;
			break;

		case spc::SPIRType::Int:
		case spc::SPIRType::Int64:
		case spc::SPIRType::SByte:
		case spc::SPIRType::Short:
			cap.type = XfbCapture::typeInt;
			break;

		default:
			dlg_assert("Invalid type");
			return u32(-1);
	}

	cap.width = type.width;
	cap.columns = type.columns;
	cap.vecsize = type.vecsize;

	return type.vecsize * type.columns * (type.width / 8);
}

void addDeco(std::vector<u32>& newDecos, u32 target, spv11::Decoration deco, u32 value) {
	newDecos.push_back((4u << 16) | u32(spv11::Op::OpDecorate));
	newDecos.push_back(target);
	newDecos.push_back(u32(deco));
	newDecos.push_back(value);
}

void addMemberDeco(std::vector<u32>& newDecos, u32 structType, u32 member, spv11::Decoration deco, u32 value) {
	newDecos.push_back((5u << 16) | u32(spv11::Op::OpMemberDecorate));
	newDecos.push_back(structType);
	newDecos.push_back(member);
	newDecos.push_back(u32(deco));
	newDecos.push_back(value);
}

void annotateCapture(const spc::Compiler& compiler, const spc::SPIRType& structType,
		const std::string& name, u32& bufOffset, std::vector<XfbCapture>& captures,
		std::vector<u32>& newDecos) {
	for(auto i = 0u; i < structType.member_types.size(); ++i) {
		auto& member = structType.member_types[i];
		auto& mtype = compiler.get_type(member);

		auto memberName = name;
		auto mname = compiler.get_member_name(structType.self, i);
		if(!mname.empty()) {
			memberName += mname;
		} else {
			memberName += std::to_string(i);
		}

		if(mtype.basetype == spc::SPIRType::Struct) {
			memberName += ".";
			annotateCapture(compiler, mtype, memberName, bufOffset, captures, newDecos);
			continue;
		}

		XfbCapture cap {};
		auto baseSize = baseTypeSize(mtype, cap);
		if(baseSize == u32(-1)) {
			continue;
		}


		if(compiler.has_member_decoration(structType.self, i, spv::DecorationBuiltIn)) {
			// filter out unwritten builtins
			auto builtin = compiler.get_member_decoration(structType.self, i, spv::DecorationBuiltIn);
			if(!compiler.has_active_builtin(spv::BuiltIn(builtin), spv::StorageClassOutput)) {
				continue;
			}
			cap.builtin = builtin;
		}

		auto size = baseSize;
		for(auto j = 0u; j < mtype.array.size(); ++j) {
			auto dim = mtype.array[j];
			if(!mtype.array_size_literal[j]) {
				dim = compiler.evaluate_constant_u32(dim);
			}

			cap.array.push_back(dim);
			size *= dim;
		}

		if(!compiler.has_member_decoration(structType.self, i, spv::DecorationArrayStride) &&
				!mtype.array.empty()) {
			addMemberDeco(newDecos, structType.self, i, spv11::Decoration::ArrayStride, baseSize);
		}

		dlg_assert_or(!compiler.has_member_decoration(structType.self, i, spv::DecorationOffset), continue);
		// TODO: have to align offset properly for 64-bit types
		addMemberDeco(newDecos, structType.self, i, spv11::Decoration::Offset, bufOffset);

		cap.name = memberName;
		cap.offset = bufOffset;

		captures.push_back(std::move(cap));
		bufOffset += size;
	}
}

XfbPatchRes patchSpirvXfb(span<const u32> spirv,
		const char* entryPoint, const ShaderSpecialization& spec) {
	// parse spirv
	if(spirv.size() < 5) {
		dlg_error("spirv to small");
		return {};
	}

	if(spirv[0] != 0x07230203) {
		dlg_error("Invalid spirv magic number. Endianess troubles?");
		return {};
	}

	std::vector<u32> patched;
	patched.reserve(spirv.size());
	patched.resize(5);
	std::copy(spirv.begin(), spirv.begin() + 5, patched.begin());

	// yeah, our hashing is terrible. But we only use it for debug output
	u32 hash = 0u;

	auto addedCap = false;
	auto addedExecutionMode = false;

	auto section = 0u;
	auto entryPointID = u32(-1);
	auto insertDecosPos = u32(-1);

	auto offset = 5u;
	while(offset < spirv.size()) {
		auto first = spirv[offset];
		auto op = spv11::Op(first & 0xFFFFu);
		auto wordCount = first >> 16u;

		// We need to add the Xfb Execution mode to our entry point.
		if(section == 5u && op != spv11::Op::OpEntryPoint) {
			dlg_assert_or(entryPointID != u32(-1), return {});

			section = 6u;
			patched.push_back((3u << 16) | u32(spv11::Op::OpExecutionMode));
			patched.push_back(entryPointID);
			patched.push_back(u32(spv11::ExecutionMode::Xfb));

			addedExecutionMode = true;
		}

		// check if we have reached section 8
		if(isOpInSection8(op) && insertDecosPos == u32(-1)) {
			dlg_assert(section < 8);
			section = 8u;
			insertDecosPos = u32(patched.size());
		}

		for(auto i = 0u; i < wordCount; ++i) {
			patched.push_back(spirv[offset + i]);
			hash ^= spirv[offset + i];
		}

		// We need to add the TransformFeedback capability
		if(op == spv11::Op::OpCapability) {
			dlg_assert(section <= 1u);
			section = 1u;

			dlg_assert(wordCount == 2);
			auto cap = spv11::Capability(spirv[offset + 1]);

			// The shader *must* declare shader capability exactly once.
			// We add the transformFeedback cap just immediately after that.
			if(cap == spv11::Capability::Shader) {
				dlg_assert(!addedCap);
				patched.push_back((2u << 16) | u32(spv11::Op::OpCapability));
				patched.push_back(u32(spv11::Capability::TransformFeedback));
				addedCap = true;
			}

			// When the shader itself declared that capability, there is
			// nothing we can do.
			// TODO: maybe in some cases shaders just declare that cap but
			// don't use it? In that case we could still patch in our own values
			if(cap == spv11::Capability::TransformFeedback) {
				dlg_debug("Shader is already using transform feedback!");
				return {};
			}
		}

		// We need to find the id of the entry point.
		// We are also interested in the variables used by the entry point
		if(op == spv11::Op::OpEntryPoint) {
			dlg_assert(section <= 5u);
			section = 5u;

			dlg_assert(wordCount >= 4);
			auto length = wordCount - 3;
			auto name = extractString(span(spirv).subspan(offset + 3, length));
			if(!name.empty() && name == entryPoint) {
				dlg_assert(entryPointID == u32(-1));
				entryPointID = spirv[offset + 2];
			}
		}

		offset += wordCount;
	}

	if(!addedCap || !addedExecutionMode || insertDecosPos == u32(-1)) {
		dlg_warn("Could not inject xfb into shader. Likely error inside vil. "
			"capability: {}, executionMode: {}, captureVars.size(): {}, decosPos: {}",
			addedCap, addedExecutionMode, insertDecosPos);
		return {};
	}

	// parse sizes, build the vector of captured output values.
	// TODO: use spc::Compiler from SpirvData. Hard to synchronize though,
	// we need to set entry point and spec constants :(
	spc::Compiler compiler(std::vector<u32>(spirv.begin(), spirv.end()));
	compiler.set_entry_point(entryPoint, spv::ExecutionModelVertex);

	// It's important we set the specialization entries here since
	// they might influence output sizes, e.g. for arrays.
	for(auto& entry : spec.entries) {
		std::optional<u32> id;
		for(auto& sc : compiler.get_specialization_constants()) {
			if(sc.constant_id == entry.constantID) {
				id = sc.id;
				break;
			}
		}

		// seems like having specialization ids that don't appear in
		// the shader is allowed per vulkan spec
		if(!id) {
			continue;
		}

		auto& constant = compiler.get_constant(*id);

		// spec constants can only be scalar: int, float or bool
		dlg_assert(constant.m.columns == 1u);
		dlg_assert(constant.m.c[0].vecsize == 1u);
		dlg_assert(entry.size <= 4);
		dlg_assert(entry.offset + entry.size <= spec.data.size());

		auto* src = spec.data.data() + entry.offset;
		std::memcpy(constant.m.c[0].r, src, entry.size);
	}

	compiler.compile();
	compiler.update_active_builtins();

	std::vector<XfbCapture> captures;
	std::vector<u32> newDecos;

	auto ivars = compiler.get_active_interface_variables();

	auto bufOffset = 0u;
	for(auto& var : ivars) {
		auto storage = compiler.get_storage_class(var);
		if(storage != spv::StorageClassOutput) {
			continue;
		}

		auto& ptype = compiler.get_type_from_variable(var);
		dlg_assert(ptype.pointer);
		dlg_assert(ptype.parent_type);
		auto& type = compiler.get_type(ptype.parent_type);

		auto name = compiler.get_name(var);
		if(name.empty()) {
			name = dlg::format("Output{}", var);
		}

		if(type.basetype == spc::SPIRType::Struct) {
			name += ".";
			annotateCapture(compiler, type, name, bufOffset, captures, newDecos);
		} else {
			XfbCapture cap {};
			auto baseSize = baseTypeSize(type, cap);
			if(baseSize == u32(-1)) {
				continue;
			}

			if(compiler.has_decoration(var, spv::DecorationBuiltIn)) {
				// filter out unwritten builtins
				auto builtin = compiler.get_decoration(var, spv::DecorationBuiltIn);
				if(!compiler.has_active_builtin(spv::BuiltIn(builtin), spv::StorageClassOutput)) {
					continue;
				}
				cap.builtin = builtin;
			}

			auto size = baseSize;
			for(auto j = 0u; j < type.array.size(); ++j) {
				auto dim = type.array[j];
				if(!type.array_size_literal[j]) {
					dim = compiler.evaluate_constant_u32(dim);
				}

				cap.array.push_back(dim);
				size *= dim;
			}

			if(!compiler.has_decoration(type.self, spv::DecorationArrayStride) &&
					!type.array.empty()) {
				addDeco(newDecos, var, spv11::Decoration::ArrayStride, baseSize);
			}

			dlg_assert_or(!compiler.has_decoration(var, spv::DecorationOffset), continue);
			// TODO: have to align offset properly for 64-bit types
			// compiler.set_decoration(var, spv::DecorationOffset, bufOffset);
			addDeco(newDecos, var, spv11::Decoration::Offset, bufOffset);

			cap.name = name;
			cap.offset = bufOffset;

			captures.push_back(std::move(cap));
			bufOffset += size;
		}
	}

	if(captures.empty()) {
		dlg_info("xfb: nothing to capture?! Likely a vil error");
		return {};
	}

	// TODO: stride align 8 is only needed when we have double vars,
	// otherwise 4 would be enough. Track that somehow. The same way
	// we'd also have to align 64-bit types, see above.
	// auto stride = align(bufOffset, 8u);
	auto stride = bufOffset;

	for(auto& var : ivars) {
		auto storage = compiler.get_storage_class(var);
		if(storage != spv::StorageClassOutput) {
			continue;
		}

		addDeco(newDecos, var, spv11::Decoration::XfbBuffer, 0u);
		addDeco(newDecos, var, spv11::Decoration::XfbStride, stride);
	}

	// insert decos into patched spirv
	patched.insert(patched.begin() + insertDecosPos, newDecos.begin(), newDecos.end());

	auto desc = IntrusivePtr<XfbPatchDesc>(new XfbPatchDesc());
	desc->captures = std::move(captures);
	desc->stride = stride;
	return {patched, std::move(desc)};
}

XfbPatchData patchShaderXfb(Device& dev, span<const u32> spirv,
		const char* entryPoint, ShaderSpecialization spec,
		std::string_view modName) {
	ZoneScoped;

	auto patched = patchSpirvXfb(spirv, entryPoint, spec);
	if(!patched.desc) {
		return {};
	}

	// NOTE: useful for debugging
	(void) modName;
	/*
	std::string output = "vil";
	if(!modName.empty()) {
		output += "_";
		output += modName;
	}
	output += ".";
	output += std::to_string(badHash);
	output += ".spv";
	writeFile(output.c_str(), bytes(patched), true);

	dlg_info("xfb: {}, stride {}", output, ret.desc->stride);
	for(auto& cap : ret.desc->captures) {
		dlg_info("  {}", cap.name);
		dlg_info("  >> offset {}", cap.offset);
		dlg_info("  >> size {}", (cap.width * cap.columns * cap.vecsize) / 8);
		if(cap.builtin) {
			dlg_info("  >> builtin {}", *cap.builtin);
		}
	}
	*/

	VkShaderModuleCreateInfo ci {};
	ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	ci.pCode = patched.spirv.data();
	ci.codeSize = patched.spirv.size() * 4;

	XfbPatchData ret;
	auto res = dev.dispatch.CreateShaderModule(dev.handle, &ci, nullptr, &ret.mod);
	if(res != VK_SUCCESS) {
		dlg_error("xfb CreateShaderModule: {} (res)", vk::name(res), res);
		return {};
	}

	std::string pname = std::string(modName);
	pname += "(vil:xfb-patched)";
	nameHandle(dev, ret.mod, pname.c_str());

	ret.entryPoint = entryPoint;
	ret.spec = std::move(spec);
	ret.desc = std::move(patched.desc);
	return ret;
}


// ShaderModule
SpirvData::~SpirvData() {
	if(reflection.get()) {
		spvReflectDestroyShaderModule(reflection.get());
	}
}

ShaderModule::~ShaderModule() {
	if(!dev) {
		return;
	}

	for(auto& patched : this->xfb) {
		dev->dispatch.DestroyShaderModule(dev->handle, patched.mod, nullptr);
	}
}

// api
VKAPI_ATTR VkResult VKAPI_CALL CreateShaderModule(
		VkDevice                                    device,
		const VkShaderModuleCreateInfo*             pCreateInfo,
		const VkAllocationCallbacks*                pAllocator,
		VkShaderModule*                             pShaderModule) {
	auto& dev = getDevice(device);
	auto res = dev.dispatch.CreateShaderModule(device, pCreateInfo, pAllocator, pShaderModule);
	if(res != VK_SUCCESS) {
		return res;
	}

	auto& mod = dev.shaderModules.add(*pShaderModule);
	mod.objectType = VK_OBJECT_TYPE_SHADER_MODULE;
	mod.dev = &dev;
	mod.handle = *pShaderModule;

	dlg_assert(pCreateInfo->codeSize % 4 == 0);
	mod.spv = {pCreateInfo->pCode, pCreateInfo->pCode + pCreateInfo->codeSize / 4};

	mod.code = IntrusivePtr<SpirvData>(new SpirvData());
	mod.code->reflection = std::make_unique<SpvReflectShaderModule>();
	auto reflRes = spvReflectCreateShaderModule(pCreateInfo->codeSize,
		pCreateInfo->pCode, mod.code->reflection.get());
	dlg_assertl(dlg_level_info, reflRes == SPV_REFLECT_RESULT_SUCCESS);

	// TODO: catch errors here
	mod.code->compiled = std::make_unique<spc::Compiler>(
		pCreateInfo->pCode, pCreateInfo->codeSize / 4);

	return res;
}

VKAPI_ATTR void VKAPI_CALL DestroyShaderModule(
		VkDevice                                    device,
		VkShaderModule                              shaderModule,
		const VkAllocationCallbacks*                pAllocator) {
	if (!shaderModule) {
		return;
	}

	auto& dev = getDevice(device);
	dev.shaderModules.mustErase(shaderModule);
	dev.dispatch.DestroyShaderModule(device, shaderModule, pAllocator);
}

} // namespace vil
