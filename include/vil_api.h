// VIL: Vulkan Live Introspection & Debugging Layer.
// Copyright (C) 2020-2021 Jan Kelling
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <stdbool.h>
#include <stdint.h>

#if defined(_WIN32) || defined(__CYGWIN__)
  // XXX NOTE ATTENTION: this header includes windows.h.
  // Just as a fair warning, this might break your application and will to live.
  // Nothing we can really do about it, we need the functions to load func pointers.
  // A possible workaround would be to move the vilLoadApi implementation
  // out of the header, into a separate file.
  #include <windows.h>

  // Needs to be defined when renamed the dll or compiled it e.g.
  // via MinGW. Must match the name of the lib that is loaded/registered as layer.
  #ifndef VIL_LIB_NAME
	#define VIL_LIB_NAME "VkLayer_live_introspection.dll"
  #endif // VIL_LIB_NAME
#else
  #include <dlfcn.h>
  #include <errno.h>

  #ifndef VIL_LIB_NAME
	#define VIL_LIB_NAME "libVkLayer_live_introspection.so"
  #endif // VIL_LIB_NAME
#endif

// Forward delcartion from vulkan.h to not include it here.
typedef struct VkDevice_T* VkDevice;

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// WIP sketch of a API
// Public API kept in vulkan-like code style.
// The functions must be externally synchronized for a given overaly.
// NOTE: header-only by design, no library needs to be linked besides
// 'dl' on unix and 'kernel32' on windows (usually automatically done).
// This will call symbols from the VIL layer, if it is loaded.

typedef struct VilOverlayT* VilOverlay;

// All keys that are handled by vil.
// Derived from linux/input-event-codes.h
enum VilKey {
	VilKeyEscape = 1,
	VilKeyTab = 15,
	VilKeyLeft = 105,
	VilKeyRight = 106,
	VilKeyUp = 103,
	VilKeyDown = 108,
	VilKeyPageUp = 104,
	VilKeyPageDown = 109,
	VilKeyHome = 102,
	VilKeyEnd = 107,
	VilKeyInsert = 110,
	VilKeyDelete = 111,
	VilKeyBackspace = 14,
	VilKeySpace = 57,
	VilKeyEnter = 28,
	VilKeyPadEnter = 96,
	VilKeyA = 30,
	VilKeyC = 46,
	VilKeyD = 32,
	VilKeyE = 18,
	VilKeyQ = 16,
	VilKeyV = 47,
	VilKeyS = 31,
	VilKeyW = 17,
	VilKeyX = 45,
	VilKeyY = 21,
	VilKeyZ = 44,
	VilKeyBackslash = 43,
};

enum VilKeyMod {
	VilKeyModShift = (1u << 0),
	VilKeyModCtrl = (1u << 1),
	VilKeyModAlt = (1u << 2),
	VilKeyModSuper = (1u << 3),
};

// Creates an overlay for the swapchain last created for the given device.
// Return NULL on failure. Note that there might be a data race when you
// create swapchains from multiple threads. No swapchain must be in creation
// for the same device in another thread while this is called.
// The overlay will automatically be moved to a new swapchain if it is created
// with the swapchain associated to this layer in the oldSwapchain
// member of the VkSwapchainCreateInfoKHR.
// By default, it is hidden but you could immediately call vilOverlayShow
// after this function to make it visible.
//
// The name is intentionally horribly long to state as much of the terribleness
// of this function as possible.
// Due to the way vulkan layers work, only handles with embedded dispatch
// table (i.e. device and instance) can be passed directly between application
// and layer. Therefore the function to create an overlay is designed like
// this.
typedef VilOverlay (*PFN_vilCreateOverlayForLastCreatedSwapchain)(VkDevice);

// Toggles whether the overlay is visible
typedef void (*PFN_vilOverlayShow)(VilOverlay, bool show);

// They return whether the event was processed by the overlay. So if they
// return true, the event should not be handled by the application again.
typedef bool (*PFN_vilOverlayMouseButtonEvent)(VilOverlay, unsigned button, bool press);
typedef bool (*PFN_vilOverlayMouseWheelEvent)(VilOverlay, float x, float y);
typedef bool (*PFN_vilOverlayKeyEvent)(VilOverlay, enum VilKey keycode, bool pressed);
typedef bool (*PFN_vilOverlayTextEvent)(VilOverlay, const char* utf8);

typedef void (*PFN_vilOverlayMouseMoveEvent)(VilOverlay, int x, int y);
typedef void (*PFN_vilOverlayKeyboardModifier)(VilOverlay, enum VilKeyMod mod, bool active);

typedef struct VilApi {
	PFN_vilCreateOverlayForLastCreatedSwapchain CreateOverlayForLastCreatedSwapchain;

	PFN_vilOverlayShow OverlayShow;

	PFN_vilOverlayMouseMoveEvent OverlayMouseMoveEvent;
	PFN_vilOverlayMouseButtonEvent OverlayMouseButtonEvent;
	PFN_vilOverlayMouseWheelEvent OverlayMouseWheelEvent;
	PFN_vilOverlayKeyEvent OverlayKeyEvent;
	PFN_vilOverlayTextEvent OverlayTextEvent;
	PFN_vilOverlayKeyboardModifier OverlayKeyboardModifier;
} VilApi;

// Must be called only *after* a vulkan device was created.
// The loaded api will remain valid only as long as the vulkan device is valid.
// Returns 0 on success, an error code otherwise.
static inline int vilLoadApi(VilApi* api) {
	// We don't actually load a library here. If vil was loaded as a
	// layer, the shared library must already be present. Otherwise,
	// we want this to fail anyways.

#if defined(_WIN32) || defined(__CYGWIN__)
	HMODULE handle = GetModuleHandleA(VIL_LIB_NAME);

	// We don't have to call FreeLibrary since GetModuleHandle does not increase ref count
	#define vilCloseLib()
	#define vilLoadSym(procName) *(FARPROC*) (&api->procName) = GetProcAddress(handle, "vil" #procName)
#else
	void* handle = dlopen(VIL_LIB_NAME, RTLD_NOLOAD | RTLD_LAZY);

	// We have to call dlclose since our dlopen increases the reference count.
	#define vilCloseLib() dlclose(handle)
	#define vilLoadSym(procName) *(void**) &(api->procName) = dlsym(handle, "vil" #procName)
#endif

	if(!handle) {
		// In this case, the layer wasn't loaded.
		return -1;
	}

	vilLoadSym(CreateOverlayForLastCreatedSwapchain);
	if(!api->CreateOverlayForLastCreatedSwapchain) {
		return -2;
	}

	// yeah well just assume they'll load fine if overlayShow loaded.
	vilLoadSym(OverlayShow);
	vilLoadSym(OverlayMouseMoveEvent);
	vilLoadSym(OverlayMouseButtonEvent);
	vilLoadSym(OverlayMouseWheelEvent);
	vilLoadSym(OverlayKeyEvent);
	vilLoadSym(OverlayTextEvent);
	vilLoadSym(OverlayKeyboardModifier);

	vilCloseLib();

#undef vilCloseLib
#undef vilLoadSym

	return 0;
}

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus
