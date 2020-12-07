#pragma once

#include <vulkan/vulkan.h>

#if defined(_WIN32) || defined(__CYGWIN__)
  // Thanks, microsoft.
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN 1
  #endif // WIN32_LEAN_AND_MEAN
  #ifndef NOMINMAX
    #define NOMINMAX 1
  #endif // NOMINMAX

  #include <windows.h>

  #undef near
  #undef far
  #undef ERROR
  #undef MemoryBarrier
  #undef WIN32_LEAN_AND_MEAN
  #undef NOMINMAX
#else
  #include <dlfcn.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// WIP sketch of a API
// Public API kept in vulkan-like code style.
// The functions must be externally synchronized for a given overaly.

typedef struct FuenOverlayT { void* data; } FuenOverlay; // Opaque

/// Creates an overlay for the swapchain last created for the given device.
/// Return NULL on failure. Note that there might be a data race when you
/// create swapchains from multiple threads. No swapchain must be in creation
/// for the same device in another thread while this is called.
/// The overlay will automatically be moved to a new swapchain if it is created
/// with the swapchain associated to this layer in the oldSwapchain
/// member of the VkSwapchainCreateInfoKHR.
/// By default, it is hidden but you could immediately call fuenOverlayShow
/// after this function to make it visible.
///
/// The name is intentionally horribly long to state as much of the terribleness
/// of this function as possible.
/// Due to the way vulkan layers work, only handles with embedded dispatch
/// table (i.e. device and instance) can be passed directly between application
/// and layer. Therefore the function to create an overlay is designed like
/// this.
/// TODO: allow to destroy overlay again.
typedef FuenOverlay (*PFN_fuenCreateOverlayForLastCreatedSwapchain)(VkDevice);

typedef void (*PFN_fuenOverlayShow)(FuenOverlay, bool show);

typedef void (*PFN_fuenOverlayMouseMoveEvent)(FuenOverlay, int x, int y);

// They return whether the event was processed by the overlay
typedef bool (*PFN_fuenOverlayMouseButtonEvent)(FuenOverlay, unsigned button, bool press);
typedef bool (*PFN_fuenOverlayMouseWheelEvent)(FuenOverlay, float x, float y);

// TODO: keycode and modifiers enum
typedef bool (*PFN_fuenOverlayKeyEvent)(FuenOverlay, uint32_t keycode, bool pressed);
typedef bool (*PFN_fuenOverlayTextEvent)(FuenOverlay, const char* utf8);
typedef void (*PFN_fuenOverlayKeyboardModifier)(FuenOverlay, uint32_t mod, bool active);

typedef struct FuenApi {
	PFN_fuenCreateOverlayForLastCreatedSwapchain createOverlayForLastCreatedSwapchain;

	PFN_fuenOverlayShow overlayShow;

	PFN_fuenOverlayMouseMoveEvent overlayMouseMoveEvent;
	PFN_fuenOverlayMouseButtonEvent overlayMouseButtonEvent;
	PFN_fuenOverlayMouseWheelEvent overlayMouseWheelEvent;
	PFN_fuenOverlayKeyEvent overlayKeyEvent;
	PFN_fuenOverlayTextEvent overlayTextEvent;
	PFN_fuenOverlayKeyboardModifier overlayKeyboardModifier;
} FuenApi;

/// Must be called only *after* a vulkan device was created.
/// Will remain valid only as long as the vulkan device is valid.
inline bool fuenLoadApi(FuenApi* api) {
	// We don't actually load a library here. If fuen was loaded as a
	// layer, the shared library must already be present. Otherwise,
	// we want this to fail anyways.

#if defined(_WIN32) || defined(__CYGWIN__)
	#define mdlsym(procName) (PFN_##procName) ((void(*)()) GetProcAddress(handle, #procName))
	// We don't have to call FreeLibrary since GetModuleHandle does not increase ref count
	#define mdlclose()

	HMODULE handle = GetModuleHandleA("libVkLayer_fuencaliente.dll");
#else
	#define mdlsym(procName) (PFN_##procName) dlsym(handle, #procName)
	// We have to call dlclose since our dlopen increases the reference count.
	#define mdlclose() dlclose(handle)

	void* handle = dlopen("libVkLayer_fuencaliente.so", RTLD_NOLOAD | RTLD_LAZY);
#endif

	if(!handle) {
		// In this case, the layer wasn't loaded.
		return false;
	}


	api->createOverlayForLastCreatedSwapchain = mdlsym(fuenCreateOverlayForLastCreatedSwapchain);
	if(!api->createOverlayForLastCreatedSwapchain) {
		return false;
	}

	// yeah well just assume they'll load fine if overlayShow loaded.
	api->overlayShow = mdlsym(fuenOverlayShow);
	api->overlayMouseMoveEvent = mdlsym(fuenOverlayMouseMoveEvent);
	api->overlayMouseButtonEvent = mdlsym(fuenOverlayMouseButtonEvent);
	api->overlayMouseWheelEvent = (PFN_fuenOverlayMouseWheelEvent) mdlsym(fuenOverlayMouseWheelEvent);
	api->overlayKeyEvent = (PFN_fuenOverlayKeyEvent) mdlsym(fuenOverlayKeyEvent);
	api->overlayTextEvent = (PFN_fuenOverlayTextEvent) mdlsym(fuenOverlayTextEvent);
	api->overlayKeyboardModifier = (PFN_fuenOverlayKeyboardModifier) mdlsym(fuenOverlayKeyboardModifier);

	mdlclose();

#undef mdlsym
#undef mdlclose

	return true;
}

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus