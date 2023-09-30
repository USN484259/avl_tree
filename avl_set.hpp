#pragma once

#include <cassert>

#ifdef AVL_SET_DEBUG
#include <cstdio>
#endif

#include "avl_tree.hpp"


template<typename T, typename C = std::less<T> >
class avl_set {
	typedef avl_node<T> node;
	struct data_cmp {
		C less_than;
		bool operator()(const node *a, const node *b) const {
			return less_than(a->data, b->data);
		}
	};

	avl_tree<T, data_cmp> tree;
	size_t count = 0;
public:
	class iterator {
		friend class avl_set;

		avl_set *container;
		node *ptr;

		iterator(avl_set *cont, node *p = nullptr) : container(cont), ptr(p) {}
	public:
		iterator(const iterator &other) : container(other.container), ptr(other.ptr) {}
		~iterator(void) = default;

		iterator& operator=(const iterator &other) {
			container = other.container;
			ptr = other.ptr;
			return *this;
		}
		bool operator==(const iterator &other) const {
			return (container == other.container && ptr == other.ptr);
		}
		bool operator!=(const iterator &other) const {
			return (container != other.container || ptr != other.ptr);
		}

		T& operator*(void) {
			return ptr->data;
		}
		T* operator->(void) {
			return &ptr->data;
		}

		iterator& operator++(void) {
			if (ptr)
				ptr = container->tree.head_node();
			else
				ptr = container->tree.next_node(ptr);
			return *this;
		}
		iterator& operator--(void) {
			if (ptr)
				ptr = container->tree.tail_node();
			else
				ptr = container->tree.prev_node(ptr);
			return *this;
		}

	};
	friend class iterator;
public:
	avl_set(void) = default;
	avl_set(const avl_set &) = delete;
	avl_set(avl_set &&other) {
		std::swap(tree, other.tree);
		std::swap(count, other.count);
	}
	~avl_set(void) {
		clear();
	}

	bool empty(void) const {
		assert((count == 0) == tree.empty());
		return count == 0;
	}
	size_t size(void) const {
		return count;
	}

	void clear(void) {
		tree.clear([](node *ptr) {
			delete ptr;
		});
		assert(tree.empty());
		count = 0;
	}

	template<typename K, typename CMP = C>
	iterator find(const K &key) const {
		CMP less_than;
		node *ptr = tree.search([&](const node *cur) {
			if (less_than(key, cur->data))
				return -1;
			if (less_than(cur->data, key))
				return 1;
			return 0;
		});
		return iterator(this, ptr);
	}

	template<typename ... Arg>
	iterator insert(Arg&& ... args) {
		node *new_node = new node(std::forward<Arg>(args) ...);
#ifdef AVL_SET_DEBUG
		printf("%p\t%s\tnew_node %p\n", this, __func__, new_node);
#endif
		tree.insert(new_node);
		count++;
		return iterator(this, new_node);
	}
	iterator erase(iterator it) {
		auto del_node = it.ptr;
		++it;
#ifdef AVL_SET_DEBUG
		printf("%p\t%s\tdel_node %p\n", this, __func__, del_node);
#endif
		tree.erase(del_node);
		count--;
		delete del_node;
		return it;
	}
};
