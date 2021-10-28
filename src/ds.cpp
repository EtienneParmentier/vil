#include <ds.hpp>
#include <device.hpp>
#include <data.hpp>
#include <buffer.hpp>
#include <image.hpp>
#include <pipe.hpp>
#include <accelStruct.hpp>
#include <threadContext.hpp>
#include <util/util.hpp>
#include <util/profiling.hpp>

namespace vil {

// util
size_t descriptorSize(VkDescriptorType dsType) {
	switch(category(dsType)) {
		case DescriptorCategory::buffer: return sizeof(BufferDescriptor);
		case DescriptorCategory::image: return sizeof(ImageDescriptor);
		case DescriptorCategory::bufferView: return sizeof(BufferViewDescriptor);
		case DescriptorCategory::accelStruct: return sizeof(AccelStructDescriptor);
		case DescriptorCategory::inlineUniformBlock: return 1u;
		case DescriptorCategory::none:
			dlg_error("unreachable: Invalid descriptor category");
			return 0u;
	}

	dlg_error("unreachable");
	return 0u;
}

DescriptorStateRef::DescriptorStateRef(const DescriptorSet& ds) :
	layout(ds.layout.get()), data(ds.data), variableDescriptorCount(ds.variableDescriptorCount) {
}

DescriptorStateRef::DescriptorStateRef(DescriptorStateCopy& ds) :
	layout(ds.layout.get()),
	data(reinterpret_cast<std::byte*>(&ds) + sizeof(DescriptorStateCopy)),
	variableDescriptorCount(ds.variableDescriptorCount) {
}

void returnToPool(DescriptorSet& ds) {
	ds.~DescriptorSet();

	static_assert(offsetof(DescriptorPool::SetAlloc, storage) == 0u);
	auto& alloc = *std::launder(reinterpret_cast<DescriptorPool::SetAlloc*>(&ds));

	// unlink from used list


	// prev pointers don't matter for free list
	alloc.next = ds.pool->freeSets;
	ds.pool->freeSets = &alloc;
}

template<typename T>
void debugStatAdd(std::atomic<T>& dst, const T& val) {
#ifdef VIL_DEBUG_STATS
	dst.fetch_add(val, std::memory_order_relaxed);
#else // VIL_DEBUG_STATS
	(void) dst;
	(void) val;
#endif // VIL_DEBUG_STATS
}

template<typename T>
void debugStatSub(std::atomic<T>& dst, const T& val) {
#ifdef VIL_DEBUG_STATS
	auto before = dst.fetch_sub(val, std::memory_order_relaxed);
	dlg_assert(before >= val);
#else // VIL_DEBUG_STATS
	(void) dst;
	(void) val;
#endif // VIL_DEBUG_STATS
}

// Returns the total raw memory size needed by descriptor state of
// the given layout, with the given variable descriptor count.
size_t totalDescriptorMemSize(const DescriptorSetLayout& layout, u32 variableDescriptorCount) {
	if(layout.bindings.empty()) {
		return 0;
	}

	auto& last = layout.bindings.back();
	size_t ret = last.offset;
	auto lastCount = last.descriptorCount;

	if(last.flags & VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT) {
		ret += variableDescriptorCount;
	}

	ret += lastCount * descriptorSize(last.descriptorType);
	return ret;
}

bool compatible(const DescriptorSetLayout& da, const DescriptorSetLayout& db) {
	if(da.bindings.size() != db.bindings.size()) {
		return false;
	}

	// bindings are sorted by binding number so we can simply compare
	// them in order
	for(auto b = 0u; b < da.bindings.size(); ++b) {
		auto& ba = da.bindings[b];
		auto& bb = db.bindings[b];

		if(ba.binding != bb.binding ||
				ba.descriptorCount != bb.descriptorCount ||
				ba.descriptorType != bb.descriptorType ||
				ba.stageFlags != bb.stageFlags) {
			return false;
		}

		// immutable samplers
		if(ba.binding == VK_DESCRIPTOR_TYPE_SAMPLER ||
				ba.binding == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
			if(bool(ba.immutableSamplers) != bool(bb.immutableSamplers)) {
				return false;
			}

			if(ba.immutableSamplers) {
				dlg_assert(ba.descriptorCount == bb.descriptorCount);
				for(auto e = 0u; e < ba.descriptorCount; ++e) {
					// TODO: consider compatible (instead of just same)
					// samplers as well?
					if(ba.immutableSamplers[e] != bb.immutableSamplers[e]) {
						return false;
					}
				}
			}
		}
	}

	return true;
}

void initImmutableSamplers(DescriptorStateRef state) {
	ZoneScoped;

	for(auto b = 0u; b < state.layout->bindings.size(); ++b) {
		// If the binding holds immutable samplers, fill them in.
		// We do this so we don't have to check for immutable samplers
		// every time we read a binding. Also needed for correct
		// invalidation tracking.
		if(state.layout->bindings[b].immutableSamplers.get()) {
			dlg_assert(needsSampler(state.layout->bindings[b].descriptorType));
			auto binds = images(state, b);

			for(auto e = 0u; e < binds.size(); ++e) {
				auto sampler = state.layout->bindings[b].immutableSamplers[e];
				dlg_assert(sampler);
				dlg_assert(sampler->handle);

				binds[e].sampler = sampler;
			}
		}
	}
}

void initDescriptorState(std::byte* data,
		const DescriptorSetLayout& layout, u32 variableDescriptorCount) {
	auto bindingSize = totalDescriptorMemSize(layout, variableDescriptorCount);
	std::memset(data, 0x0, bindingSize);

	/*
	auto it = data;
	for(auto& binding : layout->bindings) {
		auto count = binding.descriptorCount;
		if(binding.flags & VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT) {
			count = variableDescriptorCount;
		}

		switch(category(binding.descriptorType)) {
			case DescriptorCategory::buffer:
				new(it) BufferDescriptor[count];
				it += count * sizeof(BufferDescriptor);
				break;
			case DescriptorCategory::image:
				new(it) ImageDescriptor[count];
				it += count * sizeof(ImageDescriptor);
				break;
			case DescriptorCategory::bufferView:
				new(it) BufferViewDescriptor[count];
				it += count * sizeof(BufferViewDescriptor);
				break;
			case DescriptorCategory::accelStruct:
				new(it) AccelStructDescriptor[count];
				it += count * sizeof(AccelStructDescriptor);
				break;
			case DescriptorCategory::inlineUniformBlock:
				// nothing to initialize, just raw data here
				it += count;
				break;
			case DescriptorCategory::none:
				dlg_error("unreachable: invalid descriptor type");
				break;
		}
	}
	*/
}

void copy(DescriptorStateRef dst, unsigned dstBindID, unsigned dstElemID,
		DescriptorStateRef src, unsigned srcBindID, unsigned srcElemID) {
	auto& srcLayout = src.layout->bindings[srcBindID];
	auto& dstLayout = dst.layout->bindings[dstBindID];
	dlg_assert(srcLayout.descriptorType == dstLayout.descriptorType);

	switch(category(dstLayout.descriptorType)) {
		case DescriptorCategory::image: {
			auto srcCopy = images(src, srcBindID)[srcElemID];
			auto& dstBind = images(dst, dstBindID)[dstElemID];
			dstBind.imageView = std::move(srcCopy.imageView);
			dstBind.layout = srcCopy.layout;

			if(!dstLayout.immutableSamplers.get()) {
				dstBind.sampler = std::move(srcCopy.sampler);
			}
			break;
		} case DescriptorCategory::buffer: {
			buffers(dst, dstBindID)[dstElemID] = buffers(src, srcBindID)[srcElemID];
			break;
		} case DescriptorCategory::bufferView: {
			bufferViews(dst, dstBindID)[dstElemID] = bufferViews(src, srcBindID)[srcElemID];
			break;
		} case DescriptorCategory::inlineUniformBlock: {
			// NOTE: we copy byte-by-byte here which is inefficient. Would
			// have to rework the entire code structure of 'copy'. Shouldn't
			// be a huge problem tho, inline uniform blocks should be very
			// small anyways.
			auto srcBuf = inlineUniformBlock(src, srcBindID);
			auto dstBuf = inlineUniformBlock(src, dstBindID);
			dlg_assert(srcElemID < srcBuf.size());
			dlg_assert(dstElemID < dstBuf.size());
			dstBuf[dstElemID] = srcBuf[srcElemID];
			break;
		} case DescriptorCategory::accelStruct: {
			accelStructs(dst, dstBindID)[dstElemID] = accelStructs(src, srcBindID)[srcElemID];
			break;
		} case DescriptorCategory::none:
			dlg_error("unreachable: Invalid descriptor type");
			break;
	}
}

void destroyDsState(DescriptorStateRef state) {
	ZoneScopedN("destroyDsState");

	// destroy bindings, mainly to release intrusive ptrs
	for(auto b = 0u; b < state.layout->bindings.size(); ++b) {
		auto& binding = state.layout->bindings[b];
		if(!descriptorCount(state, b)) {
			continue;
		}

		switch(category(binding.descriptorType)) {
			case DescriptorCategory::buffer: {
				auto b = buffers(state, binding.binding);
				std::destroy(b.begin(), b.end());
				break;
			} case DescriptorCategory::bufferView: {
				auto b = bufferViews(state, binding.binding);
				std::destroy(b.begin(), b.end());
				break;
			} case DescriptorCategory::image: {
				auto b = images(state, binding.binding);
				std::destroy(b.begin(), b.end());
				break;
			} case DescriptorCategory::accelStruct: {
				auto b = accelStructs(state, binding.binding);
				std::destroy(b.begin(), b.end());
				break;
			} case DescriptorCategory::inlineUniformBlock: {
				// no-op, we just have raw bytes here
				break;
			} case DescriptorCategory::none:
				dlg_error("unreachable: invalid descriptor type");
				break;
		}
	}
}

void DescriptorStateCopy::Deleter::operator()(DescriptorStateCopy* copy) const {
	destroyDsState(DescriptorStateRef(*copy));

	auto memSize = sizeof(DescriptorStateCopy);
	memSize += totalDescriptorMemSize(*copy->layout, copy->variableDescriptorCount);
	debugStatSub(DebugStats::get().descriptorCopyMem, u32(memSize));
	debugStatSub(DebugStats::get().aliveDescriptorCopies, 1u);

	copy->~DescriptorStateCopy();

	// we allocated the memory as std::byte array, so we have to free
	// it like that. We don't have to call any other destructors of
	// Binding elements since they are all trivial
	auto ptr = reinterpret_cast<std::byte*>(copy);
	TracyFreeS(ptr, 8);
	delete[] ptr;
}

DescriptorStateCopyPtr copyLockedState(const DescriptorSet& set) {
	ZoneScoped;

	// NOTE: when this assert fails somewhere, we have to adjust the code (storing stuff
	// that is up-to-pointer-aligned directly behind the state object in memory).
	static_assert(sizeof(DescriptorStateCopy) % alignof(void*) == 0u);
	assertOwned(set.mutex);

	auto bindingSize = totalDescriptorMemSize(*set.layout, set.variableDescriptorCount);
	auto memSize = sizeof(DescriptorStateCopy) + bindingSize;

	auto* mem = new std::byte[memSize]();
	TracyAllocS(mem, memSize, 8);

	debugStatAdd(DebugStats::get().descriptorCopyMem, u32(memSize));
	debugStatAdd(DebugStats::get().aliveDescriptorCopies, 1u);

	auto* copy = new(mem) DescriptorStateCopy();
	dlg_assert(reinterpret_cast<std::byte*>(copy) == mem);

	copy->variableDescriptorCount = set.variableDescriptorCount;
	copy->layout = set.layout;

	DescriptorStateRef srcRef(set);
	auto dstRef = srcRef;
	dstRef.data = mem + sizeof(DescriptorStateCopy);

	initDescriptorState(dstRef.data, *set.layout, set.variableDescriptorCount);
	initImmutableSamplers(dstRef);

	// copy descriptors
	for(auto b = 0u; b < set.layout->bindings.size(); ++b) {
		for(auto e = 0u; e < descriptorCount(srcRef, b); ++e) {
			vil::copy(dstRef, b, e, srcRef, b, e);
		}
	}

	return DescriptorStateCopyPtr(copy);
}

u32 descriptorCount(DescriptorStateRef state, unsigned binding) {
	dlg_assert(state.layout);
	dlg_assert(binding < state.layout->bindings.size());
	auto& layout = state.layout->bindings[binding];
	if(layout.flags & VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT) {
		return state.variableDescriptorCount;
	}

	return layout.descriptorCount;
}

u32 totalDescriptorCount(DescriptorStateRef state) {
	auto ret = 0u;
	for(auto i = 0u; i < state.layout->bindings.size(); ++i) {
		ret += descriptorCount(state, i);
	}

	return ret;
}

std::unique_lock<DebugMutex> checkResolveCow(DescriptorSet& ds) {
	std::unique_lock objLock(ds.mutex);
	if(!ds.cow) {
		return objLock;
	}

	std::unique_lock cowLock(ds.cow->mutex);
	ds.cow->copy = copyLockedState(ds);
	// disconnect
	ds.cow->ds = nullptr;
	ds.cow = nullptr;

	return objLock;
}

void destroy(DescriptorSet& ds, bool unlink) {
	dlg_assert(ds.dev);

	// NOTE: no need to remove from descriptor pool, that will
	// be done externally

	// no need to keep lock here, ds can't be accessed anymore
	checkResolveCow(ds);
	destroyDsState(ds);

	// Return data to pool. We don't have to lock the pool mutex
	// for this, external sync guaranteed by spec and it's not
	// accessed by us.
	if(ds.data < ds.pool->data.get() ||
			ds.pool->data.get() + ds.pool->dataSize <= ds.data) {
		// See AllocateDescriptorSets. We had to choose a slow path due
		// to fragmentation
		dlg_trace("free independent DS data slot");
		delete[] ds.data;
		dlg_assert(!ds.setEntry);
	} else if(unlink) {
		dlg_assert(ds.pool->flags & VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
		// PERF: could have a fast path not doing this when the whole
		// pool is being reset.

		// unlink setEntry
		dlg_assert(!ds.setEntry->next == (ds.setEntry == ds.pool->highestEntry));
		dlg_assert(!ds.setEntry->prev == (ds.setEntry == ds.pool->usedEntries));

		if(ds.setEntry->next) {
			ds.setEntry->next->prev = ds.setEntry->prev;
		} else {
			ds.pool->highestEntry = ds.setEntry->prev;
			ds.pool->highestOffset = 0u;
			if(ds.setEntry->prev) {
				ds.pool->highestOffset = ds.setEntry->prev->offset + ds.setEntry->prev->size;
			}
		}

		if(ds.setEntry->prev) {
			ds.setEntry->prev->next = ds.setEntry->next;
		} else {
			ds.pool->usedEntries = ds.setEntry->next;
		}

		if(ds.setEntry == ds.pool->lastEntry) {
			ds.pool->lastEntry = ds.setEntry->prev;
		}

		// return to free list
		ds.setEntry->next = ds.pool->freeEntries;
		ds.setEntry->prev = nullptr;
		ds.pool->freeEntries = ds.setEntry;
	} else {
		dlg_assert(!ds.setEntry);
	}

	// return to pool
	if(unlink) {
		static_assert(offsetof(DescriptorPool::SetAlloc, storage) == 0u);
		auto& alloc = *std::launder(reinterpret_cast<DescriptorPool::SetAlloc*>(&ds));

		// unlink from used list
		if(alloc.next) {
			alloc.next->prev = alloc.prev;
		}

		dlg_assert(!alloc.prev == (&alloc == ds.pool->aliveSets));
		if(alloc.prev) {
			alloc.prev->next = alloc.next;
		} else {
			ds.pool->aliveSets = alloc.next;
		}

		// prev pointers don't matter for free list
		alloc.next = ds.pool->freeSets;
		ds.pool->freeSets = &alloc;
	}

	ds.~DescriptorSet();
	debugStatSub(DebugStats::get().aliveDescriptorSets, 1u);
}

span<BufferDescriptor> buffers(DescriptorStateRef state, unsigned binding) {
	dlg_assert(binding < state.layout->bindings.size());

	auto& layout = state.layout->bindings[binding];
	dlg_assert(category(layout.descriptorType) == DescriptorCategory::buffer);

	auto count = layout.descriptorCount;
	if(layout.flags & VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT) {
		count = state.variableDescriptorCount;
	}

	auto ptr = state.data + layout.offset;
	auto d = std::launder(reinterpret_cast<BufferDescriptor*>(ptr));
	return {d, count};
}
span<ImageDescriptor> images(DescriptorStateRef state, unsigned binding) {
	dlg_assert(binding < state.layout->bindings.size());

	auto& layout = state.layout->bindings[binding];
	dlg_assert(category(layout.descriptorType) == DescriptorCategory::image);

	auto count = layout.descriptorCount;
	if(layout.flags & VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT) {
		count = state.variableDescriptorCount;
	}

	auto ptr = state.data + layout.offset;
	auto d = std::launder(reinterpret_cast<ImageDescriptor*>(ptr));
	return {d, count};
}
span<BufferViewDescriptor> bufferViews(DescriptorStateRef state, unsigned binding) {
	dlg_assert(binding < state.layout->bindings.size());

	auto& layout = state.layout->bindings[binding];
	dlg_assert(category(layout.descriptorType) == DescriptorCategory::bufferView);

	auto count = layout.descriptorCount;
	if(layout.flags & VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT) {
		count = state.variableDescriptorCount;
	}

	auto ptr = state.data + layout.offset;
	auto d = std::launder(reinterpret_cast<BufferViewDescriptor*>(ptr));
	return {d, count};
}
span<std::byte> inlineUniformBlock(DescriptorStateRef state, unsigned binding) {
	dlg_assert(binding < state.layout->bindings.size());

	auto& layout = state.layout->bindings[binding];
	dlg_assert(layout.descriptorType == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT);

	auto count = layout.descriptorCount;
	if(layout.flags & VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT) {
		count = state.variableDescriptorCount;
	}

	auto ptr = state.data + layout.offset;
	return {ptr, count};
}
span<AccelStructDescriptor> accelStructs(DescriptorStateRef state, unsigned binding) {
	dlg_assert(binding < state.layout->bindings.size());

	auto& layout = state.layout->bindings[binding];
	dlg_assert(category(layout.descriptorType) == DescriptorCategory::accelStruct);

	auto count = layout.descriptorCount;
	if(layout.flags & VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT) {
		count = state.variableDescriptorCount;
	}

	auto ptr = state.data + layout.offset;
	auto d = std::launder(reinterpret_cast<AccelStructDescriptor*>(ptr));
	return {d, count};
}

// DescriptorPool impl
DescriptorPool::~DescriptorPool() {
	if(!dev) {
		return;
	}

	// NOTE: we don't need a lock here:
	// While the ds pool is being destroyed, no descriptor sets from it
	// can be created or destroyed in another thread, that would always be a
	// race. So accessing this vector is safe.
	// (Just adding a lock here would furthermore result in deadlocks due
	// to the mutexes locked inside the loop, don't do it!)
	// We don't use a for loop since the descriptors remove themselves
	// on destruction
	// while(!descriptorSets.empty()) {
	// 	if(HandleDesc<VkDescriptorSet>::wrap) {
	// 		// TODO: ugh, this is terrible, should find a cleaner solution
	// 		// auto h = u64ToHandle<VkDescriptorSet>(reinterpret_cast<std::uintptr_t>(descriptorSets[0]));
	// 		// dev->descriptorSets.mustErase(h);
	// 		delete descriptorSets[0];
	// 	} else {
	// 		auto* ds = descriptorSets[0];
	// 		dev->descriptorSets.mustErase(ds->handle);
	// 	}
	// }

	while(!descriptorSets.empty()) {
		auto* ds = descriptorSets[0];
		if(!HandleDesc<VkDescriptorSet>::wrap) {
			dev->descriptorSets.mustErase(ds->handle);
		}
		ds->~DescriptorSet();
		// no need to return to pool
	}

	dlg_assert(!usedEntries);
	debugStatSub(DebugStats::get().descriptorPoolMem, dataSize);
	TracyFree(data.get());
}

DescriptorSetLayout::~DescriptorSetLayout() {
	if(!dev) {
		return;
	}

	// ds layouts are never used directly by command buffers
	dlg_assert(!refRecords);
	dlg_assert(handle);

	dev->dispatch.DestroyDescriptorSetLayout(dev->handle, handle, nullptr);
}

DescriptorUpdateTemplate::~DescriptorUpdateTemplate() {
	if(!dev) {
		return;
	}

	// never used directly by command buffers
	dlg_assert(!refRecords);
	dlg_assert(handle);

	dev->dispatch.DestroyDescriptorUpdateTemplate(dev->handle, handle, nullptr);
}

// util
DescriptorCategory category(VkDescriptorType type) {
	switch(type) {
		case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
		case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
		case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
		case VK_DESCRIPTOR_TYPE_SAMPLER:
		case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			return DescriptorCategory::image;
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
			return DescriptorCategory::buffer;
		case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
		case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
			return DescriptorCategory::bufferView;
		case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT:
			return DescriptorCategory::inlineUniformBlock;
		case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
			return DescriptorCategory::accelStruct;
		default:
			dlg_trace("Unsupported descriptor type: {}", type);
			return DescriptorCategory::none;
	}
}

bool needsSampler(VkDescriptorType type) {
	switch(type) {
		case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
		case VK_DESCRIPTOR_TYPE_SAMPLER:
			return true;
		default:
			return false;
	}
}

bool needsBoundSampler(const DescriptorSetLayout& dsl, unsigned binding) {
	auto& bind = dsl.bindings[binding];
	return needsSampler(bind.descriptorType) && !bind.immutableSamplers;
}

bool needsImageView(VkDescriptorType type) {
	switch(type) {
		case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
		case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
		case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
		case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			return true;
		default:
			return false;
	}
}

bool needsImageLayout(VkDescriptorType type) {
	switch(type) {
		case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
		case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
		case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
		case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			return true;
		default:
			return false;
	}
}

bool needsDynamicOffset(VkDescriptorType type) {
	switch(type) {
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
			return true;
		default:
			return false;
	}
}

// dsLayout
VKAPI_ATTR VkResult VKAPI_CALL CreateDescriptorSetLayout(
		VkDevice                                    device,
		const VkDescriptorSetLayoutCreateInfo*      pCreateInfo,
		const VkAllocationCallbacks*                pAllocator,
		VkDescriptorSetLayout*                      pSetLayout) {
	// NOTE: we don't use host allocators here since this handle is potentially
	// kept alive inside the layer, preventing us from passing an application
	// allocator to the destruction function
	// See design.md on allocators.
	(void) pAllocator;

	auto& dev = getDevice(device);

	// unwrap immutable sampler handles
	auto nci = *pCreateInfo;

	ThreadMemScope memScope;
	auto nbindings = memScope.copy(nci.pBindings, nci.bindingCount);
	nci.pBindings = nbindings.data();

	for(auto& bind : nbindings) {
		if(!needsSampler(bind.descriptorType) || bind.descriptorCount == 0 ||
				!bind.pImmutableSamplers) {
			continue;
		}

		auto handles = memScope.alloc<VkSampler>(bind.descriptorCount);
		for(auto i = 0u; i < bind.descriptorCount; ++i) {
			auto& sampler = get(dev, bind.pImmutableSamplers[i]);
			handles[i] = sampler.handle;
		}

		bind.pImmutableSamplers = handles.data();
	}

	auto res = dev.dispatch.CreateDescriptorSetLayout(dev.handle, &nci, nullptr, pSetLayout);
	if(res != VK_SUCCESS) {
		return res;
	}

	auto dsLayoutPtr = IntrusivePtr<DescriptorSetLayout>(new DescriptorSetLayout());
	auto& dsLayout = *dsLayoutPtr;
	dsLayout.objectType = VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT;
	dsLayout.dev = &dev;
	dsLayout.handle = *pSetLayout;
	dsLayout.flags = nci.flags;

	auto* flagsInfo = findChainInfo<VkDescriptorSetLayoutBindingFlagsCreateInfo,
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO>(*pCreateInfo);
	flagsInfo = (flagsInfo && flagsInfo->bindingCount == 0u) ? nullptr : flagsInfo;
	dlg_assert(!flagsInfo || flagsInfo->bindingCount == pCreateInfo->bindingCount);

	for(auto i = 0u; i < pCreateInfo->bindingCount; ++i) {
		const auto& bind = pCreateInfo->pBindings[i];
		ensureSize(dsLayout.bindings, bind.binding + 1);

		auto& dst = dsLayout.bindings[bind.binding];
		dst.binding = bind.binding;
		dst.descriptorCount = bind.descriptorCount;
		dst.descriptorType = bind.descriptorType;
		dst.stageFlags = bind.stageFlags;
		dst.flags = flagsInfo ? flagsInfo->pBindingFlags[i] : 0u;

		if(needsSampler(bind.descriptorType) && dst.descriptorCount > 0 &&
				bind.pImmutableSamplers) {
			// Couldn't find in the spec whether this is allowed or not.
			// But it seems incorrect to me, we might not handle it correctly
			// everywhere.
			dlg_assert(!(dst.flags & VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT));
			dst.immutableSamplers = std::make_unique<IntrusivePtr<Sampler>[]>(dst.descriptorCount);
			for(auto e = 0u; e < dst.descriptorCount; ++e) {
				dst.immutableSamplers[e] = getPtr(dev, bind.pImmutableSamplers[e]);
			}

			dsLayout.immutableSamplers = true;
		}
	}

	// number offsets
	auto off = 0u;
	for(auto b = 0u; b < dsLayout.bindings.size(); ++b) {
		auto& bind = dsLayout.bindings[b];
		bind.offset = off;

		off += unsigned(bind.descriptorCount * descriptorSize(bind.descriptorType));

		auto varCount = !!(bind.flags & VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT);
		dlg_assert(b + 1 == dsLayout.bindings.size() || !varCount);

		if(needsDynamicOffset(bind.descriptorType)) {
			// VUID-VkDescriptorSetLayoutBindingFlagsCreateInfo-pBindingFlags-03015
			dlg_assert(!varCount);
			bind.dynOffset = dsLayout.numDynamicBuffers;
			dsLayout.numDynamicBuffers += bind.descriptorCount;
		}
	}

	*pSetLayout = castDispatch<VkDescriptorSetLayout>(dsLayout);
	dev.dsLayouts.mustEmplace(*pSetLayout, std::move(dsLayoutPtr));

	return res;
}

VKAPI_ATTR void VKAPI_CALL DestroyDescriptorSetLayout(
		VkDevice                                    device,
		VkDescriptorSetLayout                       descriptorSetLayout,
		const VkAllocationCallbacks*                pAllocator) {
	if(!descriptorSetLayout) {
		return;
	}

	auto& dev = getDevice(device);
	dev.dsLayouts.mustErase(descriptorSetLayout);

	// NOTE: We intenntionally don't destruct the handle here, handle might
	// need to be kept alive, they have shared ownership. Destroyed
	// in handle destructor.
	// dev.dispatch.DestroyDescriptorSetLayout(dev.handle, dsl.handle, pAllocator);
	(void) pAllocator;
}

// dsPool
void initResetPoolEntries(DescriptorPool& dsPool) {
	for(auto i = 1u; i + 1 < dsPool.maxSets; ++i) {
		dsPool.entries[i].prev = &dsPool.entries[i - 1];
		dsPool.entries[i].next = &dsPool.entries[i + 1];
	}

	if(dsPool.maxSets > 1) {
		dsPool.entries[0].next = &dsPool.entries[1];
		dsPool.entries[dsPool.maxSets - 1].prev = &dsPool.entries[dsPool.maxSets - 2];
	}

	dsPool.freeEntries = &dsPool.entries[0];

	dsPool.usedEntries = nullptr;
	dsPool.lastEntry = nullptr;
	dsPool.highestEntry = nullptr;
}

void initResetPoolSets(DescriptorPool& dsPool) {
	for(auto i = 1u; i + 1 < dsPool.maxSets; ++i) {
		dsPool.sets[i].prev = &dsPool.sets[i - 1];
		dsPool.sets[i].next = &dsPool.sets[i + 1];
	}

	if(dsPool.maxSets > 1) {
		dsPool.sets[0].next = &dsPool.sets[1];
		dsPool.sets[dsPool.maxSets - 1].prev = &dsPool.sets[dsPool.maxSets - 2];
	}

	dsPool.freeSets = &dsPool.sets[0];
	dsPool.aliveSets = nullptr;
}

VKAPI_ATTR VkResult VKAPI_CALL CreateDescriptorPool(
		VkDevice                                    device,
		const VkDescriptorPoolCreateInfo*           pCreateInfo,
		const VkAllocationCallbacks*                pAllocator,
		VkDescriptorPool*                           pDescriptorPool) {
	auto& dev = getDevice(device);

	auto res = dev.dispatch.CreateDescriptorPool(dev.handle, pCreateInfo, pAllocator, pDescriptorPool);
	if(res != VK_SUCCESS) {
		return res;
	}

	auto dsPoolPtr = std::make_unique<DescriptorPool>();
	auto& dsPool = *dsPoolPtr;
	dsPool.objectType = VK_OBJECT_TYPE_DESCRIPTOR_POOL;
	dsPool.dev = &dev;
	dsPool.handle = *pDescriptorPool;
	dsPool.maxSets = pCreateInfo->maxSets;
	dsPool.poolSizes = {pCreateInfo->pPoolSizes, pCreateInfo->pPoolSizes + pCreateInfo->poolSizeCount};
	dsPool.flags = pCreateInfo->flags;

	// init descriptor entries
	if(dsPool.flags & VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT) {
		dsPool.entries = std::make_unique<DescriptorPool::SetEntry[]>(dsPool.maxSets);
		initResetPoolEntries(dsPool);
	}

	// init descriptor sets
	dsPool.sets = std::make_unique<DescriptorPool::SetAlloc[]>(dsPool.maxSets);
	initResetPoolSets(dsPool);

	// init descriptor data
	dsPool.dataSize = 0u;
	for(auto& pool : dsPool.poolSizes) {
		dsPool.dataSize += descriptorSize(pool.type) * pool.descriptorCount;
	}

	dsPool.data = std::make_unique<std::byte[]>(dsPool.dataSize);
	debugStatAdd(DebugStats::get().descriptorPoolMem, dsPool.dataSize);
	TracyAlloc(dsPool.data.get(), dsPool.dataSize);

	*pDescriptorPool = castDispatch<VkDescriptorPool>(dsPool);
	dev.dsPools.mustEmplace(*pDescriptorPool, std::move(dsPoolPtr));

	return res;
}

VKAPI_ATTR void VKAPI_CALL DestroyDescriptorPool(
		VkDevice                                    device,
		VkDescriptorPool                            descriptorPool,
		const VkAllocationCallbacks*                pAllocator) {
	if(!descriptorPool) {
		return;
	}

	auto& dev = getDevice(device);
	auto handle = dev.dsPools.mustMove(descriptorPool)->handle;
	dev.dispatch.DestroyDescriptorPool(dev.handle, handle, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL ResetDescriptorPool(
		VkDevice                                    device,
		VkDescriptorPool                            descriptorPool,
		VkDescriptorPoolResetFlags                  flags) {
	ZoneScoped;

	auto& dsPool = get(device, descriptorPool);
	auto& dev = *dsPool.dev;

	// We know the linked list isn't modified in between
	for(auto it = dsPool.aliveSets; it; it = it->next) {
		auto& ds = it->ds();
		if(!HandleDesc<VkDescriptorSet>::wrap) {
			dev.descriptorSets.mustErase(ds.handle);
		}

		destroy(ds, false);
	}

	initResetPoolSets(dsPool);
	if(dsPool.flags & VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT) {
		initResetPoolEntries(dsPool);
	}

	dsPool.highestOffset = 0u;

	{
		ZoneScopedN("dispatch");
		return dev.dispatch.ResetDescriptorPool(dev.handle, dsPool.handle, flags);
	}
}

// descriptor set
// DeadDescriptorSetPtr kill(std::unique_ptr<DescriptorSet> dsPtr) {
// 	dsPtr->~DescriptorSet();
// 	return DeadDescriptorSetPtr(dsPtr.release());
// }
//
// std::unique_ptr<DescriptorSet> revive(DeadDescriptorSetPtr deadDS) {
// 	new (deadDS.get()) DescriptorSet();
// 	return std::unique_ptr<DescriptorSet>(deadDS.release());
// }

void initDescriptorSet(Device& dev, DescriptorPool& pool, VkDescriptorSet& handle,
		IntrusivePtr<DescriptorSetLayout> layoutPtr, u32 varCount) {
	ZoneScopedN("initDescriptorSet");
	// std::unique_ptr<DescriptorSet> dsPtr;
//
	// if(pool.setPool.empty()) {
	// 	ZoneScopedN("allocDescriptorSet");
	// 	dsPtr = std::make_unique<DescriptorSet>();
	// } else {
	// 	ZoneScopedN("reviveDescriptorSet");
	// 	dsPtr = revive(std::move(pool.setPool.back()));
	// 	pool.setPool.pop_back();
	// }
//
	// auto& ds = *dsPtr;

	DescriptorSet& ds = *new(&pool.freeSets->ds) DescriptorSet();
	pool.freeSets = pool.freeSets->nextFree;

	ds.objectType = VK_OBJECT_TYPE_DESCRIPTOR_SET;
	ds.dev = &dev;
	ds.handle = handle;
	ds.layout = std::move(layoutPtr);
	ds.variableDescriptorCount = varCount;
	ds.pool = &pool;

	// find data
	auto memSize = align(totalDescriptorMemSize(*ds.layout, varCount), sizeof(void*));
	if(pool.highestOffset + memSize <= pool.dataSize) {
		ds.data = &pool.data[pool.highestOffset];
		if(pool.flags & VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT) {
			dlg_assert(pool.freeEntries);

			auto& entry = *pool.freeEntries;
			pool.freeEntries = entry.next;
			pool.freeEntries->prev = nullptr;

			if(pool.highestEntry) {
				pool.highestEntry->next = &entry;
			} else {
				pool.usedEntries = &entry;
			}

			entry.offset = pool.highestOffset;
			entry.size = memSize;
			entry.set = &ds;
			entry.next = nullptr;
			entry.prev = pool.highestEntry;

			pool.highestEntry = &entry;
			ds.setEntry = &entry;
		}
		pool.highestOffset += memSize;
	} else {
		ZoneScopedN("findData - fragmented");

		// otherwise we can't get fragmentation at all
		dlg_assert(pool.flags & VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);

		auto offset = 0u;
		auto it = pool.lastEntry;

		// Try to use position of last allocation
		if(it) {
			auto nextOff = pool.dataSize;
			if(it->next) {
				nextOff = it->next->offset;
			}

			offset = it->offset + it->size;
			auto fits = (offset + memSize <= nextOff);
			it = (fits) ? it->next : nullptr;
		}

		// Start search at 0
		if(!it) {
			offset = 0u;
			it = pool.usedEntries;

			while(it) {
				auto& entry = *it;
				if(offset + memSize <= it->offset) {
					break;
				}
				offset = entry.offset + entry.size;
				it = it->next;
			}
		}

		if(offset + memSize > pool.dataSize) {
			// NOTE: we could just return VK_ERROR_OUT_OF_POOL_MEMORY
			// here. Some drivers do it. But if we land here, the driver
			// seems to not do it. So we just use a slow path.
			dlg_assert(!it);
			dlg_warn("Fragmentation of descriptor pool detected. Slow path");
			ds.data = new std::byte[memSize];
		} else {
			// it == null can't happen, we should have landed
			// in some earlier branch.
			dlg_assert(it);
			dlg_assert(pool.usedEntries && pool.highestEntry);
			dlg_assert(pool.freeEntries);

			auto& entry = *pool.freeEntries;
			pool.freeEntries = entry.next;
			pool.freeEntries->prev = nullptr;

			entry.offset = offset;
			entry.size = memSize;
			entry.set = &ds;

			// insert entry before 'it'
			entry.prev = it->prev;
			entry.next = it;

			dlg_assert(!it->prev == (it == pool.usedEntries));
			if(it->prev) {
				it->prev->next = &entry;
			} else {
				pool.usedEntries = &entry;
			}

			it->prev = &entry;

			pool.lastEntry = &entry;

			ds.data = &pool.data[offset];
			ds.setEntry = &entry;
		}
	}

	dlg_assert(ds.data >= pool.data.get() &&
		pool.data.get() + pool.dataSize >= ds.data);
	initDescriptorState(ds.data, *ds.layout, ds.variableDescriptorCount);

	dlg_assert(std::uintptr_t(ds.data) % sizeof(void*) == 0u);
	handle = castDispatch<VkDescriptorSet>(ds);

	// WIP(ds): temporary optimization to not insert into dev.descriptorSets
	// when wrapping anyways. This means we lose the ability to enumerate
	// descriptor sets in the gui though :(
	// But this function can be on very hot paths.
	if(!HandleDesc<VkDescriptorSet>::wrap) {
		dev.descriptorSets.mustEmplace(handle, &ds);
	} else {
		// (void) dsPtr.release();
	}

	if(ds.layout->immutableSamplers) {
		initImmutableSamplers(ds);
	}

	pool.descriptorSets.push_back(&ds);
}

VKAPI_ATTR VkResult VKAPI_CALL AllocateDescriptorSets(
		VkDevice                                    device,
		const VkDescriptorSetAllocateInfo*          pAllocateInfo,
		VkDescriptorSet*                            pDescriptorSets) {
	ZoneScoped;

	auto& pool = get(device, pAllocateInfo->descriptorPool);
	auto& dev = *pool.dev;
	auto count = pAllocateInfo->descriptorSetCount;

	auto nci = *pAllocateInfo;
	nci.descriptorPool = pool.handle;

	ThreadMemScope memScope;
	auto dsLayouts = memScope.alloc<VkDescriptorSetLayout>(count);
	for(auto i = 0u; i < count; ++i) {
		dsLayouts[i] = get(dev, pAllocateInfo->pSetLayouts[i]).handle;
	}

	nci.pSetLayouts = dsLayouts.data();

	{
		ZoneScopedN("dispatch");
		auto res = dev.dispatch.AllocateDescriptorSets(dev.handle, &nci, pDescriptorSets);
		if(res != VK_SUCCESS) {
			return res;
		}
	}

	auto* variableCountInfo = findChainInfo<VkDescriptorSetVariableDescriptorCountAllocateInfo,
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO>(*pAllocateInfo);
	if (variableCountInfo && variableCountInfo->descriptorSetCount == 0u) {
		variableCountInfo = nullptr;
	}
	dlg_assert(!variableCountInfo ||
		variableCountInfo->descriptorSetCount == pAllocateInfo->descriptorSetCount);

	for(auto i = 0u; i < count; ++i) {
		auto layoutPtr = getPtr(dev, pAllocateInfo->pSetLayouts[i]);
		auto& layout = *layoutPtr;

		// per spec variable counts are zero by default, if no other value is provided
		auto varCount = u32(0);
		if(variableCountInfo && !layout.bindings.empty() &&
				layout.bindings.back().flags & VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT) {
			varCount = variableCountInfo->pDescriptorCounts[i];
		}

		initDescriptorSet(dev, pool, pDescriptorSets[i], std::move(layoutPtr), varCount);
	}

	debugStatAdd(DebugStats::get().aliveDescriptorSets, count);

	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL FreeDescriptorSets(
		VkDevice                                    device,
		VkDescriptorPool                            descriptorPool,
		uint32_t                                    descriptorSetCount,
		const VkDescriptorSet*                      pDescriptorSets) {
	ZoneScoped;

	auto& pool = get(device, descriptorPool);
	auto& dev = *pool.dev;

	ThreadMemScope memScope;
	auto handles = memScope.alloc<VkDescriptorSet>(descriptorSetCount);

	for(auto i = 0u; i < descriptorSetCount; ++i) {
		if(!HandleDesc<VkDescriptorSet>::wrap) {
			auto ptr = dev.descriptorSets.mustMove(pDescriptorSets[i]);
			handles[i] = ptr->handle;
			// pool.setPool.push_back(kill(std::move(ptr)));
			returnToPool(*ptr);
		} else {
			auto& ds = get(dev, pDescriptorSets[i]);
			handles[i] = ds.handle;
			// pool.setPool.push_back(kill(std::unique_ptr<DescriptorSet>(&ds)));
			returnToPool(ds);
		}
	}

	{
		ZoneScopedN("dispatch");
		return dev.dispatch.FreeDescriptorSets(dev.handle, pool.handle,
			u32(handles.size()), handles.data());
	}
}

void update(DescriptorSet& state, unsigned bind, unsigned elem,
		VkBufferView& handle) {
	dlg_assert(handle);

	auto& binding = bufferViews(state, bind)[elem];
	auto ptr = getPtr(*state.layout->dev, handle);
	handle = ptr->handle;

	std::lock_guard lock(state.mutex);
	binding = std::move(ptr);
}

void update(DescriptorSet& state, unsigned bind, unsigned elem,
		VkDescriptorImageInfo& img) {
	auto& dev = *state.layout->dev;

	auto& binding = images(state, bind)[elem];
	binding.layout = img.imageLayout;

	auto& layout = state.layout->bindings[bind];
	if(needsImageView(layout.descriptorType)) {
		dlg_assert(img.imageView);
		auto ptr = getPtr(dev, img.imageView);
		img.imageView = ptr->handle;

		std::lock_guard lock(state.mutex);
		binding.imageView = std::move(ptr);
	}

	if(needsSampler(layout.descriptorType)) {
		if(layout.immutableSamplers) {
			// immutable samplers are initialized at the beginning and
			// never unset.
			dlg_assert(binding.sampler);
			dlg_assert(binding.sampler.get() == layout.immutableSamplers[elem].get());
		} else {
			dlg_assert(img.sampler);
			auto ptr = getPtr(dev, img.sampler);
			img.sampler = ptr->handle;

			std::lock_guard lock(state.mutex);
			binding.sampler = std::move(ptr);
		}
	}
}

void update(DescriptorSet& state, unsigned bind, unsigned elem,
		VkDescriptorBufferInfo& buf) {
	auto& binding = buffers(state, bind)[elem];
	auto ptr = getPtr(*state.layout->dev, buf.buffer);
	buf.buffer = ptr->handle;

	std::lock_guard lock(state.mutex);
	binding.buffer = std::move(ptr);
	binding.offset = buf.offset;
	binding.range = evalRange(binding.buffer->ci.size, buf.offset, buf.range);
}

void update(DescriptorSet& state, unsigned bind, unsigned elem,
		VkAccelerationStructureKHR& handle) {
	dlg_assert(handle);

	auto& binding = accelStructs(state, bind)[elem];
	auto ptr = getPtr(*state.layout->dev, handle);
	handle = ptr->handle;

	std::lock_guard lock(state.mutex);
	binding = std::move(ptr);
}

void update(DescriptorSet& state, unsigned bind, unsigned offset,
		std::byte src) {
	auto buf = inlineUniformBlock(state, bind);
	dlg_assert(offset < buf.size());

	std::lock_guard lock(state.mutex);
	// NOTE: updating uniform inline blocks byte-by-byte is inefficient
	// but reworking this to be more efficient would be complicated.
	// Especially so since we still have to consider that additional bytes
	// will update the next descriptor.
	buf[offset] = src;
}

void advanceUntilValid(DescriptorSet& state, unsigned& binding, unsigned& elem) {
	dlg_assert(binding < state.layout->bindings.size());
	auto count = descriptorCount(state, binding);
	while(elem >= count) {
		++binding;
		elem = 0u;
		dlg_assert(binding < state.layout->bindings.size());
		count = descriptorCount(state, binding);
	}
}

// NOTE: is UpdateDescriptorSets(WithTemplate), we don't invalidate
// command records even more, even though it would be needed in most
// cases (excluding update_after_bind stuff) but we don't need that
// information and can save some work this way.
// DescriptorSets

VKAPI_ATTR void VKAPI_CALL UpdateDescriptorSets(
		VkDevice                                    device,
		uint32_t                                    descriptorWriteCount,
		const VkWriteDescriptorSet*                 pDescriptorWrites,
		uint32_t                                    descriptorCopyCount,
		const VkCopyDescriptorSet*                  pDescriptorCopies) {
	ZoneScoped;
	auto& dev = getDevice(device);

	// handle writes
	auto totalWriteCount = 0u;
	for(auto i = 0u; i < descriptorWriteCount; ++i) {
		totalWriteCount += pDescriptorWrites[i].descriptorCount;
	}

	ThreadMemScope memScope;

	auto writes = memScope.alloc<VkWriteDescriptorSet>(descriptorWriteCount);
	auto imageInfos = memScope.alloc<VkDescriptorImageInfo>(totalWriteCount);
	auto bufferInfos = memScope.alloc<VkDescriptorBufferInfo>(totalWriteCount);
	auto bufferViews = memScope.alloc<VkBufferView>(totalWriteCount);
	auto accelStructs = memScope.alloc<VkAccelerationStructureKHR>(totalWriteCount);

	auto writeOff = 0u;
	for(auto i = 0u; i < descriptorWriteCount; ++i) {
		auto& write = pDescriptorWrites[i];
		dlg_assert(write.descriptorCount > 0u); // per vulkan spec

		auto& ds = get(dev, write.dstSet);
		dlg_assert(ds.handle);
		dlg_assert(ds.layout);

		writes[i] = write;
		writes[i].dstSet = ds.handle;

		auto dstBinding = write.dstBinding;
		auto dstElem = write.dstArrayElement;

		auto* chainCopy = copyChainLocal(memScope, write.pNext);
		auto* accelStructWrite = (VkWriteDescriptorSetAccelerationStructureKHR*) findChainInfo2<
			VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR>(chainCopy);
		auto* inlineUniformWrite = (VkWriteDescriptorSetInlineUniformBlockEXT*) findChainInfo2<
			VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_INLINE_UNIFORM_BLOCK_EXT>(chainCopy);

		// NOTE: technically, a cow could be set immediately *after*
		// we call this here, making us change the state even though there
		// is an active cow. We only ever add cows during submission so
		// that would mean that the application updates a descriptor set
		// that is bound in a cb that is currently being submitted.
		// That's only allowed with VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT,
		// we don't support it.
		//
		// The proper fix for this: hard-require all handles used in
		// descriptorSetUpdating to be wrapped (so we don't have to access
		// the device mutex in between) and then just hold the lock returned
		// by checkResolveCow while updating the ds
		/*auto lock = */checkResolveCow(ds);

		for(auto j = 0u; j < write.descriptorCount; ++j, ++dstElem) {
			advanceUntilValid(ds, dstBinding, dstElem);
			dlg_assert(dstBinding < ds.layout->bindings.size());
			auto& layout = ds.layout->bindings[dstBinding];
			dlg_assert(write.descriptorType == layout.descriptorType);

			switch(category(write.descriptorType)) {
				case DescriptorCategory::image: {
					dlg_assert(write.pImageInfo);
					auto& info = imageInfos[writeOff + j];
					info = write.pImageInfo[j];
					update(ds, dstBinding, dstElem, info);
					break;
				} case DescriptorCategory::buffer: {
					dlg_assert(write.pBufferInfo);
					auto& info = bufferInfos[writeOff + j];
					info = write.pBufferInfo[j];
					update(ds, dstBinding, dstElem, info);
					break;
				} case DescriptorCategory::bufferView: {
					dlg_assert(write.pTexelBufferView);
					auto& info = bufferViews[writeOff + j];
					info = write.pTexelBufferView[j];
					update(ds, dstBinding, dstElem, info);
					break;
				} case DescriptorCategory::accelStruct: {
					dlg_assert(accelStructWrite);
					dlg_assert(j < accelStructWrite->accelerationStructureCount);
					auto& info = accelStructs[writeOff + j];
					info = accelStructWrite->pAccelerationStructures[j];
					update(ds, dstBinding, dstElem, info);
					break;
				} case DescriptorCategory::inlineUniformBlock: {
					dlg_assert(inlineUniformWrite);
					dlg_assert(j < inlineUniformWrite->dataSize);
					auto ptr = reinterpret_cast<const std::byte*>(inlineUniformWrite->pData);
					update(ds, dstBinding, dstElem, ptr[j]);
					break;
				} case DescriptorCategory::none:
					dlg_error("unreachable: Invalid descriptor type");
					break;
			}
		}

		writes[i].pImageInfo = imageInfos.data() + writeOff;
		writes[i].pBufferInfo = bufferInfos.data() + writeOff;
		writes[i].pTexelBufferView = bufferViews.data() + writeOff;

		if(accelStructWrite) {
			dlg_assert(category(write.descriptorType) == DescriptorCategory::accelStruct);
			accelStructWrite->pAccelerationStructures = accelStructs.data() + writeOff;
			writes[i].pNext = chainCopy;
		}

		writeOff += writes[i].descriptorCount;
	}

	// handle copies
	auto copies = memScope.alloc<VkCopyDescriptorSet>(descriptorCopyCount);

	for(auto i = 0u; i < descriptorCopyCount; ++i) {
		auto& copyInfo = pDescriptorCopies[i];
		auto& src = get(dev, copyInfo.srcSet);
		auto& dst = get(dev, copyInfo.dstSet);

		copies[i] = copyInfo;
		copies[i].srcSet = src.handle;
		copies[i].dstSet = dst.handle;

		auto dstBinding = copyInfo.dstBinding;
		auto dstElem = copyInfo.dstArrayElement;
		auto srcBinding = copyInfo.srcBinding;
		auto srcElem = copyInfo.srcArrayElement;

		auto lock = checkResolveCow(dst);

		for(auto j = 0u; j < copyInfo.descriptorCount; ++j, ++srcElem, ++dstElem) {
			advanceUntilValid(dst, dstBinding, dstElem);
			advanceUntilValid(src, srcBinding, srcElem);
			copy(dst, dstBinding, dstElem, src, srcBinding, srcElem);
		}
	}

	{
		ZoneScopedN("dispatch");
		return dev.dispatch.UpdateDescriptorSets(dev.handle,
			u32(writes.size()), writes.data(),
			u32(copies.size()), copies.data());
	}
}

VKAPI_ATTR VkResult VKAPI_CALL CreateDescriptorUpdateTemplate(
		VkDevice                                    device,
		const VkDescriptorUpdateTemplateCreateInfo* pCreateInfo,
		const VkAllocationCallbacks*                pAllocator,
		VkDescriptorUpdateTemplate*                 pDescriptorUpdateTemplate) {
	// NOTE: we don't use host allocators here since this handle is potentially
	// kept alive inside the layer, preventing us from passing an application
	// allocator to the destruction function.
	// See design.md on allocators.
	(void) pAllocator;

	auto& dsLayout = get(device, pCreateInfo->descriptorSetLayout);
	auto& dev = *dsLayout.dev;
	auto& pipeLayout = get(dev, pCreateInfo->pipelineLayout);

	auto nci = *pCreateInfo;
	nci.descriptorSetLayout = dsLayout.handle;
	nci.pipelineLayout = pipeLayout.handle;

	auto res = dev.dispatch.CreateDescriptorUpdateTemplate(dev.handle, &nci,
		nullptr, pDescriptorUpdateTemplate);
	if(res != VK_SUCCESS) {
		return res;
	}

	auto dutPtr = IntrusivePtr<DescriptorUpdateTemplate>(new DescriptorUpdateTemplate());
	auto& dut = *dutPtr;
	dut.objectType = VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE;
	dut.dev = &dev;
	dut.handle = *pDescriptorUpdateTemplate;

	dut.entries = {
		pCreateInfo->pDescriptorUpdateEntries,
		pCreateInfo->pDescriptorUpdateEntries + pCreateInfo->descriptorUpdateEntryCount
	};

	*pDescriptorUpdateTemplate = castDispatch<VkDescriptorUpdateTemplate>(dut);
	dev.dsuTemplates.mustEmplace(*pDescriptorUpdateTemplate, std::move(dutPtr));

	return res;
}

VKAPI_ATTR void VKAPI_CALL DestroyDescriptorUpdateTemplate(
		VkDevice                                    device,
		VkDescriptorUpdateTemplate                  descriptorUpdateTemplate,
		const VkAllocationCallbacks*                pAllocator) {
	if(!descriptorUpdateTemplate) {
		return;
	}

	auto& dev = getDevice(device);
	dev.dsuTemplates.mustErase(descriptorUpdateTemplate);

	// Don't destroy it here, handle has shared ownership, see e.g.
	// the dsuTemplates hash map in Device for justification
	(void) pAllocator;
}

VKAPI_ATTR void VKAPI_CALL UpdateDescriptorSetWithTemplate(
		VkDevice                                    device,
		VkDescriptorSet                             descriptorSet,
		VkDescriptorUpdateTemplate                  descriptorUpdateTemplate,
		const void*                                 pData) {
	ZoneScoped;

	auto& ds  = get(device, descriptorSet);
	auto& dev = *ds.dev;
	auto& dut = get(dev, descriptorUpdateTemplate);

	// NOTE: technically, a cow could be set immediately *after*
	// we call this here, making us change the state even though there
	// is an active cow. We only ever add cows during submission so
	// that would mean that the application updates a descriptor set
	// that is bound in a cb that is currently being submitted.
	// That's only allowed with VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT,
	// we don't support it.
	//
	// The proper fix for this: hard-require all handles used in
	// descriptorSetUpdating to be wrapped (so we don't have to access
	// the device mutex in between) and then just hold the lock returned
	// by checkResolveCow while updating the ds
	/*auto lock =*/checkResolveCow(ds);

	auto totalSize = totalUpdateDataSize(dut);

	ThreadMemScope memScope;
	auto fwdData = memScope.alloc<std::byte>(totalSize);
	std::memcpy(fwdData.data(), pData, totalSize);
	auto* ptr = fwdData.data();

	for(auto& entry : dut.entries) {
		auto dstBinding = entry.dstBinding;
		auto dstElem = entry.dstArrayElement;
		for(auto j = 0u; j < entry.descriptorCount; ++j, ++dstElem) {
			advanceUntilValid(ds, dstBinding, dstElem);
			auto dsType = ds.layout->bindings[dstBinding].descriptorType;

			// TODO: such an assertion here would be nice. Track used
			// layout in update?
			// dlg_assert(write.descriptorType == type);

			auto* data = ptr + (entry.offset + j * entry.stride);

			// TODO: the reinterpret_cast here is UB in C++ I guess.
			// Assuming the caller did it correctly (really creating
			// the objects e.g. via placement new) we could probably also
			// do it correctly by using placement new (copy) into 'fwdData'
			// instead of the memcpy above.
			switch(category(dsType)) {
				case DescriptorCategory::image: {
					auto& img = *reinterpret_cast<VkDescriptorImageInfo*>(data);
					update(ds, dstBinding, dstElem, img);
					break;
				} case DescriptorCategory::buffer: {
					auto& buf = *reinterpret_cast<VkDescriptorBufferInfo*>(data);
					update(ds, dstBinding, dstElem, buf);
					break;
				} case DescriptorCategory::bufferView: {
					auto& bufView = *reinterpret_cast<VkBufferView*>(data);
					update(ds, dstBinding, dstElem, bufView);
					break;
				} case DescriptorCategory::accelStruct: {
					auto& accelStruct = *reinterpret_cast<VkAccelerationStructureKHR*>(data);
					update(ds, dstBinding, dstElem, accelStruct);
					break;
				} case DescriptorCategory::inlineUniformBlock: {
					auto ptr = reinterpret_cast<const std::byte*>(data);
					update(ds, dstBinding, dstElem, *ptr);
					break;
				} case DescriptorCategory::none:
					dlg_error("Invalid/unknown descriptor type");
					break;
			}
		}
	}

	{
		ZoneScopedN("dispatchUpdateDescriptorSetWithTemplate");
		dev.dispatch.UpdateDescriptorSetWithTemplate(dev.handle, ds.handle,
			dut.handle, static_cast<const void*>(fwdData.data()));
	}
}

u32 totalUpdateDataSize(const DescriptorUpdateTemplate& dut) {
	u32 ret = 0u;
	for(auto& entry : dut.entries) {
		auto stride = entry.stride;
		auto size = 0u;
		switch(category(entry.descriptorType)) {
			case DescriptorCategory::image:
				size = sizeof(VkDescriptorImageInfo);
				break;
			case DescriptorCategory::buffer:
				size = sizeof(VkDescriptorBufferInfo);
				break;
			case DescriptorCategory::bufferView:
				size = sizeof(VkBufferView);
				break;
			case DescriptorCategory::accelStruct:
				size = sizeof(VkAccelerationStructureKHR);
				break;
			case DescriptorCategory::inlineUniformBlock:
				// this is a special case defined in VK_EXT_inline_uniform_block.
				// entry.stride should be ignored and 1 used.
				stride = 1u;
				size = 1u;
				break;
			case DescriptorCategory::none:
				dlg_error("unreachable: Invalid/unknown descriptor type");
				break;
		}

		auto off = u32(entry.offset + entry.descriptorCount * stride);
		off += size;

		ret = std::max<u32>(ret, off);
	}

	return ret;
}

DescriptorSetCow::~DescriptorSetCow() {
	if(ds) {
		std::lock_guard lock(ds->mutex);

		// Unregister. We succesfully saved a copy *yeay*.
		ds->cow = nullptr;
	}
}

std::pair<DescriptorStateRef, std::unique_lock<DebugMutex>> access(DescriptorSetCow& cow) {
	std::unique_lock cowLock(cow.mutex);
	if(cow.copy) {
		return {DescriptorStateRef(*cow.copy), std::move(cowLock)};
	}

	assert(cow.ds);
	assert(cow.ds->cow == &cow);

	// NOTE how we don't have to lock cow.obj->mutex to access the object
	// state itself here. We know that while
	// cow.source, and therefore cow.source->cow, are set, cow.source->state
	// is immutable. All functions that change it must first call checkResolveCow.
	return {DescriptorStateRef(*cow.ds), std::move(cowLock)};
}

IntrusivePtr<DescriptorSetCow> addCow(DescriptorSet& set) {
	std::lock_guard lock(set.mutex);
	if(!set.cow) {
	// TODO: get from a pool or something
		set.cow = new DescriptorSetCow();
		set.cow->ds = &set;
	}

	// increase reference count via new intrusive ptr
	return IntrusivePtr<DescriptorSetCow>(set.cow);
}

} // namespace vil
