#pragma once

#include <fwd.hpp>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <dlg/dlg.hpp>
#include <ds.hpp>
#include <buffer.hpp>
#include <string>

namespace vil {

struct Row {
	const char* name;
	std::string content;

	template<typename... Args>
	Row(const char* rname, const char* fmt, Args&&... args) :
			name(rname), content(dlg::format(fmt, std::forward<Args>(args)...)) {
	}

	template<typename Arg>
	Row(const char* rname, const Arg& arg) :
			name(rname), content(dlg::format("{}", arg)) {
	}

	Row(const char* rname, std::string rcontent) :
			name(rname), content(std::move(rcontent)) {
	}
};

template<typename... Args>
inline void imGuiText(const char* fmt, Args&&... args) {
	ImGui::Text("%s", dlg::format(fmt, std::forward<Args>(args)...).c_str());
}

inline void asColumns2(span<const Row> rows) {
	ImGui::Columns(2);
	for(auto& row : rows) {
		ImGui::Text("%s", row.name);
		ImGui::NextColumn();
		ImGui::Text("%s", row.content.c_str());
		ImGui::NextColumn();
	}

	ImGui::Columns();
}

// Yes, there is a lot of ugliness here.
inline int imGuiTextStringCallback(ImGuiInputTextCallbackData* data) {
	if(data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
		auto& output = *static_cast<std::string*>(data->UserData);
		dlg_assert(output.c_str() == data->Buf);
		output.resize(data->BufTextLen);
		data->Buf = (char*) output.c_str();
	}

	return 0;
}

inline bool imGuiTextInput(const char* label, std::string& output) {
	auto ret = ImGui::InputText(label, (char*) output.c_str(), output.capacity() + 1,
		ImGuiInputTextFlags_CallbackResize, imGuiTextStringCallback, (void*) &output);
	return ret;
}

inline bool imGuiTextMultiline(const char* label, std::string& output) {
	auto ret = ImGui::InputTextMultiline(label, (char*) output.c_str(), output.capacity() + 1,
		{0, 0}, ImGuiInputTextFlags_CallbackResize, imGuiTextStringCallback, (void*) &output);
	return ret;
}

inline void drawOffsetSize(const BufferDescriptor& info,
		std::optional<u32> dynOffset = std::nullopt) {
	std::string offText;
	auto off = info.offset;
	if(!dynOffset) {
		offText = dlg::format("Offset {}", info.offset);
	} else {
		off = info.offset + *dynOffset;
		offText = dlg::format("Offset {} (Static {} + Dynamic {})", off,
			info.offset, *dynOffset);
	}

	if(info.range == VK_WHOLE_SIZE) {
		if(info.buffer) {
			dlg_assert(info.buffer->ci.size >= off);
			auto range = info.buffer->ci.size - off;
			imGuiText("{}, whole size ({})", offText, range);
		} else {
			imGuiText("{}, whole size", offText);
		}
	} else {
		imGuiText("{}, Size {}", offText, info.range);
	}
}

// If count > 1, displays an int slider for val. Always makes sure val is
// in range [0, count). count must not be 0.
template<typename T = u32>
bool optSliderRange(const char* name, T& val, std::size_t count) {
	dlg_assert(count != 0u);
	if(count <= 1u) {
		val = 0u;
		return false;
	}

	int v = int(val);
	auto ret = ImGui::SliderInt(name, &v, 0, int(count - 1));

	// clamping is needed since SliderInt might go out of range
	// when pressing CTRL
	v = std::clamp<int>(v, 0, int(count - 1));
	val = T(v);

	return ret;
}

inline void pushDisabled(bool disabled) {
	if(disabled) {
		ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.6f);
	}
}

inline void popDisabled(bool disabled) {
	if(disabled) {
		ImGui::PopStyleVar();
		ImGui::PopItemFlag();
	}
}

} // namesapce vil
