#pragma once

#include <cstdint>

#include "avl_tree.hpp"

#ifndef container_of
#define container_of(ptr, type, member) \
	((type *)((uint8_t *)ptr - offsetof(type, member)))
#endif

#ifndef panic
#define panic(...) abort()
#endif


template<typename Mutex, typename Alloc>
class avl_heap {
#if PTRDIFF_WIDTH == 64
	typedef uint32_t half_size_t;
#elif PTRDIFF_WIDTH == 32
	typedef uint16_t half_size_t;
#endif
	static constexpr size_t half_size_mask = half_size_t(-1);
	static constexpr size_t half_size_shift = sizeof(half_size_t) * 8;

	typedef avl_node<half_size_t> node;
	static_assert(sizeof(node) == 4 * sizeof(void *));

	struct block {
		node size_node;
		node addr_node;

		block(size_t size) : size_node(size & half_size_mask), addr_node(size >> half_size_shift) {}
		size_t size(void) const {
			return (size_t(addr_node.data) << half_size_shift) | size_t(size_node.data);
		}
	};
	static_assert(sizeof(block) == 8 * sizeof(void *));

	struct size_cmp {
		bool operator()(const node *a, const node *b) const {
			return container_of(a, block, size_node)->size() < container_of(b, block, size_node)->size();
		}
	};
	struct addr_cmp {
		bool operator()(const node *a, const node *b) const {
			return container_of(a, block, size_node) < container_of(b, block, size_node);
		}
	};

	Mutex mutex;
	avl_tree<half_size_t, size_cmp> size_tree;
	avl_tree<half_size_t, addr_cmp> addr_tree;
	size_t used_size = 0;
	size_t total_size = 0;
	Alloc &allocator;

public:
	static constexpr size_t min_size = sizeof(block);
	static constexpr size_t alignment = 0x10;
	static constexpr size_t align_mask = alignment - 1;
	static_assert(0 == (min_size & align_mask));
	static_assert(0 == (Alloc::alignment & align_mask));

public:
	avl_heap(Alloc &a) : allocator(a) {}
	avl_heap(const avl_heap &) = delete;
	~avl_heap(void) = default;

	size_t used(void) const {
		return used_size;
	}
	size_t total(void) const {
		return total_size;
	}

	void *alloc(size_t &size) {
		size = align_size(size);
		block *found_block;
		while (true) {
			mutex.lock();
			found_block = nullptr;
			size_tree.search([&](const node *cur) {
				auto *cur_block = container_of(cur, block, size_node);
				if (cur_block->size() >= size) {
					found_block = cur_block;
					return -1;
				} else {
					return 1;
				}
			});
			if (found_block)
				break;

			mutex.unlock();
			if (!expand(size << 4))
				return nullptr;
		}
		// mutex still locked
		auto block_size = found_block->size();
		size_tree.erase(&found_block->size_node);
		addr_tree.erase(&found_block->addr_node);
		found_block->~block();
		if (block_size - size >= min_size) {
			auto new_block = new((uint8_t *)found_block + size) block(block_size - size);
			size_tree.insert(&new_block->size_node);
			addr_tree.insert(&new_block->addr_node);
		} else {
			size = block_size;
		}

		used_size += size;
		mutex.unlock();
		return found_block;
	}
	void free(void *ptr, size_t size) {
		if (!ptr || size < min_size || (ptrdiff_t(ptr) & align_mask))
			panic("ptr %p, size %lX", ptr, size);
		mutex.lock();
		insert(new(ptr) block(size));
		used_size -= size;
		mutex.unlock();
	}
	void *realloc(void *ptr, size_t old_size, size_t &new_size) {
		return nullptr;
	}
	size_t expand(size_t size) {
		auto ptr = allocator.alloc(size);
		if (ptr && size >= min_size) {
			mutex.lock();
			insert(new(ptr) block(size));
			total_size += size;
			mutex.unlock();
		} else {
			size = 0;
		}
		return size;
	}
private:
	static inline size_t align_size(size_t size) {
		if (size <= min_size)
			return min_size;
		return (size + align_mask) & (~align_mask);
	}
	void insert(block *cur_block) {
		size_tree.insert(&cur_block->size_node);
		addr_tree.insert(&cur_block->addr_node);

		auto prev_node = addr_tree.prev_node(&cur_block->addr_node);
		if (prev_node) {
			auto prev_block = container_of(prev_node, block, addr_node);
			if (merge_block(prev_block, cur_block))
				cur_block = prev_block;
		}
		auto next_node = addr_tree.next_node(&cur_block->addr_node);
		if (next_node) {
			auto next_block = container_of(next_node, block, addr_node);
			merge_block(cur_block, next_block);
		}
	}
	bool merge_block(block *base, block *extra) {
		if ((uint8_t *)base + base->size() > (uint8_t *)extra)
			panic("base %p, %lX; extra %p, %lX", base, base->size(), extra, extra->size());

		if ((uint8_t *)base + base->size() < (uint8_t *)extra)
			return false;

		size_tree.erase(&base->size_node);
		addr_tree.erase(&base->addr_node);
		size_tree.erase(&extra->size_node);
		addr_tree.erase(&extra->addr_node);

		auto new_size = base->size() + extra->size();
		base->~block();
		extra->~block();
		new(base) block(new_size);

		size_tree.insert(&base->size_node);
		addr_tree.insert(&base->addr_node);

		return true;
	}
};
