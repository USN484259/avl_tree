#pragma once

#include <cstdint>

#ifdef AVL_HEAP_DEBUG
#include <cstdio>
#endif

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
	static constexpr size_t alignment = min_size;
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
		void *ptr = nullptr;
		while (true) {
			mutex.lock();
			ptr = allocate_once(size);
			if (ptr)
				break;
			mutex.unlock();
			if (!expand(size << 4))
				return nullptr;
		}
		// mutex still locked
		used_size += size;
		mutex.unlock();
		return ptr;
	}
	void free(void *ptr, size_t size) {
		if (!ptr || size < min_size || (ptrdiff_t(ptr) & align_mask))
			panic("ptr %p, size %lX", ptr, size);
		mutex.lock();
		insert(new(ptr) block(size));
		used_size -= size;
		mutex.unlock();
	}
	void *realloc(void *ptr, const size_t old_size, size_t &new_size) {
		if (!ptr)
			return alloc(new_size);
		if (!ptr || old_size < min_size || (ptrdiff_t(ptr) & align_mask))
			panic("ptr %p, size %lX", ptr, old_size);
		if (new_size == 0) {
			free(ptr, old_size);
			return nullptr;
		}

		new_size = align_size(new_size);
		if (new_size <= old_size) {
			// request shrinking
			auto spare_size = old_size - new_size;
			if (spare_size < min_size) {
				// not worth shrinking
				new_size = old_size;
				return ptr;
			}
			mutex.lock();
			insert(new((uint8_t *)ptr + new_size) block(spare_size));
			used_size -= spare_size;
			mutex.unlock();
			return ptr;
		}
		// else: request expanding
		mutex.lock();
		// find the block just prior to ptr
		block *prior_block = nullptr;
		block *adjacent_block = nullptr;
		addr_tree.search([&](const node *cur) {
			auto *cur_block = container_of(cur, block, addr_node);
			if ((uint8_t *)cur_block < (uint8_t *)ptr) {
				prior_block = cur_block;
				return 1;
			} else {
				adjacent_block = cur_block;
				return -1;
			}
		});

		// first check adjacent block
		if (adjacent_block) {
			auto block_size = adjacent_block->size();
			auto expect_addr = (uint8_t *)ptr + old_size;
			assert((uint8_t *)adjacent_block >= expect_addr);

			if ((uint8_t *)adjacent_block == expect_addr && old_size + block_size >= new_size) {
				// okay to expand
				auto expand_size = new_size - old_size;
				allocate_at(adjacent_block, expand_size, false);
				new_size = old_size + expand_size;
				used_size += expand_size;
				mutex.unlock();
				return ptr;
			}
		}

		// then try allocate at another position and memcpy
		{
			auto new_ptr = allocate_once(new_size);
			if (new_ptr) {
				used_size += new_size;
				mutex.unlock();
				memcpy(new_ptr, ptr, old_size);
				free(ptr, old_size);
				return new_ptr;
			}
		}
		// lastly check if we can use the prior block
		if (prior_block) {
			auto block_size = prior_block->size();
			auto expect_addr = (uint8_t *)prior_block + block_size;
			assert(expect_addr <= (uint8_t *)ptr);

			if (expect_addr == (uint8_t *)ptr && old_size + block_size >= new_size) {
#ifdef AVL_HEAP_DEBUG
			printf("%s: %p expand to prior block %p %lx\n", __func__, ptr, prior_block, block_size);
#endif

				auto expand_size = new_size - old_size;
				auto new_ptr = allocate_at(prior_block, expand_size, true);
				new_size = old_size + expand_size;
				used_size += expand_size;
				mutex.unlock();
				memmove(new_ptr, ptr, old_size);
				return new_ptr;
			}
		}

		// finally expand heap and fallback to alloc
		mutex.unlock();
		if (!expand(new_size << 4))
			return nullptr;
		auto new_ptr = alloc(new_size);
		if (!new_ptr)
			return nullptr;
		memcpy(new_ptr, ptr, old_size);
		free(ptr, old_size);
		return new_ptr;
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
	size_t shrink(size_t hint = 0) {
		size_t shrink_size = 0;
		mutex.lock();
		auto *cur_node = size_tree.tail_node();
		while (cur_node && (hint == 0 || shrink_size < hint)) {
			auto *top_block = container_of(cur_node, block, size_node);
			 cur_node = size_tree.prev_node(cur_node);
			auto block_size = top_block->size();
#ifdef AVL_HEAP_DEBUG
			printf("%s: top_block %p %lx\n", __func__, top_block, block_size);
#endif
			if (block_size < Alloc::alignment)
				break;
			auto ptr = align_addr(top_block, Alloc::alignment);
			// num bytes before aligned page
			size_t offset = ptr - (uint8_t *)top_block;
			auto size = block_size - offset;
			// page count
			auto count = size / Alloc::alignment;
			// num bytes after aligned page
			auto tail = size - (count * Alloc::alignment);

			// adjust page boundary to reserve space for blocks
			while (count && offset && offset < min_size) {
				offset += Alloc::alignment;
				ptr += Alloc::alignment;
				count--;
			}
			while (count && tail && tail < min_size) {
				tail += Alloc::alignment;
				count--;
			}
			auto aligned_size = count * Alloc::alignment;
			assert(offset + tail + aligned_size == block_size);
			if (count == 0) {
				if (hint == 0)
					break;
				else
					continue;
			}
#ifdef AVL_HEAP_DEBUG
			printf("%s: aligned %p, size %lx, offset %lx, tail %lx\n", __func__, ptr, aligned_size, offset, tail);
#endif
			// detach top_block, construct tail block and put back
			size = offset + aligned_size;
			allocate_at(top_block, size, false);
			assert(size == offset + aligned_size);
			if (offset) {
				// construct head block and put back
				insert(new(top_block) block(offset));
			}
			// release memory pages
			allocator.free(ptr, aligned_size);
			shrink_size += aligned_size;
		}
#ifdef AVL_HEAP_DEBUG
			printf("%s: done %lx\n", __func__, shrink_size);
#endif
		total_size -= shrink_size;
		mutex.unlock();
		return shrink_size;
	}
private:
	static inline size_t align_size(size_t size) {
		if (size <= min_size)
			return min_size;
		return (size + align_mask) & (~align_mask);
	}
	static inline uint8_t *align_addr(void *ptr, size_t alignment) {
		return (uint8_t *)(((uintptr_t)ptr + alignment - 1) & ~(alignment - 1));
	}
	void *allocate_once(size_t &size) {
		block *found_block = nullptr;
		size_tree.search([&](const node *cur) {
			auto *cur_block = container_of(cur, block, size_node);
			if (cur_block->size() >= size) {
				found_block = cur_block;
				return -1;
			} else {
				return 1;
			}
		});
		if (!found_block)
			return nullptr;
		return allocate_at(found_block, size);
	}
	void *allocate_at(block *base, size_t &size, bool tail = false) {
		auto block_size = base->size();
		assert(block_size >= size);
		size_tree.erase(&base->size_node);
		addr_tree.erase(&base->addr_node);
		base->~block();
		if (block_size - size >= min_size) {
			auto new_block = new((uint8_t *)base + (tail ? 0 : size)) block(block_size - size);
			size_tree.insert(&new_block->size_node);
			addr_tree.insert(&new_block->addr_node);
			return (uint8_t *)base + (tail ? (block_size - size) : 0);
		} else {
			size = block_size;
			return base;
		}
#ifdef AVL_HEAP_DEBUG
		walk_heap(__func__);
#endif
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
#ifdef AVL_HEAP_DEBUG
		walk_heap(__func__);
#endif
	}
	bool merge_block(block *base, block *extra) {
		if ((uint8_t *)base + base->size() > (uint8_t *)extra)
			panic("base %p, %lX; extra %p, %lX", base, base->size(), extra, extra->size());
#ifdef AVL_HEAP_DEBUG
		printf("merging block %p %lx; %p %lx\n", base, base->size(), extra, extra->size());
#endif
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
#ifdef AVL_HEAP_DEBUG
	void walk_heap(const char *name) {
		uint8_t *last_ptr = nullptr;
		printf("%s %s start\n", __func__, name);
		for (auto *cur_node = addr_tree.head_node(); cur_node; cur_node = addr_tree.next_node(cur_node)) {
			auto *cur_block = container_of(cur_node, block, addr_node);
			auto block_size = cur_block->size();
			printf("%s: %p %lx\n", __func__, cur_block, block_size);
			auto ptr = (uint8_t *)cur_block + block_size;
			assert(!last_ptr || last_ptr < (uint8_t *)cur_block);
			last_ptr = ptr;
		}
		printf("%s %s end\n", __func__, name);
	}
#endif
};
