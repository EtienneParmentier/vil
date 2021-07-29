#include <util/buffmt.hpp>
#include <util/f16.hpp>
#include <util/bytes.hpp>
#include <gui/util.hpp>
#include <threadContext.hpp>

namespace vil {

struct Formatter {
	const spc::Meta::Decoration* memberDeco;
	const spc::SPIRType* type;
	u32 typeID;
	const char* name;

	ReadBuf data;
	u32 offset;
	const spc::Compiler* compiler;
};

void display(const Formatter& formatter);

struct FormattedScalar {
	std::string scalar;
	std::string error {};
};

template<typename T8, typename T16, typename T32, typename T64, typename Cast = T64>
FormattedScalar formatScalar(span<const std::byte> data, u32 offset, u32 width) {
	dlg_assert(width % 8 == 0u);
	if(offset + width / 8 > data.size()) {
		return {"N/A", dlg::format("Out of bounds")};
	}

	data = data.subspan(offset, width / 8);
	std::string val;
	switch(width) {
		case 8:  return {dlg::format("{}", Cast(copy<T8> (data)))};
		case 16: return {dlg::format("{}", Cast(copy<T16>(data)))};
		case 32: return {dlg::format("{}", Cast(copy<T32>(data)))};
		case 64: return {dlg::format("{}", Cast(copy<T64>(data)))};
		default:
			return {"N/A", dlg::format("Unsupported type width {}", width)};
	}
}

void displayStruct(const Formatter& fmt) {
	ImGui::TableNextRow();
	ImGui::TableNextColumn();

	auto id = dlg::format("{}:{}", fmt.name, fmt.offset);
	auto flags = ImGuiTreeNodeFlags_FramePadding;

	// all structs are initially open
	ImGui::SetNextItemOpen(true, ImGuiCond_Once);
	if(ImGui::TreeNodeEx(id.c_str(), flags, "%s", fmt.name)) {
		auto* meta = fmt.compiler->get_ir().find_meta(fmt.typeID);
		for(auto i = 0u; i < fmt.type->member_types.size(); ++i) {
			auto memTypeID = fmt.type->member_types[i];
			auto& memType = fmt.compiler->get_type(memTypeID);

			auto off = 0u;
			const spc::Meta::Decoration* deco {};
			auto name = dlg::format("?{}", i);

			dlg_assert(meta && meta->members.size() > i);
			// if(meta && meta->members.size() > i) {
				deco = &meta->members[i];
				off = deco->offset;
				if(!deco->alias.empty()) {
					name = deco->alias;
				}
			// }

			auto fwd = fmt;
			fwd.name = name.c_str();
			fwd.type = &memType;
			fwd.typeID = memTypeID;
			fwd.memberDeco = deco;
			fwd.offset += off;

			display(fwd);
		}

		ImGui::TreePop();
	}
}

FormattedScalar formatScalar(const spc::SPIRType& type,
		ReadBuf data, u32 offset) {
	switch(type.basetype) {
		case spc::SPIRType::SByte:
		case spc::SPIRType::Short:
		case spc::SPIRType::Int:
		case spc::SPIRType::Int64:
			return formatScalar<i8, i16, i32, i64>(data, offset, type.width);

		case spc::SPIRType::UByte:
		case spc::SPIRType::UShort:
		case spc::SPIRType::UInt:
		case spc::SPIRType::UInt64:
			return formatScalar<u8, u16, u32, u64>(data, offset, type.width);

		case spc::SPIRType::Float:
		case spc::SPIRType::Double:
		case spc::SPIRType::Half:
			dlg_assertm(type.width != 8, "Invalid float bit width");
			return formatScalar<u8, f16, float, double>(data, offset, type.width);

		case spc::SPIRType::Boolean:
			return formatScalar<u8, u16, u32, u64, bool>(data, offset, type.width);

		// TODO: pointer support

		default:
			return {"N/A", dlg::format("Unsupported type {}", unsigned(type.basetype))};
	}
}

std::string atomTypeName(const spc::SPIRType& type) {
	bool scalar = (type.vecsize == 1u && type.columns == 1u);
	std::string t;
	switch(type.basetype) {
		case spc::SPIRType::SByte: t = "i8"; break;
		case spc::SPIRType::Short: t = "i16"; break;
		case spc::SPIRType::Int: t = "i32"; break;
		case spc::SPIRType::Int64: t = "i64"; break;
		case spc::SPIRType::UByte: t = "u8"; break;
		case spc::SPIRType::UShort: t = "u16"; break;
		case spc::SPIRType::UInt: t = "u32"; break;
		case spc::SPIRType::UInt64: t = "u64"; break;
		case spc::SPIRType::Float: t = "f32"; break;
		case spc::SPIRType::Double: t = "f64"; break;
		case spc::SPIRType::Half: t = "f16"; break;
		case spc::SPIRType::Boolean: t = scalar ? "bool" : "b"; break;
		default:
			return "Unsupported";
	}

	if(scalar) {
		return t;
	}

	if(type.vecsize > 1 && type.columns == 1u) {
		return dlg::format("{}vec{}", t, type.vecsize);
	}

	if(type.columns == type.vecsize) {
		return dlg::format("{}mat{}", t, type.vecsize);
	}

	// NOTE: we don't use glsl matCxR syntax here since it's confusing.
	// Usually number of rows is given first
	return dlg::format("{}mat ({} rows, {} colums}", t, type.vecsize, type.columns);
}

// non-array, non-struct: matrix/vector/scalar/pointer
void displayAtom(const Formatter& fmt) {
	ImGui::TableNextRow();
	ImGui::TableNextColumn();

	ImGui::AlignTextToFramePadding();
	ImGui::Bullet();
	ImGui::SameLine();
	imGuiText("{} ", fmt.name);

	if(ImGui::IsItemHovered()) {
		ImGui::BeginTooltip();
		imGuiText("{}", atomTypeName(*fmt.type));

		if(fmt.memberDeco) {
			if(fmt.memberDeco->decoration_flags.get(spv::DecorationRowMajor)) {
				imGuiText("Row major memory layout");
			}
			if(fmt.memberDeco->decoration_flags.get(spv::DecorationColMajor)) {
				imGuiText("Column major memory layout");
			}
			if(fmt.memberDeco->decoration_flags.get(spv::DecorationMatrixStride)) {
				imGuiText("Matrix stride: {}", fmt.memberDeco->matrix_stride);
			}
		}

		ImGui::EndTooltip();
	}

	ImGui::TableNextColumn();

	auto baseSize = fmt.type->width / 8;
	auto rowStride = baseSize;
	auto colStride = baseSize;
	auto rowMajor = true;

	// display size; we display vectors as row vectors
	auto numRows = fmt.type->vecsize;
	auto numColumns = fmt.type->columns;
	if(fmt.type->vecsize > 1 && fmt.type->columns == 1) {
		numColumns = fmt.type->vecsize;
		numRows = 1u;
	}

	dlg_assert(numRows > 0);
	dlg_assert(numColumns > 0);
	dlg_assert(fmt.memberDeco);

	if(fmt.memberDeco) {
		rowMajor = fmt.memberDeco->decoration_flags.get(spv::DecorationRowMajor);

		auto hasMtxStride = fmt.memberDeco->decoration_flags.get(spv::DecorationMatrixStride);
		if(hasMtxStride) {
			(rowMajor ? rowStride : colStride) = fmt.memberDeco->matrix_stride;
		}
	}

	auto id = dlg::format("Value:{}:{}", fmt.name, (void*) &fmt);
	auto flags = ImGuiTableFlags_SizingFixedFit; // | ImGuiTableFlags_BordersInner;
	if(ImGui::BeginTable(id.c_str(), numColumns, flags)) {
		for(auto r = 0u; r < numRows; ++r) {
			ImGui::TableNextRow();
			for(auto c = 0u; c < numColumns; ++c) {
				ImGui::TableNextColumn();
				auto off = fmt.offset + r * rowStride + c * colStride;
				auto fs = formatScalar(*fmt.type, fmt.data, off);

				imGuiText("{}", fs.scalar);
				if(ImGui::IsItemHovered()) {
					ImGui::BeginTooltip();
					imGuiText("Offset: {}", off);
					if(!fs.error.empty()) {
						imGuiText("Error: {}", fs.error);
					}
					ImGui::EndTooltip();
				}
			}
		}

		ImGui::EndTable();
	}
}

void displayNonArray(const Formatter& fmt) {
	if(fmt.type->basetype == spc::SPIRType::Struct) {
		displayStruct(fmt);
	} else {
		displayAtom(fmt);
	}
}

void displayArrayDim(const Formatter& fmt, span<const u32> rem) {
	auto count = rem[0];
	rem = rem.subspan(1);

	auto* meta = fmt.compiler->get_ir().find_meta(fmt.typeID);
	dlg_assert(meta && meta->decoration.decoration_flags.get(spv::DecorationArrayStride));
	auto stride = meta->decoration.array_stride;

	std::string name;
	auto subSize = stride;
	for(auto size : rem) {
		dlg_assert(size != 0u); // only first dimension can be runtime size
		subSize *= size;

		name += dlg::format("[{}]", size);
	}

	if(count == 0u) {
		// runtime array, find out real size.
		auto remSize = fmt.data.size() - fmt.offset;
		// doesn't have to be like that even though it's sus if the buffer
		// size isn't a multiple of the stride.
		// dlg_assert(remSize % subSize == 0u);
		count = remSize / subSize; // intentionally round down
	}

	name = dlg::format("{}: [{}]", fmt.name, count) + name;

	ImGui::TableNextRow();
	ImGui::TableNextColumn();

	// all arrays are initially closed
	auto id = dlg::format("{}:{}", fmt.name, fmt.offset);
	auto flags = ImGuiTreeNodeFlags_FramePadding;

	ImGui::SetNextItemOpen(false, ImGuiCond_Once);
	if(ImGui::TreeNodeEx(id.c_str(), flags, "%s", name.c_str())) {
		// draw paging mechanism in the right column
		constexpr auto pageSize = 100u;
		auto page = 0;
		if(count > pageSize) {
			auto maxPage = (count - 1) / pageSize;
			auto id = ImGui::GetID("arrayPage");

			page = ImGui::GetStateStorage()->GetInt(id, 0);
			page = std::clamp<int>(page, 0, maxPage);

			ImGui::TableNextColumn();

			auto prevDisabled = (page == 0);
			auto nextDisabled = (unsigned(page) == maxPage);

			imGuiText("Page:");
			ImGui::SameLine();

			pushDisabled(prevDisabled);
			if(ImGui::Button("<") && !prevDisabled) {
				--page;
			}
			popDisabled(prevDisabled);

			ImGui::SameLine();

			// TODO: find better way to quickly get to a specific entry
			imGuiText("{} / {}", page, maxPage);
			// ImGui::SetNextItemWidth(30.f);
			// ImGui::DragInt("", &page, 0, maxPage);

			ImGui::SameLine();

			pushDisabled(nextDisabled);
			if(ImGui::Button(">") && !nextDisabled) {
				++page;
			}
			popDisabled(nextDisabled);

			ImGui::GetStateStorage()->SetInt(id, page);
		}

		// draw children
		auto fwd = fmt;
		if(rem.empty()) {
			// strip array type
			dlg_assert(fwd.type->parent_type);
			fwd.typeID = fmt.type->parent_type;
			fwd.type = &fmt.compiler->get_type(fmt.type->parent_type);
		}

		auto end = std::min(count, (page + 1) * pageSize);
		for(auto i = page * pageSize; i < end; ++i) {
			auto newName = dlg::format("{}[{}]", fmt.name, i);
			fwd.name = newName.c_str();
			fwd.offset = fmt.offset + i * subSize;
			if(rem.empty()) {
				displayNonArray(fwd);
			} else {
				displayArrayDim(fwd, rem);
			}
		}

		ImGui::TreePop();
	}
}

void displayArray(const Formatter& fmt) {
	// display array
	dlg_assert(fmt.type->array.size() == fmt.type->array_size_literal.size());
	std::vector<u32> sizes;
	sizes.resize(fmt.type->array.size());

	for(auto d = 0u; d < fmt.type->array.size(); ++d) {
		if(fmt.type->array_size_literal[d] == true) {
			sizes[d] = fmt.type->array[d];
		} else {
			sizes[d] = fmt.compiler->evaluate_constant_u32(fmt.type->array[d]);
		}
	}

	displayArrayDim(fmt, sizes);
}

void display(const Formatter& formatter) {
	if(!formatter.type->array.empty()) {
		displayArray(formatter);
	} else {
		displayNonArray(formatter);
	}
}

void display(const spc::Compiler& compiler, u32 typeID,
		const char* name, ReadBuf data, u32 offset) {
	Formatter fmt {};
	fmt.compiler = &compiler;
	fmt.type = &compiler.get_type(typeID);
	fmt.typeID = typeID;

	if(fmt.type->pointer) {
		dlg_assert(fmt.type->parent_type);
		fmt.typeID = fmt.type->parent_type;
		fmt.type = &compiler.get_type(fmt.type->parent_type);
	}

	fmt.name = name ? name : "?";
	fmt.data = data;
	fmt.offset = offset;
	// TODO fmt.memberDeco?

	display(fmt);
}


/////////////////////////////////////////////////////////////////////////////////

Type* buildType(const spc::Compiler& compiler, u32 typeID,
		ThreadMemScope& memScope, const spc::Meta::Decoration* memberDeco) {

	auto stype = &compiler.get_type(typeID);
	if(stype->pointer) {
		dlg_assert(stype->parent_type);
		typeID = stype->parent_type;
		stype = &compiler.get_type(stype->parent_type);
	}

	auto& dst = *memScope.allocRaw<Type>();

	auto* meta = compiler.get_ir().find_meta(typeID);
	if(meta) {
		dst.deco.name = meta->decoration.alias;
	}

	if(memberDeco) {
		if(memberDeco->decoration_flags.get(spv::DecorationRowMajor)) {
			dst.deco.flags |= Decoration::Bits::rowMajor;
		}
		if(memberDeco->decoration_flags.get(spv::DecorationColMajor)) {
			dst.deco.flags |= Decoration::Bits::colMajor;
		}
		if(memberDeco->decoration_flags.get(spv::DecorationMatrixStride)) {
			dst.deco.matrixStride = memberDeco->matrix_stride;
		}
		if(memberDeco->decoration_flags.get(spv::DecorationOffset)) {
			dst.deco.offset = memberDeco->offset;
		}
	}

	// handle array
	if(!stype->array.empty()) {
		dlg_assert(meta && meta->decoration.decoration_flags.get(spv::DecorationArrayStride));
		dst.deco.arrayStride = meta->decoration.array_stride;

		dlg_assert(stype->array.size() == stype->array_size_literal.size());
		dst.array.resize(stype->array.size());

		for(auto d = 0u; d < stype->array.size(); ++d) {
			if(stype->array_size_literal[d] == true) {
				dst.array[d] = stype->array[d];
			} else {
				dst.array[d] = compiler.evaluate_constant_u32(stype->array[d]);
			}
		}
	}

	if(stype->basetype == spc::SPIRType::Struct) {
		// handle struct
		for(auto i = 0u; i < stype->member_types.size(); ++i) {
			auto memTypeID = stype->member_types[i];

			auto off = 0u;
			const spc::Meta::Decoration* deco {};
			auto name = dlg::format("?{}", i);

			dlg_assert(meta && meta->members.size() > i);
			deco = &meta->members[i];
			off = deco->offset;
			if(!deco->alias.empty()) {
				name = deco->alias;
			}

			auto& mdst = dst.members.emplace_back();
			mdst.type = buildType(compiler, memTypeID, memScope, deco);
			mdst.name = name;
			mdst.offset = off;

			if(!mdst.type) {
				return nullptr;
			}
		}

		dst.type = Type::typeStruct;
		return &dst;
	}

	// handle atom
	auto getBaseType = [](spc::SPIRType::BaseType t) -> std::optional<Type::BaseType> {
		switch(t) {
			case spc::SPIRType::Double:
			case spc::SPIRType::Float:
			case spc::SPIRType::Half:
				return Type::typeFloat;

			case spc::SPIRType::Int:
			case spc::SPIRType::Short:
			case spc::SPIRType::Int64:
			case spc::SPIRType::SByte:
				return Type::typeInt;

			case spc::SPIRType::UInt:
			case spc::SPIRType::UShort:
			case spc::SPIRType::UInt64:
			case spc::SPIRType::UByte:
				return Type::typeUint;

			case spc::SPIRType::Boolean:
				return Type::typeBool;

			default:
				return std::nullopt;
		}
	};

	auto bt = getBaseType(stype->basetype);
	if(!bt) {
		dlg_error("Unsupported shader type: {}", u32(stype->basetype));
		return nullptr;
	}

	dst.type = *bt;
	dst.width = stype->width;
	dst.vecsize = stype->vecsize;
	dst.columns = stype->columns;

	return &dst;
}

Type* buildType(const spc::Compiler& compiler, u32 typeID,
		ThreadMemScope& memScope) {
	return buildType(compiler, typeID, memScope, nullptr);
}


/////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////

FormattedScalar formatScalar(const Type& type, ReadBuf data, u32 offset) {
	switch(type.type) {
		case Type::typeInt:
			return formatScalar<i8, i16, i32, i64>(data, offset, type.width);
		case Type::typeUint:
			return formatScalar<u8, u16, u32, u64>(data, offset, type.width);
		case Type::typeFloat:
			dlg_assertm(type.width != 8, "Invalid float bit width");
			return formatScalar<u8, f16, float, double>(data, offset, type.width);
		case Type::typeBool:
			return formatScalar<u8, u16, u32, u64, bool>(data, offset, type.width);
		default:
			return {"N/A", dlg::format("Unsupported type {}", unsigned(type.type))};
	}
}
std::string atomTypeName(const Type& type) {
	bool scalar = (type.vecsize == 1u && type.columns == 1u);
	std::string t;
	switch(type.type) {
		case Type::typeFloat: t = "f"; break;
		case Type::typeInt: t = "i"; break;
		case Type::typeUint: t = "u"; break;
		case Type::typeBool: t = "b"; break;
		default:
			return "Unsupported";
	}

	t += dlg::format("{}", type.width);

	if(scalar) {
		return t;
	}

	if(type.vecsize > 1 && type.columns == 1u) {
		return dlg::format("{}vec{}", t, type.vecsize);
	}

	if(type.columns == type.vecsize) {
		return dlg::format("{}mat{}", t, type.vecsize);
	}

	// NOTE: we don't use glsl matCxR syntax here since it's confusing.
	// Usually number of rows is given first
	return dlg::format("{}mat, {} rows, {} colums", t, type.vecsize, type.columns);
}

void displayAtom(const char* baseName, const Type& type, ReadBuf data, u32 offset) {
	ImGui::TableNextRow();
	ImGui::TableNextColumn();

	ImGui::AlignTextToFramePadding();
	ImGui::Bullet();
	ImGui::SameLine();
	imGuiText("{} ", baseName);

	if(ImGui::IsItemHovered()) {
		ImGui::BeginTooltip();
		imGuiText("{}", atomTypeName(type));

		if(type.deco.flags & Decoration::Bits::rowMajor) {
			imGuiText("Row major memory layout");
		}
		if(type.deco.flags & Decoration::Bits::colMajor) {
			imGuiText("Column major memory layout");
		}
		if(type.deco.matrixStride) {
			imGuiText("Matrix stride: {}", type.deco.matrixStride);
		}

		ImGui::EndTooltip();
	}

	ImGui::TableNextColumn();

	auto baseSize = type.width / 8;
	auto rowStride = baseSize;
	auto colStride = baseSize;
	auto rowMajor = bool(type.deco.flags & Decoration::Bits::rowMajor);

	// display size; we display vectors as row vectors
	auto numRows = type.vecsize;
	auto numColumns = type.columns;
	if(type.vecsize > 1 && type.columns == 1) {
		numColumns = type.vecsize;
		numRows = 1u;
	}

	dlg_assert(numRows > 0);
	dlg_assert(numColumns > 0);

	if(type.deco.matrixStride) {
		(rowMajor ? rowStride : colStride) = type.deco.matrixStride;
	}

	auto id = dlg::format("Value:{}:{}", baseName, &type);
	auto flags = ImGuiTableFlags_SizingFixedFit; // | ImGuiTableFlags_BordersInner;
	if(ImGui::BeginTable(id.c_str(), numColumns, flags)) {
		for(auto r = 0u; r < numRows; ++r) {
			ImGui::TableNextRow();
			for(auto c = 0u; c < numColumns; ++c) {
				ImGui::TableNextColumn();
				auto off = offset + r * rowStride + c * colStride;
				auto fs = formatScalar(type, data, off);

				imGuiText("{}", fs.scalar);
				if(ImGui::IsItemHovered()) {
					ImGui::BeginTooltip();
					imGuiText("Offset: {}", off);
					if(!fs.error.empty()) {
						imGuiText("Error: {}", fs.error);
					}
					ImGui::EndTooltip();
				}
			}
		}

		ImGui::EndTable();
	}
}

void displayStruct(const char* baseName, const Type& type, ReadBuf data, u32 offset) {
	ImGui::TableNextRow();
	ImGui::TableNextColumn();

	auto id = dlg::format("{}:{}", baseName, offset);
	auto flags = ImGuiTreeNodeFlags_FramePadding;

	// all structs are initially open
	ImGui::SetNextItemOpen(true, ImGuiCond_Once);
	if(ImGui::TreeNodeEx(id.c_str(), flags, "%s", baseName)) {
		for(auto i = 0u; i < type.members.size(); ++i) {
			auto& member = type.members[i];

			auto off = member.offset;
			auto name = member.name;
			if(name.empty()) {
				name = dlg::format("?{}", i);
			}

			display(name.c_str(), *member.type, data, offset + off);
		}

		ImGui::TreePop();
	}
}

void displayNonArray(const char* baseName, const Type& type, ReadBuf data, u32 offset) {
	if(type.type == Type::typeStruct) {
		displayStruct(baseName, type, data, offset);
	} else {
		displayAtom(baseName, type, data, offset);
	}
}

void displayArrayDim(const char* baseName, const Type& type, span<const u32> rem,
		ReadBuf data, u32 offset) {
	auto count = rem[0];
	rem = rem.subspan(1);

	std::string name;
	auto subSize = type.deco.arrayStride;
	dlg_assert(subSize);

	for(auto size : rem) {
		dlg_assert(size != 0u); // only first dimension can be runtime size
		subSize *= size;

		name += dlg::format("[{}]", size);
	}

	if(count == 0u) {
		// runtime array, find out real size.
		auto remSize = data.size() - offset;
		// doesn't have to be like that even though it's sus if the buffer
		// size isn't a multiple of the stride.
		// dlg_assert(remSize % subSize == 0u);
		count = remSize / subSize; // intentionally round down
	}

	name = dlg::format("{}: [{}]", baseName, count) + name;

	ImGui::TableNextRow();
	ImGui::TableNextColumn();

	// all arrays are initially closed
	auto id = dlg::format("{}:{}", type.deco.name, offset);
	auto flags = ImGuiTreeNodeFlags_FramePadding;

	ImGui::SetNextItemOpen(false, ImGuiCond_Once);
	if(ImGui::TreeNodeEx(id.c_str(), flags, "%s", name.c_str())) {
		// draw paging mechanism in the right column
		constexpr auto pageSize = 100u;
		auto page = 0;
		if(count > pageSize) {
			auto maxPage = (count - 1) / pageSize;
			auto id = ImGui::GetID("arrayPage");

			page = ImGui::GetStateStorage()->GetInt(id, 0);
			page = std::clamp<int>(page, 0, maxPage);

			ImGui::TableNextColumn();

			auto prevDisabled = (page == 0);
			auto nextDisabled = (unsigned(page) == maxPage);

			imGuiText("Page:");
			ImGui::SameLine();

			pushDisabled(prevDisabled);
			if(ImGui::Button("<") && !prevDisabled) {
				--page;
			}
			popDisabled(prevDisabled);

			ImGui::SameLine();

			// TODO: find better way to quickly get to a specific entry
			imGuiText("{} / {}", page, maxPage);
			// ImGui::SetNextItemWidth(30.f);
			// ImGui::DragInt("", &page, 0, maxPage);

			ImGui::SameLine();

			pushDisabled(nextDisabled);
			if(ImGui::Button(">") && !nextDisabled) {
				++page;
			}
			popDisabled(nextDisabled);

			ImGui::GetStateStorage()->SetInt(id, page);
		}

		// draw children
		auto end = std::min(count, (page + 1) * pageSize);
		for(auto i = page * pageSize; i < end; ++i) {
			auto newName = dlg::format("{}[{}]", baseName, i);
			auto newOffset = offset + i * subSize;
			if(rem.empty()) {
				displayNonArray(newName.c_str(), type, data, newOffset);
			} else {
				displayArrayDim(newName.c_str(), type, rem, data, newOffset);
			}
		}

		ImGui::TreePop();
	}
}

void display(const char* name, const Type& type, ReadBuf data, u32 offset) {
	if(type.array.empty()) {
		displayNonArray(name, type, data, offset);
		return;
	}

	// display array
	displayArrayDim(name, type, type.array, data, offset);
}

} // namespace vil
