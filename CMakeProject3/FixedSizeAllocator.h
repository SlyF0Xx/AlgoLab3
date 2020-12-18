#pragma once

#include <windows.h>
#include <cassert>
#include <cstddef>

constexpr size_t PageSize = 4096;

template<int AllocSize>
class FixedSizeAllocator
{
private:
#pragma pack(push, 8)
	struct Page {
		Page* next_page = nullptr;
		int free_list_begin_index = -1;
		int initialized_buckets = 0;
	};
#pragma pack(pop)

#pragma pack(push, 8)
	struct Bucket {
#ifdef _DEBUG
		Bucket(int next_index, size_t size)
			: next_index(next_index), size(size)
		{}
#else
		Bucket(int next_index)
			: next_index(next_index)
		{}
#endif

#ifdef _DEBUG
		long long magic_number = 0xDEADBEEF;
		size_t size;
#endif

		int next_index;

		int reserved_byte; // for detecting allocator
	};
#pragma pack(pop)

public:
	FixedSizeAllocator() = default;
	~FixedSizeAllocator()
	{
#ifdef _DEBUG
		assert(initialized);
		assert(deinitialized);
#endif
	}

	void init()
	{
		LPVOID new_page_ptr = VirtualAlloc(NULL, PageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		Page* new_page = new (new_page_ptr) Page();
		first_page = new_page;

#ifdef _DEBUG
		assert(!initialized);
		initialized = true;
#endif
	}

	void destroy()
	{
#ifdef _DEBUG
		assert(initialized);
		assert(!deinitialized);
		deinitialized = true;

		Page* page_it = first_page;
		while (page_it) {
			int index = page_it->free_list_begin_index;
			int freed_blocks = 0;
			while (index != -1) {
				std::byte* bucket = reinterpret_cast<std::byte*>(page_it) + sizeof(Page) + (BucketSize * index);
				Bucket* old_bucket = reinterpret_cast<Bucket*>(bucket);
				++freed_blocks;
				index = old_bucket->next_index;
			}
			assert(page_it->initialized_buckets == freed_blocks);

			page_it = page_it->next_page;
		}
#endif

		destroy_i(first_page);
	}

	inline static auto BucketSize = AllocSize + sizeof(Bucket);
	inline static auto BucketsInPage = (PageSize - sizeof(Page)) / BucketSize;

	void* alloc(size_t size)
	{
#ifdef _DEBUG
		assert(initialized);
		assert(!deinitialized);
#endif
		Page* page_it = first_page;
		Page* prev_page_it = nullptr;
		while (page_it) {
			if (page_it->initialized_buckets < BucketsInPage) {
				return allocate_uninitialized_bucket(page_it, size);
			}
			else if (page_it->free_list_begin_index != -1) {
				std::byte* bucket_ptr = reinterpret_cast<std::byte*>(page_it) + sizeof(Page) + (BucketSize * page_it->free_list_begin_index);
				Bucket* bucket = reinterpret_cast<Bucket*>(bucket_ptr);
				int cpy = bucket->next_index;
				bucket->next_index = page_it->free_list_begin_index; // allocated block, let's write own index here
				page_it->free_list_begin_index = cpy;

#ifdef _DEBUG
				bucket->size = size;
#endif

				return bucket + sizeof(Bucket);
			}
			else {
				prev_page_it = page_it;
				page_it = page_it->next_page;
			}
		}
		// no free space, let's allocate new page
		LPVOID new_page_ptr = VirtualAlloc(NULL, PageSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		Page* new_page = new (new_page_ptr) Page();

		if (prev_page_it) {
			prev_page_it->next_page = new_page;
		}

		return allocate_uninitialized_bucket(new_page, size);
	}

	void free(void* p)
	{
#ifdef _DEBUG
		assert(initialized);
		assert(!deinitialized);
#endif
		std::byte* bucket = reinterpret_cast<std::byte*>(p) - sizeof(Bucket);
		Bucket* old_bucket = reinterpret_cast<Bucket*>(bucket);

#ifdef _DEBUG
		assert(old_bucket->magic_number == 0xDEADBEEF);
#endif

		int own_index = old_bucket->next_index; // we write own index in free-list cell on allocation

		std::byte* page_ptr = bucket - (BucketSize * own_index) - sizeof(Page);
		Page* page = reinterpret_cast<Page*>(page_ptr);

		old_bucket->next_index = page->free_list_begin_index;
		page->free_list_begin_index = own_index;
	}

#ifdef _DEBUG
	int get_allocated_blocks() const
	{
		int pages_size = 0;
		int total_free_blocks = 0;
		int total_uninitialized_blocks = 0;

		Page* page_it = first_page;
		while (page_it) {
			int index = page_it->free_list_begin_index;
			int freed_blocks = 0;

			while (index != -1) {
				std::byte* bucket = reinterpret_cast<std::byte*>(page_it) + sizeof(Page) + (BucketSize * index);
				Bucket* old_bucket = reinterpret_cast<Bucket*>(bucket);
				++freed_blocks;
				index = old_bucket->next_index;
			}

			total_free_blocks += freed_blocks;
			total_uninitialized_blocks += BucketsInPage - page_it->initialized_buckets;
			page_it = page_it->next_page;
			++pages_size;
		}

		return (BucketsInPage * pages_size) - total_free_blocks - total_uninitialized_blocks;
	}

	int get_freed_blocks() const
	{
		int total_free_blocks = 0;

		Page* page_it = first_page;
		while (page_it) {
			int index = page_it->free_list_begin_index;
			int freed_blocks = 0;
			while (index != -1) {
				std::byte* bucket = reinterpret_cast<std::byte*>(page_it) + sizeof(Page) + (BucketSize * index);
				Bucket* old_bucket = reinterpret_cast<Bucket*>(bucket);
				++freed_blocks;
				index = old_bucket->next_index;
			}

			total_free_blocks += freed_blocks;
			page_it = page_it->next_page;
		}

		return total_free_blocks;
	}

	int get_uninitialized_blocks() const
	{
		int total_uninitialized_blocks = 0;

		Page* page_it = first_page;
		while (page_it) {
			total_uninitialized_blocks += BucketsInPage - page_it->initialized_buckets;
			page_it = page_it->next_page;
		}

		return total_uninitialized_blocks;
	}

	int get_pages_count() const
	{
		int pages_size = 0;

		Page* page_it = first_page;
		while (page_it) {
			page_it = page_it->next_page;
			++pages_size;
		}

		return pages_size;
	}

	void dumpStat() const
	{
		assert(initialized);
		assert(!deinitialized);

		int pages_size = 0;
		int total_blocks = 0;
		int total_free_blocks = 0;
		int total_uninitialized_blocks = 0;

		std::cout << "Blocks: " << AllocSize << std::endl;
		Page* page_it = first_page;
		while (page_it) {
			//std::cout << "Page, size " << PageSize << ", block statistics:" << std::endl;
			// std::cout << "Total blocks: " << BucketsInPage << std::endl;
			// std::cout << "Uninitialized blocks: " << BucketsInPage - page_it->initialized_buckets << std::endl;

			int index = page_it->free_list_begin_index;
			int freed_blocks = 0;
			while (index != -1) {
				std::byte* bucket = reinterpret_cast<std::byte*>(page_it) + sizeof(Page) + (BucketSize * index);
				Bucket* old_bucket = reinterpret_cast<Bucket*>(bucket);
				++freed_blocks;
				index = old_bucket->next_index;
			}

			// std::cout << "Freed blocks: " << freed_blocks << std::endl;
			// std::cout << "Allocated blocks: " << BucketsInPage - freed_blocks - (BucketsInPage - page_it->initialized_buckets) << std::endl << std::endl;

			total_free_blocks += freed_blocks;
			total_uninitialized_blocks += BucketsInPage - page_it->initialized_buckets;
			page_it = page_it->next_page;
			++pages_size;
		}

		std::cout << std::endl << "Total block statistics:" << std::endl;
		// std::cout << "Total pages: " << pages_size << std::endl;
		std::cout << "Total blocks: " << BucketsInPage * pages_size << std::endl;
		std::cout << "Uninitialized blocks: " << total_uninitialized_blocks << std::endl;
		std::cout << "Freed blocks: " << total_free_blocks << std::endl;
		std::cout << "Allocated blocks: " << (BucketsInPage * pages_size) - total_free_blocks - total_uninitialized_blocks << std::endl << std::endl;

		// TODO: ¬ыводит в стандартный поток вывода статистику по аллокатору: количество зан€тых и свободных блоков, список блоков запрошенных у ќ— и их размеры
	}

	void dumpBlocks() const
	{
		assert(initialized);
		assert(!deinitialized);

		Page* page_it = first_page;
		while (page_it) {
			for (int i = 0; i < page_it->initialized_buckets; ++i) {
				std::byte* bucket = reinterpret_cast<std::byte*>(page_it) + sizeof(Page) + (BucketSize * i);
				Bucket* old_bucket = reinterpret_cast<Bucket*>(bucket);

				if (old_bucket->next_index == i) { // it's allocated bucket
					std::cout << "size - " << old_bucket->size << std::endl;
					std::cout << "ptr - " << bucket + sizeof(Bucket) << std::endl;
				}
			}
			page_it = page_it->next_page;
		}
	}
#endif

	Page* get_first_page()
	{
		return first_page;
	}

private:

	void* allocate_uninitialized_bucket(Page* page_it, size_t size)
	{
		std::byte* bucket_ptr = reinterpret_cast<std::byte*>(page_it) + sizeof(Page) + (BucketSize * page_it->initialized_buckets);

#ifdef _DEBUG
		// allocated block, let's write own index here
		Bucket* bucket = new(bucket_ptr)Bucket(page_it->initialized_buckets, size);
#else
		Bucket* bucket = new(bucket_ptr)Bucket(page_it->initialized_buckets);
#endif
		++page_it->initialized_buckets;

		return bucket_ptr + sizeof(Bucket);
	}

	void destroy_i(Page* page_it)
	{
		if (!page_it) {
			return;
		}
		destroy_i(page_it->next_page);
		VirtualFree(page_it, 0, MEM_RELEASE);
	}

	Page* first_page = nullptr;

#ifdef _DEBUG
	bool initialized = false;
	bool deinitialized = false;
#endif
};