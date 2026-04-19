// Copyright 2024-2026 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// ReSharper disable CppClangTidyCppcoreguidelinesOwningMemory
// ReSharper disable CppClangTidyCppcoreguidelinesNoMalloc
#include "GeoToolbox/Profiling.hpp"

using namespace std;

#if TRACK_ALLOCATED_MEMORY

#if 01

void* TrackedMalloc(size_t size)
{
	auto const result = static_cast<size_t*>(malloc(size + sizeof(size_t)));
	if (result == nullptr)
	{
		return result;
	}

	GeoToolbox::TotalAllocatedSize += size;
	result[0] = size;
	return result + 1;
}

void TrackedFree(void* block)
{
	auto const original = static_cast<size_t*>(block) - 1;
	GeoToolbox::TotalAllocatedSize -= original[0];
	free(original);
}

// This simple approach misses the deallocation of array allocations, and aligned allocations altogether, but it's enough for the CompareSpatialIndices test, the code never uses those (in VC++)

#pragma warning( disable : 6387 )
#pragma warning( disable : 28196 )
#pragma warning( disable : 28251 )

void* operator new(size_t size)
{
	GeoToolbox::TotalAllocatedSize += size;
	return malloc(size);
}

void operator delete(void* block, size_t size) noexcept
{
	free(block);
	GeoToolbox::TotalAllocatedSize -= size;
}

#else

// The right approach is to keep all allocations in a hash table that uses its own malloc-based allocator

//std::unordered_map<void const*, size_t, std::hash<void const*>, std::equal_to<void const*>, MallocAllocator<std::pair<void const*, size_t>>> AllocatedSizes;

#endif

#else

void* TrackedMalloc(size_t size)
{
	return malloc(size);
}

void TrackedFree(void* block)
{
	free(block);
}

#endif
