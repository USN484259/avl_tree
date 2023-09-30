#pragma once

#include <cassert>
#include <functional>

#if defined(AVL_TREE_DEBUG) || defined(AVL_TREE_DUMP)
#include <cstdio>
#endif

/*
right rotate truth table for `balance`

cur	top	cur’	top’
-2	-1	0	0
-2	0	-1	1
-2	-2	1	-1
-1	0	0	1
-1	1	0	2
-1	-1	1	1

*/


template<typename T>
struct avl_node {
	avl_node *left = nullptr;
	avl_node *right = nullptr;
	avl_node *parent = nullptr;
	int8_t balance = 0;
	T data;

	template<typename ... Arg>
	avl_node(Arg&& ... args) : data(std::forward<Arg>(args) ...) {}
};

template<>
struct avl_node<void> {
	avl_node *left = nullptr;
	avl_node *right = nullptr;
	avl_node *parent = nullptr;
	int8_t balance = 0;
};

static_assert(sizeof(avl_node<void>) == sizeof(void *) * 4);

template<typename T, typename C>
class avl_tree {
public:
	typedef avl_node<T> node;

private:
	node *root = nullptr;

public:
	avl_tree(void) = default;
	avl_tree(const avl_tree &) = delete;
	avl_tree(avl_tree &&other) {
		std::swap(root, other.root);
	}
	~avl_tree(void) = default;

	bool empty(void) const {
		return root == nullptr;
	}
	template<typename F>
	void clear(F &&func) {
		erase_subtree(root, func);
		root = nullptr;
	}

	node *head_node(void) {
		if (!root)
			return nullptr;
		node *cur = root;
		while (cur->left)
			cur = cur->left;
		return cur;
	}
	node *tail_node(void) {
		if (!root)
			return nullptr;
		node *cur = root;
		while (cur->right)
			cur = cur->right;
		return cur;
	}
	node *prev_node(node *cur) {
		assert(cur);
		if (cur->left) {
			cur = cur->left;
			while (cur->right)
				cur = cur->right;
			return cur;
		}
		while (cur) {
			auto side = get_side(cur);
			cur = cur->parent;
			if (side != -1)
				break;
		}
		return cur;
	}
	node *next_node(node *cur) {
		assert(cur);
		if (cur->right) {
			cur = cur->right;
			while (cur->left)
				cur = cur->left;
			return cur;
		}
		while (cur) {
			auto side = get_side(cur);
			cur = cur->parent;
			if (side != 1)
				break;
		}
		return cur;
	}

	template<typename F>
	node *search(F &&func) const {
		node *cur = root;
		while (cur) {
			auto side = func((const node *)cur);
			if (side == 0)
				break;
			else if (side < 0)
				cur = cur->left;
			else if (side > 0)
				cur = cur->right;
		}
		return cur;
	}

	void insert(node *new_node) {
		assert(new_node && !new_node->left && !new_node->right && !new_node->parent && !new_node->balance);

		C less_than;
		auto cur = root;
		int8_t side = 0;
		while (cur) {
			if (less_than(new_node, cur))
				side = -1;	// go left
			else if (less_than(cur, new_node))
				side = 1;	// go right
			else
				side = (cur->balance > 0) ? -1 : 1;

			if (side == -1) {
				if (cur->left)
					cur = cur->left;
				else
					break;
			} else if (side == 1) {
				if (cur->right)
					cur = cur->right;
				else
					break;
			}
		}
		switch (side) {
		case 0:
			assert(!root);
			root = new_node;
			return;
		case -1:
			assert(cur && !cur->left);
			cur->left = new_node;
			break;
		case 1:
			assert(cur && !cur->right);
			cur->right = new_node;
			break;
		}

		new_node->parent = cur;
		rebalance(cur, side, false);
#ifdef AVL_TREE_DEBUG
		check_integrity(__func__);
#endif
	}

	void erase(node *del_node) {
		assert(del_node);
		if (is_leaf(del_node)) {
			unlink(del_node);	// rebalanced
#ifdef AVL_TREE_DUMP
			dump(__func__);
#endif
#ifdef AVL_TREE_DEBUG
			check_integrity(__func__);
#endif
			return;
		}

		// find replace node in bigger subtree
		node *replace_node = nullptr;
		int8_t side = (del_node->balance > 0) ? 1 : -1;
		for (auto i = 1; i >= -1 && !replace_node; i -= 2) {
			switch (side * i) {
			case -1:
				replace_node = prev_node(del_node);
				break;
			case 1:
				replace_node = next_node(del_node);
				break;
			}
			if (!is_leaf(replace_node))
				replace_node = nullptr;
		}
		if (replace_node) {
			unlink(replace_node);	// rebalanced
		} else {
			switch (side) {
			case -1:
				replace_node = prev_node(del_node);
				break;
			case 1:
				replace_node = next_node(del_node);
				break;
			}
			erase(replace_node);
		}

		assert(replace_node);

		replace_node->left = del_node->left;
		if (del_node->left) {
			assert(del_node->left->parent == del_node);
			del_node->left->parent = replace_node;
		}
		replace_node->right = del_node->right;
		if (del_node->right) {
			assert(del_node->right->parent == del_node);
			del_node->right->parent = replace_node;
		}
		replace_node->parent = del_node->parent;
		switch (get_side(del_node)) {
		case 0:
			assert(del_node == root);
			root = replace_node;
			break;
		case -1:
			del_node->parent->left = replace_node;
			break;
		case 1:
			del_node->parent->right = replace_node;
			break;
		}
		replace_node->balance = del_node->balance;
#ifdef AVL_TREE_DUMP
		dump(__func__);
#endif
#ifdef AVL_TREE_DEBUG
		check_integrity(__func__);
#endif
	}

#ifdef AVL_TREE_DEBUG
	void check_integrity(const char *str) {
		printf("%p\t%s: %s start\n", this, str, __func__);
		size_t count = 0;
		unsigned depth = 0;
		if (root)
			depth = check_node(root, count);
		printf("%p\t%s: %s end, count %lu, depth %u\n", this, str, __func__, count, depth);
	}
#endif

#ifdef AVL_TREE_DUMP
	void dump(const char *str, FILE *out = stderr) {
		printf("%p\t%s:%s\n", this, str, __func__);
		fprintf(out, "strict digraph \"%p\" {\n\tnode\t[shape=box];\n", this);
			dump_node(root, out);
		fprintf(out, "}\n");
	}
#endif
private:
	bool is_leaf(node *cur) const {
		if (cur->left || cur->right)
			return false;
		assert(cur->balance == 0);
		return true;
	}
	int8_t get_side(node *cur) {
		auto parent = cur->parent;
		if (!parent) {
			assert(cur == root);
			return 0;
		} else if (parent->left == cur) {
			return -1;
		} else if (parent->right == cur) {
			return 1;
		} else assert(false);
	}
	template<typename F>
	void erase_subtree(node *cur, F &func) {
		if (!cur)
			return;
		erase_subtree(cur->left, func);
		erase_subtree(cur->right, func);
		func(cur);
	}
	void unlink(node *cur) {
		assert(is_leaf(cur));
		auto side = get_side(cur);
		switch (side) {
		case 0:
			assert(cur == root);
			root = nullptr;
			return;
		case -1:
			cur->parent->left = nullptr;
			break;
		case 1:
			cur->parent->right = nullptr;
			break;
		}
		rebalance(cur->parent, side, true);
	}
	void rebalance(node *cur, int8_t diff, bool remove) {
		assert(cur && (diff == -1 || diff == 1));

		auto parent = cur->parent;
		auto side = get_side(cur);
		if (remove) {
			cur->balance -= diff;
			if (cur->balance == 0 && parent) {
				// !=0 before, -1 depth, propagate to parent
				rebalance(parent, side, remove);
				return;
			}
		} else {
			cur->balance += diff;
			if (cur->balance == diff && parent) {
				// =0 before, +1 depth, propagate to parent
				rebalance(parent, side, remove);
				return;
			}
		}

		if (cur->balance >= -1 && cur->balance <= 1)
			return;

#ifdef AVL_TREE_DUMP
		dump(__func__);
#endif
		if (cur->balance == -2) {
			if (cur->left->balance == 1) {
				// LR rotation
				cur->left = rotate_left(cur->left);
#ifdef AVL_TREE_DUMP
				dump(__func__);
#endif
				cur = rotate_right(cur);
			} else {
				// RR rotation
				cur = rotate_right(cur);
			}
		} else if (cur->balance == 2) {
			if (cur->right->balance == -1) {
				// RL rotation
				cur->right = rotate_right(cur->right);
#ifdef AVL_TREE_DUMP
				dump(__func__);
#endif
				cur = rotate_left(cur);
			} else {
				// LL rotation
				cur = rotate_left(cur);
			}
		}

		if (side == 0) {
			assert(parent == nullptr);
			root = cur;
		} else if (side == -1) {
			parent->left = cur;
		} else if (side == 1) {
			parent->right = cur;
		}
#ifdef AVL_TREE_DUMP
		dump(__func__);
#endif
		if (remove && parent && cur->balance == 0) {
			// erase..rebalance, -1 depth, propagate to parent
			assert(parent == cur->parent);
			rebalance(parent, get_side(cur), remove);
		}
	}
	node *rotate_left(node *cur) {
		auto *top = cur->right;

		cur->right = top->left;
		if (top->left) {
			assert(top->left->parent == top);
			top->left->parent = cur;
		}
		top->left = cur;
		top->parent = cur->parent;
		cur->parent = top;

		bool same = (cur->balance == top->balance);
		cur->balance--;
		if (top->balance > 0)
			cur->balance -= top->balance;
		top->balance -= 1 + same;

		return top;
	}
	node *rotate_right(node *cur) {
		auto *top = cur->left;

		cur->left = top->right;
		if (top->right) {
			assert(top->right->parent == top);
			top->right->parent = cur;
		}
		top->right = cur;
		top->parent = cur->parent;
		cur->parent = top;

		bool same = (cur->balance == top->balance);
		cur->balance++;
		if (top->balance < 0)
			cur->balance -= top->balance;
		top->balance += 1 + same;

		return top;
	}
#ifdef AVL_TREE_DEBUG
	unsigned check_node(node *cur, size_t &count) {
		C less_than;
		assert(cur);
		++count;

		if (cur->parent)
			assert((cur->parent->left == cur) ^ (cur->parent->right == cur));
		else
			assert(cur == root);

		assert(cur->balance >= -1 && cur->balance <= 1);

		if (is_leaf(cur))
			return 1;
		unsigned depth_l = 0, depth_r = 0;
		if (cur->left) {
			assert(cur->left->parent == cur);
			assert(!less_than(cur, cur->left));
			depth_l = check_node(cur->left, count);
		}
		if (cur->right) {
			assert(cur->right->parent == cur);
			assert(!less_than(cur->right, cur));
			depth_r = check_node(cur->right, count);
		}
		assert(depth_l + cur->balance == depth_r);
		return std::max(depth_l, depth_r) + 1;
	}
#endif

#ifdef AVL_TREE_DUMP
	void dump_node(node *cur, FILE *out) {
		if (!cur)
			return;
		fprintf(out, "\t\"%p\"\t[label=\"id = %04X\\nbalance = %d\"];\n", cur, unsigned((uintptr_t)cur & 0xFFFF), cur->balance);
		if (cur->parent)
			fprintf(out, "\t\"%p\" -> \"%p\"\t[label=\"parent\"];\n", cur, cur->parent);
		if (cur->left)
			fprintf(out, "\t\"%p\" -> \"%p\"\t[label=\"left\"];\n", cur, cur->left);
		if (cur->right)
			fprintf(out, "\t\"%p\" -> \"%p\"\t[label=\"right\"];\n", cur, cur->right);

		dump_node(cur->left, out);
		dump_node(cur->right, out);
	}
#endif
};
