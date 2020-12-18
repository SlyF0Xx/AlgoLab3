#include "MemoryAllocator.h"

void MemoryAllocator::init()
{
	m_fixed_size16.init();
	m_fixed_size32.init();
	m_fixed_size64.init();
	m_fixed_size128.init();
	m_fixed_size256.init();
	m_fixed_size512.init();
	m_coalesed.init();
}

void MemoryAllocator::destroy()
{
	m_fixed_size16.destroy();
	m_fixed_size32.destroy();
	m_fixed_size64.destroy();
	m_fixed_size128.destroy();
	m_fixed_size256.destroy();
	m_fixed_size512.destroy();
	m_coalesed.destroy();
}

#pragma pack(push, 8)
struct Bucket
{
	int allocator_type; // for detecting allocator
};
#pragma pack(pop)

void* MemoryAllocator::alloc(size_t size)
{
	if (size <= 16) {
		void * ptr = m_fixed_size16.alloc(size);
		*reinterpret_cast<int*>(reinterpret_cast<std::byte*>(ptr) - sizeof(int)) = 1;
		return ptr;
	}
	else if (size <= 32) {
		void* ptr = m_fixed_size32.alloc(size);
		*reinterpret_cast<int*>(reinterpret_cast<std::byte*>(ptr) - sizeof(int)) = 2;
		return ptr;
	}
	else if (size <= 64) {
		void* ptr = m_fixed_size64.alloc(size);
		*reinterpret_cast<int*>(reinterpret_cast<std::byte*>(ptr) - sizeof(int)) = 3;
		return ptr;
	}
	else if (size <= 128) {
		void* ptr = m_fixed_size128.alloc(size);
		*reinterpret_cast<int*>(reinterpret_cast<std::byte*>(ptr) - sizeof(int)) = 4;
		return ptr;
	}
	else if (size <= 256) {
		void* ptr = m_fixed_size256.alloc(size);
		*reinterpret_cast<int*>(reinterpret_cast<std::byte*>(ptr) - sizeof(int)) = 5;
		return ptr;
	}
	else if (size <= 512) {
		void* ptr = m_fixed_size512.alloc(size);
		*reinterpret_cast<int*>(reinterpret_cast<std::byte*>(ptr) - sizeof(int)) = 6;
		return ptr;
	}
	else if (size <= 1024*1024*10) {
		void* ptr = m_coalesed.alloc(size);
		*reinterpret_cast<int*>(reinterpret_cast<std::byte*>(ptr) - sizeof(int)) = 7;
		return ptr;
	}
	LPVOID ptr = VirtualAlloc(NULL, size + sizeof(Bucket), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	*reinterpret_cast<int*>(reinterpret_cast<std::byte*>(ptr) + sizeof(Bucket) - sizeof(int)) = 8;
	return reinterpret_cast<std::byte*>(ptr) + sizeof(Bucket);
}

void MemoryAllocator::free(void* p)
{
	int allocator_type = *reinterpret_cast<int*>(reinterpret_cast<std::byte*>(p) - sizeof(int));
	switch (allocator_type)
	{
	case 1: {
		m_fixed_size16.free(p);
		break;
	}
	case 2: {
		m_fixed_size32.free(p);
		break;
	}
	case 3: {
		m_fixed_size64.free(p);
		break;
	}
	case 4: {
		m_fixed_size128.free(p);
		break;
	}
	case 5: {
		m_fixed_size256.free(p);
		break;
	}
	case 6: {
		m_fixed_size512.free(p);
		break;
	}
	case 7: {
		m_coalesed.free(p);
		break;
	}
	case 8: {
		VirtualFree(reinterpret_cast<std::byte*>(p) - sizeof(Bucket), 0, MEM_RELEASE);
		break;
	}
	default:
		break;
	}
}

#ifdef _DEBUG

void MemoryAllocator::dumpStat() const
{
	std::cout << std::endl << "Total block statistics:" << std::endl;
	std::cout << "Pages: " << std::endl;
	std::cout << "size " << CoalesedPageSize << ": " << m_coalesed.get_pages_count() << std::endl;
	std::cout << "size " << PageSize << ": ";
	int pages_count = 0;
	pages_count += m_fixed_size16.get_pages_count();
	pages_count += m_fixed_size32.get_pages_count();
	pages_count += m_fixed_size64.get_pages_count();
	pages_count += m_fixed_size128.get_pages_count();
	pages_count += m_fixed_size256.get_pages_count();
	pages_count += m_fixed_size512.get_pages_count();
	std::cout << pages_count << std::endl;

	m_fixed_size16.dumpStat();
	m_fixed_size32.dumpStat();
	m_fixed_size64.dumpStat();
	m_fixed_size128.dumpStat();
	m_fixed_size256.dumpStat();
	m_fixed_size512.dumpStat();
	m_coalesed.dumpStat();
}

void MemoryAllocator::dumpBlocks() const
{
	m_fixed_size16.dumpBlocks();
	m_fixed_size32.dumpBlocks();
	m_fixed_size64.dumpBlocks();
	m_fixed_size128.dumpBlocks();
	m_fixed_size256.dumpBlocks();
	m_fixed_size512.dumpBlocks();
	m_coalesed.dumpBlocks();
}

#endif