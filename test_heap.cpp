#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>
#include <random>
#include <algorithm>

#include "sys/mman.h"

#define panic(fmt, ...) \
	fprintf(stderr, fmt "\n", __VA_ARGS__), abort()

// #define AVL_HEAP_DEBUG
// #define AVL_TREE_DUMP
// #define AVL_SET_DEBUG
#include "avl_heap.hpp"

using namespace std;

class mmap_allocator {
	void *hint = nullptr;
public:
	static constexpr size_t alignment = 0x1000;	// PAGE_SIZE;
	static constexpr size_t align_mask = alignment - 1;
public:
	void *alloc(size_t &size) {
		size = (size + align_mask) & (~align_mask);
		auto *ptr = mmap(hint, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (ptr) {
			hint = (uint8_t *)ptr + size;
		}
		return ptr;
	}
	void free(void *ptr, size_t size) {
		munmap(ptr, size);
	}
};

int main(void) {
	auto round = 5;
	mmap_allocator allocator;
	avl_heap<std::mutex, mmap_allocator> heap(allocator);
	random_device rng;
	mt19937_64 g(rng());

	vector<pair<void *, size_t> > pool;

	while (round--) {
		for (auto i = 0; i < 10000; ++i) {
			size_t size = g() & 0xFFFF;
			auto ptr = heap.alloc(size);
			pool.push_back(make_pair(ptr, size));
			memset(ptr, g(), size);
		}

		printf("alloc done: used %lX, total %lX\n", heap.used(), heap.total());

		shuffle(pool.begin(), pool.end(), g);

		auto top_size = heap.used();
		do {
			auto &rec = pool.back();
			auto ptr = rec.first;
			auto size = rec.second;
			pool.pop_back();
			heap.free(ptr, size);
			heap.shrink();
		} while (!pool.empty() && (round == 0 || heap.used() > (top_size << 2)));

		for (auto it = pool.begin(); it != pool.end(); ) {
			auto ptr = it->first;
			auto size = it->second;
			size_t new_size = g() & 0xFFFF;
			auto new_ptr = heap.realloc(ptr, size, new_size);
			if (!new_ptr) {
				if (new_size == 0) {
					it = pool.erase(it);
					continue;
				} else {
					printf("realloc failed %p %lu %lu\n", ptr, size, new_size);
				}
			} else {
				it->first = new_ptr;
				it->second = new_size;
				memset(new_ptr, g(), new_size);
			}

			++it;
		}
		printf("round end: used %lX, total %lX\n", heap.used(), heap.total());
	}
	assert(pool.empty());
	do {
		auto shrink_size = heap.shrink(0x10000);
		if (shrink_size == 0)
			break;
		printf("shrinked %lu pages\n", shrink_size / 0x1000);
	} while (true);

	printf("exiting: used %lX, total %lX\n", heap.used(), heap.total());

	return 0;
}
