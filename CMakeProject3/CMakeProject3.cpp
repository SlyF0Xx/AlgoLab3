// CMakeProject3.cpp: определяет точку входа для приложения.
//

#include "CMakeProject3.h"
#include "FixedSizeAllocator.h"
#include "CoalesedAllocator.h"
#include "MemoryAllocator.h"

#include <rapidcheck.h>

#include <algorithm>
#include <random>

using namespace std;

int main()
{
	rc::check("fixed size alllocator",
		[]() {
			const auto smallInts = *rc::gen::container<std::vector<int>>(rc::gen::inRange(1, 30));
			FixedSizeAllocator<64> allocator;
			allocator.init();

			std::vector<void*> ptrs;
			for (auto& value : smallInts) {
				ptrs.push_back(allocator.alloc(value));
			}

			auto rng = std::default_random_engine{};
			std::shuffle(ptrs.begin(), ptrs.end(), rng);

			for (auto& value : ptrs) {
				// shouldn't assert that there are corrupted block
				allocator.free(value);
			}

			// shouldn't assert that there are non freed blocks
			allocator.destroy();
		}
	);

	rc::check("coalesed alllocator",
		[]() {
			const auto smallInts = *rc::gen::container<std::vector<int>>(rc::gen::inRange(1, 30));
			CoalesedAllocator allocator;
			allocator.init();


			std::vector<void*> ptrs;
			for (auto& value : smallInts) {
				ptrs.push_back(allocator.alloc(value));
			}
			
			auto rng = std::default_random_engine{};
			std::shuffle(ptrs.begin(), ptrs.end(), rng);

			for (auto& value : ptrs) {
				// shouldn't assert that there are corrupted block
				allocator.free(value);
			}

			// shouldn't assert that there are non freed blocks
			allocator.destroy();
		}
	);

	rc::check("alllocator",
		[]() {
			const auto smallInts = *rc::gen::container<std::vector<int>>(rc::gen::inRange(1, 1024*1024*10));
			MemoryAllocator allocator;
			allocator.init();

			std::vector<void*> ptrs;
			for (auto& value : smallInts) {
				ptrs.push_back(allocator.alloc(value));
			}

			auto rng = std::default_random_engine{};
			std::shuffle(ptrs.begin(), ptrs.end(), rng);

			for (auto& value : ptrs) {
				// shouldn't assert that there are corrupted block
				allocator.free(value);
			}

			// shouldn't assert that there are non freed blocks
			allocator.destroy();
		}
	);

	cout << "Hello CMake." << endl;
	MemoryAllocator allocator;
	allocator.init();

	int* pi = reinterpret_cast<int*>(allocator.alloc(sizeof(int)));
	double* pd = reinterpret_cast<double*>(allocator.alloc(sizeof(double)));
	int* pa = reinterpret_cast<int*>(allocator.alloc(10 * sizeof(int)));
	int* a = reinterpret_cast<int*>(allocator.alloc(256 * sizeof(int)));

	allocator.dumpStat();
	allocator.dumpBlocks();


	allocator.free(a);
	allocator.free(pa);
	allocator.free(pd);
	allocator.free(pi);

	allocator.destroy();


	/*
	cout << "Hello CMake."  << endl;
	FixedSizeAllocator<512> allocator;
	allocator.init();

	int* pi = reinterpret_cast<int*>(allocator.alloc(sizeof(int)));
	double* pd = reinterpret_cast<double*>(allocator.alloc(sizeof(double)));
	int* pa = reinterpret_cast<int*>(allocator.alloc(10 * sizeof(int)));

	allocator.dumpStat();
	allocator.dumpBlocks();

	allocator.free(pa);
	allocator.free(pd);
	allocator.free(pi);

	allocator.destroy();
	*/

	/*
	CoalesedAllocator allocator;
	allocator.init();

	int* pi = reinterpret_cast<int*>(allocator.alloc(sizeof(int)));
	double* pd = reinterpret_cast<double*>(allocator.alloc(sizeof(double)));
	int* pa = reinterpret_cast<int*>(allocator.alloc(10 * sizeof(int)));

	// allocator.dumpStat();
	// allocator.dumpBlocks();

	allocator.free(pa);
	allocator.free(pd);
	allocator.free(pi);

	allocator.destroy();
	*/
	return 0;
}
