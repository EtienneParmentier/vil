#include "layer.hpp"
#include "util.hpp"
#include "data.hpp"
#include "swapchain.hpp"
#include "image.hpp"
#include "rp.hpp"
#include "cb.hpp"
#include "sync.hpp"
#include "ds.hpp"
#include "buffer.hpp"
#include "memory.hpp"
#include "shader.hpp"
#include "pipe.hpp"
#include "wayland.hpp"
#include "commands.hpp"

#include <dlg/dlg.hpp>
#include <swa/swa.h>

#include <vulkan/vk_layer.h>
#include <vulkan/vk_dispatch_table_helper.h>

namespace fuen {

// Classes
Instance::~Instance() {
	if(display) {
		swa_display_destroy(display);
	}
}

// Instance
VKAPI_ATTR VkResult VKAPI_CALL CreateInstance(
		const VkInstanceCreateInfo* ci,
		const VkAllocationCallbacks* alloc,
		VkInstance* pInstance) {
	auto* linkInfo = findChainInfo<VkLayerInstanceCreateInfo, VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO>(*ci);
	while(linkInfo && linkInfo->function != VK_LAYER_LINK_INFO) {
		linkInfo = findChainInfo<VkLayerInstanceCreateInfo, VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO>(*linkInfo);
	}

	if(!linkInfo) {
		dlg_error("No linkInfo");
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	auto fpGetInstanceProcAddr = linkInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
	auto fpCreateInstance = (PFN_vkCreateInstance)fpGetInstanceProcAddr(nullptr, "vkCreateInstance");
	if(!fpCreateInstance) {
		dlg_error("could not load vkCreateInstance");
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	// Advance the link info for the next element on the chain
	auto mutLinkInfo = const_cast<VkLayerInstanceCreateInfo*>(linkInfo);
	mutLinkInfo->u.pLayerInfo = linkInfo->u.pLayerInfo->pNext;

	// Init instance data
	// Add additionally requiresd extensions
	auto iniPtr = std::make_unique<Instance>();
	auto& ini = *iniPtr;

	// TODO: allow to disable separate window creation via compile-time
	// flag (not even compiling/requiring swa) and environment variable.
	ini.display = swa_display_autocreate("fuencaliente");

	std::vector<const char*> newExts; // keep-alive
	auto nci = *ci;
	if(ini.display) {
		newExts = {
			ci->ppEnabledExtensionNames,
			ci->ppEnabledExtensionNames + ci->enabledExtensionCount
		};

		auto fpEnumerateInstanceExtensionProperties =
			(PFN_vkEnumerateInstanceExtensionProperties)
			fpGetInstanceProcAddr(nullptr, "vkEnumerateInstanceExtensionProperties");
		dlg_assert(fpEnumerateInstanceExtensionProperties);

		unsigned nsup;
		VK_CHECK(fpEnumerateInstanceExtensionProperties(nullptr, &nsup, nullptr));
		auto supExts = std::make_unique<VkExtensionProperties[]>(nsup);
		VK_CHECK(fpEnumerateInstanceExtensionProperties(nullptr, &nsup, supExts.get()));

		unsigned nexts;
		auto exts = swa_display_vk_extensions(ini.display, &nexts);
		auto enable = true;
		for(auto i = 0u; i < nexts; ++i) {
			auto ext = std::string_view(exts[i]);

			// check if extension is already enabled by application
			auto it = find(newExts, ext);
			if(it != end(newExts)) {
				continue;
			}

			// Check if extension is supported.
			// If not, we simply can't create a window.
			auto finder = [&](auto& props){ return props.extensionName == ext; };
			auto sit = std::find_if(supExts.get(), supExts.get() + nsup, finder);
			if(sit == supExts.get() + nsup) {
				dlg_warn("Won't create window since required extension '{}' is not supported", ext);
				enable = false;
				break;
			}

			dlg_trace("Adding extension {} to instance creation", ext);
			newExts.push_back(exts[i]);
		}

		if(enable) {
			nci.ppEnabledExtensionNames = newExts.data();
			nci.enabledExtensionCount = newExts.size();
		} else {
			swa_display_destroy(ini.display);
			ini.display = nullptr;
		}
	}

	// Create instance
	VkResult result = fpCreateInstance(&nci, alloc, pInstance);
	if(result != VK_SUCCESS) {
		return result;
	}

	insertData(*pInstance, iniPtr.release());
	ini.handle = *pInstance;

	auto strOrEmpty = [](const char* str) {
		return str ? str : "";
	};

	if(ci->pApplicationInfo) {
		ini.app.apiVersion = ci->pApplicationInfo->apiVersion;
		ini.app.version = ci->pApplicationInfo->applicationVersion;
		ini.app.name = strOrEmpty(ci->pApplicationInfo->pApplicationName);
		ini.app.engineName = strOrEmpty(ci->pApplicationInfo->pEngineName);
		ini.app.engineVersion = ci->pApplicationInfo->engineVersion;
	}

	layer_init_instance_dispatch_table(*pInstance, &ini.dispatch, fpGetInstanceProcAddr);

	// add instance data to all physical devices so we can retrieve
	// it in CreateDevice
	u32 phdevCount = 0;
	ini.dispatch.EnumeratePhysicalDevices(*pInstance, &phdevCount, nullptr);
	auto phdevs = std::make_unique<VkPhysicalDevice[]>(phdevCount);
	ini.dispatch.EnumeratePhysicalDevices(*pInstance, &phdevCount, phdevs.get());

	for(auto i = 0u; i < phdevCount; ++i) {
		insertData(phdevs[i], &ini);
	}

	return result;
}

VKAPI_ATTR void VKAPI_CALL DestroyInstance(VkInstance ini, const VkAllocationCallbacks* alloc) {
	auto inid = moveData<Instance>(ini);
	dlg_assert(inid);
	inid->dispatch.DestroyInstance(ini, alloc);
}


VKAPI_ATTR void VKAPI_CALL DestroySurfaceKHR(
		VkInstance                                  instance,
		VkSurfaceKHR                                surface,
		const VkAllocationCallbacks*                pAllocator) {
	// auto platform = moveDataOpt<Platform>(surface); // destroy it
	auto& ini = getData<Instance>(instance);
	ini.dispatch.DestroySurfaceKHR(instance, surface, pAllocator);
}

VKAPI_ATTR PFN_vkVoidFunction GetInstanceProcAddr(VkInstance, const char*);
VKAPI_ATTR PFN_vkVoidFunction GetDeviceProcAddr(VkDevice, const char*);

#define FUEN_HOOK(fn) { "vk" # fn, (void *) fn }
#define FUEN_ALIAS(alias, fn) { "vk" # alias, (void *) ## fn }

static const std::unordered_map<std::string_view, void*> funcPtrTable {
   FUEN_HOOK(GetInstanceProcAddr),
   FUEN_HOOK(GetDeviceProcAddr),

   FUEN_HOOK(CreateInstance),
   FUEN_HOOK(DestroyInstance),

   FUEN_HOOK(CreateDevice),
   FUEN_HOOK(DestroyDevice),

   FUEN_HOOK(CreateSwapchainKHR),
   FUEN_HOOK(DestroySwapchainKHR),

   FUEN_HOOK(QueueSubmit),
   FUEN_HOOK(QueuePresentKHR),

   // TODO: we probably have to implement *all* the functions since
   // we say we support the extension.
   // We probably also have to return nullptr when the extension
   // isn't enabled, instead of our implementations. Just add
   // an extra "(func name -> (extension, func ptr))" lookup table. Or
   // simply add "extension" field here that is empty usually?
   // Anyways, then check (in GetProcAddr) whether that extension was
   // enabled.
   FUEN_HOOK(SetDebugUtilsObjectNameEXT),
   FUEN_HOOK(SetDebugUtilsObjectTagEXT),
   FUEN_HOOK(CmdBeginDebugUtilsLabelEXT),
   FUEN_HOOK(CmdEndDebugUtilsLabelEXT),

   // TODO: make optional
   // FUEN_HOOK(CreateWaylandSurfaceKHR),

   FUEN_HOOK(DestroySurfaceKHR),

   // rp.hpp
   FUEN_HOOK(CreateFramebuffer),
   FUEN_HOOK(DestroyFramebuffer),

   FUEN_HOOK(CreateRenderPass),
   FUEN_HOOK(DestroyRenderPass),

   // image.hpp
   FUEN_HOOK(CreateImage),
   FUEN_HOOK(DestroyImage),
   FUEN_HOOK(BindImageMemory),

   FUEN_HOOK(CreateImageView),
   FUEN_HOOK(DestroyImageView),

   FUEN_HOOK(CreateSampler),
   FUEN_HOOK(DestroySampler),

   // buffer.hpp
   FUEN_HOOK(CreateBuffer),
   FUEN_HOOK(DestroyBuffer),
   FUEN_HOOK(BindBufferMemory),

   // memory.hpp
   FUEN_HOOK(AllocateMemory),
   FUEN_HOOK(FreeMemory),
   FUEN_HOOK(MapMemory),
   FUEN_HOOK(UnmapMemory),

   // shader.hpp
   FUEN_HOOK(CreateShaderModule),
   FUEN_HOOK(DestroyShaderModule),

   // sync.hpp
   FUEN_HOOK(CreateFence),
   FUEN_HOOK(DestroyFence),
   FUEN_HOOK(ResetFences),
   FUEN_HOOK(GetFenceStatus),
   FUEN_HOOK(WaitForFences),

   FUEN_HOOK(CreateSemaphore),
   FUEN_HOOK(DestroySemaphore),

   FUEN_HOOK(CreateEvent),
   FUEN_HOOK(DestroyEvent),
   FUEN_HOOK(SetEvent),
   FUEN_HOOK(ResetEvent),
   FUEN_HOOK(GetEventStatus),

	// ds.hpp
	FUEN_HOOK(CreateDescriptorSetLayout),
	FUEN_HOOK(DestroyDescriptorSetLayout),
	FUEN_HOOK(CreateDescriptorPool),
	FUEN_HOOK(DestroyDescriptorPool),
	FUEN_HOOK(ResetDescriptorPool),
	FUEN_HOOK(AllocateDescriptorSets),
	FUEN_HOOK(FreeDescriptorSets),
	FUEN_HOOK(UpdateDescriptorSets),

	// pipe.hpp
	FUEN_HOOK(CreateGraphicsPipelines),
	FUEN_HOOK(CreateComputePipelines),
	FUEN_HOOK(CreatePipelineLayout),
	FUEN_HOOK(DestroyPipelineLayout),

   // cb.hpp
   FUEN_HOOK(CreateCommandPool),
   FUEN_HOOK(DestroyCommandPool),
   FUEN_HOOK(ResetCommandPool),

   FUEN_HOOK(AllocateCommandBuffers),
   FUEN_HOOK(FreeCommandBuffers),
   FUEN_HOOK(BeginCommandBuffer),
   FUEN_HOOK(EndCommandBuffer),
   FUEN_HOOK(ResetCommandBuffer),

   FUEN_HOOK(CmdBeginRenderPass),
   FUEN_HOOK(CmdEndRenderPass),
   FUEN_HOOK(CmdNextSubpass),
   FUEN_HOOK(CmdWaitEvents),
   FUEN_HOOK(CmdPipelineBarrier),
   FUEN_HOOK(CmdDraw),
   FUEN_HOOK(CmdDrawIndexed),
   FUEN_HOOK(CmdDrawIndirect),
   FUEN_HOOK(CmdDrawIndexedIndirect),
   FUEN_HOOK(CmdDispatch),
   FUEN_HOOK(CmdDispatchIndirect),
   FUEN_HOOK(CmdBindVertexBuffers),
   FUEN_HOOK(CmdBindIndexBuffer),
   FUEN_HOOK(CmdBindDescriptorSets),
   FUEN_HOOK(CmdClearColorImage),
   FUEN_HOOK(CmdCopyBufferToImage),
   FUEN_HOOK(CmdCopyImageToBuffer),
   FUEN_HOOK(CmdBlitImage),
   FUEN_HOOK(CmdCopyImage),
   FUEN_HOOK(CmdExecuteCommands),
   FUEN_HOOK(CmdCopyBuffer),
   FUEN_HOOK(CmdFillBuffer),
   FUEN_HOOK(CmdUpdateBuffer),
   FUEN_HOOK(CmdBindPipeline),
   FUEN_HOOK(CmdPushConstants),
};

#undef FUEN_HOOK
#undef FUEN_ALIAS

PFN_vkVoidFunction findFunctionPtr(const char* name) {
	auto it = funcPtrTable.find(std::string_view(name));
	if(it == funcPtrTable.end()) {
		return nullptr;
	}

   return reinterpret_cast<PFN_vkVoidFunction>(it->second);
}

// We make sure this way that e.g. calling vkGetInstanceProcAddr with
// vkGetInstanceProcAddr as funcName parameter returns itself.
// Not sure how important that is though.
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance ini, const char* funcName) {
	// Check if we hooked it. If we didn't hook it and ini is invalid,
	// return nullptr.
	auto ptr = fuen::findFunctionPtr(funcName);
	if(ptr || !ini) {
		return ptr;
	}

	// If it's not hooked, just forward it to the next chain link
	auto* inid = fuen::findData<fuen::Instance>(ini);
	if(!inid || !inid->dispatch.GetInstanceProcAddr) {
		dlg_error("invalid instance data: {}", fuen::handleCast(ini));
		return nullptr;
	}

	return inid->dispatch.GetInstanceProcAddr(ini, funcName);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice dev, const char* funcName) {
   auto ptr = fuen::findFunctionPtr(funcName);
   if(ptr || !dev) {
	   return ptr;
   }

   auto* devd = fuen::findData<fuen::Device>(dev);
   if(!devd || !devd->dispatch.GetDeviceProcAddr) {
		dlg_error("invalid device data: {}", fuen::handleCast(dev));
	   return nullptr;
   }

   return devd->dispatch.GetDeviceProcAddr(dev, funcName);
}

} // namespace fuen

// TODO: not sure why we need this on windows
#ifdef _WIN32
	#define FUEN_EXPORT __declspec(dllexport)
#else
	#define FUEN_EXPORT VK_LAYER_EXPORT
#endif

// Global layer entry points
extern "C" FUEN_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance ini, const char* funcName) {
   dlg_info("fuen get ini proc addr");
	return fuen::GetInstanceProcAddr(ini, funcName);
}

extern "C" FUEN_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetDeviceProcAddr(VkDevice dev, const char* funcName) {
   dlg_info("fuen get device proc addr");
	return fuen::GetDeviceProcAddr(dev, funcName);
}