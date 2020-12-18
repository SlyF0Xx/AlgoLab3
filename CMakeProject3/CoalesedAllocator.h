#pragma once

#include <windows.h>
#include <cassert>
#include <cstddef>
#include <iostream>

constexpr size_t CoalesedPageSize = 1024*1024*11;

class CoalesedAllocator
{
	class Page;

#pragma pack(push, 8)
	struct Bucket
	{
		Bucket(Bucket* prev_bucket, Bucket* prev_free_bucket, Page* page, size_t size)
			: prev_bucket(prev_bucket), prev_free_bucket(prev_free_bucket), page(page), size(size)
		{}

#ifdef _DEBUG
		long long red_zone = 0xDEADBEEF;
#endif
		Bucket* next_free_bucket = nullptr;
		Bucket* prev_free_bucket;
		Bucket* next_bucket = nullptr;
		Bucket* prev_bucket;
		Page* page;
		size_t size;
		bool freed = true;


		int reserved_byte; // for detecting allocator
	};
#pragma pack(pop)

#pragma pack(push, 8)
	struct Page {
		Page()
		{
			free_list_begin = reinterpret_cast<Bucket *>(reinterpret_cast<std::byte*>(this) + sizeof(Page));
			Bucket* new_bucket = new (free_list_begin)Bucket(nullptr, nullptr, this, CoalesedPageSize - sizeof(Page) - sizeof(Bucket));
		}

		Page* next_page = nullptr;
		Bucket* free_list_begin;
	};
#pragma pack(pop)

public:
	CoalesedAllocator() = default;
	~CoalesedAllocator()
	{
#ifdef _DEBUG
		assert(initialized);
		assert(deinitialized);
#endif
	}

	void init()
	{
#ifdef _DEBUG
		assert(!initialized);
		initialized = true;
#endif

		LPVOID new_page_ptr = VirtualAlloc(NULL, CoalesedPageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		Page* new_page = new (new_page_ptr) Page();
		first_page = new_page;
	}

	void destroy()
	{
#ifdef _DEBUG
		assert(initialized);
		assert(!deinitialized);
		deinitialized = true;

		Page* page_it = first_page;
		while (page_it) {
			Bucket* it = reinterpret_cast<Bucket*>(reinterpret_cast<std::byte*>(page_it) + sizeof(Page));

			assert(it->freed);
			assert(!it->next_bucket);
			page_it = page_it->next_page;
		}
#endif

		destroy_i(first_page);
	}

	void* alloc(size_t size)
	{
#ifdef _DEBUG
		assert(initialized);
		assert(!deinitialized);
#endif
		Page* page_it = first_page;
		Page* prev_page_it = nullptr;
		while (page_it) {
			Bucket* list_it = page_it->free_list_begin;
			while (list_it) {
				if (list_it->size >= size) {
					// correct block!
					return alloc_block(list_it, page_it, size);
				}

				list_it = list_it->next_bucket;
			}

			prev_page_it = page_it;
			page_it = page_it->next_page;
		}
		// no free space, let's allocate new page
		LPVOID new_page_ptr = VirtualAlloc(NULL, CoalesedPageSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		Page* new_page = new (new_page_ptr) Page();

		if (prev_page_it) {
			prev_page_it->next_page = new_page;
		}
		return alloc_block(new_page->free_list_begin, new_page, size);
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
		assert(old_bucket->red_zone == 0xDEADBEEF);
#endif

		if (old_bucket->prev_bucket && old_bucket->prev_bucket->freed) {
			if (old_bucket->next_bucket && old_bucket->next_bucket->freed) {
				// let's unite prev bucket with current
				old_bucket->prev_bucket->size += sizeof(Bucket) + old_bucket->size;
				old_bucket->prev_bucket->next_bucket = old_bucket->next_bucket;

				if (old_bucket->next_bucket) {
					old_bucket->next_bucket->prev_bucket = old_bucket->prev_bucket;
				}
				old_bucket = old_bucket->prev_bucket;


				// it's needed to remove this block from free-list
				if (old_bucket->next_bucket->prev_free_bucket) {
					old_bucket->next_bucket->prev_free_bucket->next_free_bucket = old_bucket->next_bucket->next_free_bucket;
				}
				else {
					old_bucket->page->free_list_begin = old_bucket->next_bucket->next_free_bucket;
					if (old_bucket->next_bucket->next_free_bucket) {
						old_bucket->next_bucket->next_free_bucket->prev_free_bucket = nullptr;
					}
				}

				if (old_bucket->next_bucket->next_free_bucket) {
					old_bucket->next_bucket->next_free_bucket->prev_free_bucket = old_bucket->next_bucket->prev_free_bucket;
				}

				// and then unite
				old_bucket->size += sizeof(Bucket) + old_bucket->next_bucket->size;

				if (old_bucket->next_bucket->next_bucket) {
					old_bucket->next_bucket->next_bucket->prev_bucket = old_bucket;
				}

				old_bucket->next_bucket = old_bucket->next_bucket->next_bucket;
			}
			else {
				// let's unite prev bucket with current
				old_bucket->prev_bucket->size += sizeof(Bucket) + old_bucket->size;
				old_bucket->prev_bucket->next_bucket = old_bucket->next_bucket;

				if (old_bucket->next_bucket) {
					old_bucket->next_bucket->prev_bucket = old_bucket->prev_bucket;
				}
				old_bucket = old_bucket->prev_bucket;
			}
		}
		else {
			if (old_bucket->next_bucket && old_bucket->next_bucket->freed) {
				// let's steal data from next buffer

				old_bucket->size += sizeof(Bucket) + old_bucket->next_bucket->size;
				old_bucket->next_free_bucket = old_bucket->next_bucket->next_free_bucket;

				if (old_bucket->next_bucket->next_free_bucket) {
					old_bucket->next_bucket->next_free_bucket->prev_free_bucket = old_bucket;
				}

				old_bucket->prev_free_bucket = old_bucket->next_bucket->prev_free_bucket;

				if (old_bucket->next_bucket->prev_free_bucket) {
					old_bucket->next_bucket->prev_free_bucket->next_free_bucket = old_bucket;
				}
				else {
					old_bucket->page->free_list_begin = old_bucket;
				}

				if (old_bucket->next_bucket->next_bucket) {
					old_bucket->next_bucket->next_bucket->prev_bucket = old_bucket;
				}
				old_bucket->next_bucket = old_bucket->next_bucket->next_bucket;
			}
			else {
				// just free - add ourself to free-list

				old_bucket->next_free_bucket = old_bucket->page->free_list_begin;
				old_bucket->prev_free_bucket = nullptr;
				if (old_bucket->page->free_list_begin) {
					old_bucket->page->free_list_begin->prev_free_bucket = old_bucket;
				}
				old_bucket->page->free_list_begin = old_bucket;
			}
		}
		old_bucket->freed = true;
	}

#ifdef _DEBUG
	int get_allocated_blocks() const
	{
		int total_blocks = 0;
		int total_free_blocks = 0;

		Page* page_it = first_page;
		while (page_it) {
			Bucket* it = reinterpret_cast<Bucket*>(reinterpret_cast<std::byte*>(page_it) + sizeof(Page));

			int total_pages_blocks = 0;
			int freed_blocks = 0;
			while (it) {
				++total_pages_blocks;
				if (it->freed) {
					++freed_blocks;
				}
				it = it->next_bucket;
			}

			total_free_blocks += freed_blocks;
			total_blocks += total_pages_blocks;
			page_it = page_it->next_page;
		}

		return total_blocks - total_free_blocks;
	}

	int get_freed_blocks() const
	{
		int total_free_blocks = 0;

		Page* page_it = first_page;
		while (page_it) {
			Bucket* it = reinterpret_cast<Bucket*>(reinterpret_cast<std::byte*>(page_it) + sizeof(Page));

			int freed_blocks = 0;
			while (it) {
				if (it->freed) {
					++freed_blocks;
				}
				it = it->next_bucket;
			}

			total_free_blocks += freed_blocks;
			page_it = page_it->next_page;
		}

		return total_free_blocks;
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

		Page* page_it = first_page;
		while (page_it) {
			// std::cout << "Page, size " << CoalesedPageSize << ", block statistics:" << std::endl;

			Bucket* it = reinterpret_cast<Bucket*>(reinterpret_cast<std::byte*>(page_it) + sizeof(Page));

			int total_pages_blocks = 0;
			int freed_blocks = 0;
			while (it) {
				++total_pages_blocks;
				if (it->freed) {
					++freed_blocks;
				}
				it = it->next_bucket;
			}

			std::cout << "Total blocks: " << total_blocks << std::endl;
			std::cout << "Freed blocks: " << freed_blocks << std::endl;
			std::cout << "Allocated blocks: " << total_blocks - freed_blocks << std::endl << std::endl;

			total_free_blocks += freed_blocks;
			total_blocks += total_pages_blocks;
			page_it = page_it->next_page;
			++pages_size;
		}

		// std::cout << std::endl << "Total block statistics:" << std::endl;
		// std::cout << "Total pages: " << pages_size << std::endl;
		std::cout << "Total blocks: " << total_blocks << std::endl;
		std::cout << "Freed blocks: " << total_free_blocks << std::endl;
		std::cout << "Allocated blocks: " << total_blocks - total_free_blocks << std::endl << std::endl;

		// TODO: ¬ыводит в стандартный поток вывода статистику по аллокатору: количество зан€тых и свободных блоков, список блоков запрошенных у ќ— и их размеры
	}

	void dumpBlocks() const
	{
		assert(initialized);
		assert(!deinitialized);

		Page* page_it = first_page;
		while (page_it) {
			Bucket* it = reinterpret_cast<Bucket*>(reinterpret_cast<std::byte*>(page_it) + sizeof(Page));

			while (it) {
				if (!it->freed) {
					std::cout << "size - " << it->size << std::endl;
					std::cout << "ptr - " << it + sizeof(Bucket) << std::endl;
				}
				it = it->next_bucket;
			}
			page_it = page_it->next_page;
		}
	}
#endif

private:
	void destroy_i(Page* page_it)
	{
		if (!page_it) {
			return;
		}
		destroy_i(page_it->next_page);
		VirtualFree(page_it, 0, MEM_RELEASE);
	}

	void* alloc_block(Bucket* list_it, Page* page, size_t size)
	{
		//let's try to split
		if (list_it->size - size > sizeof(Bucket)) {
			Bucket* new_bucket = new (reinterpret_cast<std::byte*>(list_it) + sizeof(Bucket) + size)Bucket(list_it, list_it->prev_free_bucket, page, list_it->size - size - sizeof(Bucket));
			new_bucket->next_bucket = list_it->next_bucket;
			if (list_it->next_bucket) {
				list_it->next_bucket->prev_bucket = new_bucket;
			}
			list_it->next_bucket = new_bucket;

			new_bucket->next_free_bucket = list_it->page->free_list_begin;
			list_it->page->free_list_begin->prev_free_bucket = new_bucket;
			list_it->page->free_list_begin = new_bucket;
		}
		else {
			// diff is too small
		}
		list_it->size = size;
		list_it->freed = false;

		if (list_it->prev_free_bucket) {
			list_it->prev_free_bucket->next_free_bucket = list_it->next_free_bucket;
		}
		else {
			list_it->page->free_list_begin = list_it->next_free_bucket;
		}

		if (list_it->next_free_bucket) {
			list_it->next_free_bucket->prev_free_bucket = list_it->prev_free_bucket;
		}
		// prev and next free buckets are invalidated

		return reinterpret_cast<std::byte*>(list_it) + sizeof(Bucket);
	}

	Page* first_page;
#ifdef _DEBUG
	bool initialized = false;
	bool deinitialized = false;
#endif
};