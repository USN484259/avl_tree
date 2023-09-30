#include <cstdio>
#include <mutex>
#include <vector>
#include <random>
#include <algorithm>

#include "sys/mman.h"

#define panic(fmt, ...) \
	fprintf(stderr, fmt "\n", __VA_ARGS__), abort()

#define AVL_TREE_DEBUG
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
	mmap_allocator allocator;
	avl_heap<std::mutex, mmap_allocator> heap(allocator);
	random_device rng;
	mt19937_64 g(rng());

	vector<pair<void *, size_t> > pool;
	for (auto i = 0; i < 10000; ++i) {
		size_t size = g() & 0xFFFF;
		auto ptr = heap.alloc(size);
		pool.push_back(make_pair(ptr, size));
	}

	shuffle(pool.begin(), pool.end(), g);

	printf("used %lX, total %lX\n", heap.used(), heap.total());

	for (auto &it : pool) {
		auto ptr = it.first;
		auto size = it.second;
		heap.free(ptr, size);
	}

	printf("used %lX, total %lX\n", heap.used(), heap.total());

	return 0;
}
