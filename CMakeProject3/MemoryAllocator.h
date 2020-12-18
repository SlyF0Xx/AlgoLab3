#include "CoalesedAllocator.h"
#include "FixedSizeAllocator.h"

class MemoryAllocator
{
public:
	MemoryAllocator() = default;
	virtual ~MemoryAllocator() = default;

	virtual void init();
	virtual void destroy();
	virtual void* alloc(size_t size);
	virtual void free(void* p);

#ifdef _DEBUG
	virtual void dumpStat() const;
	virtual void dumpBlocks() const;
#endif

private:
	FixedSizeAllocator<16> m_fixed_size16;
	FixedSizeAllocator<32> m_fixed_size32;
	FixedSizeAllocator<64> m_fixed_size64;
	FixedSizeAllocator<128> m_fixed_size128;
	FixedSizeAllocator<256> m_fixed_size256;
	FixedSizeAllocator<512> m_fixed_size512;
	CoalesedAllocator m_coalesed;
};