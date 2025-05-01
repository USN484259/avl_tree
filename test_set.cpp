#include <cstdio>
#include <vector>
#include <set>
#include <random>
#include <algorithm>


#define AVL_TREE_DEBUG
// #define AVL_TREE_DUMP
// #define AVL_SET_DEBUG
#include "avl_set.hpp"

using namespace std;

int main(void) {
	avl_set<uint64_t> container;
	std::multiset<uint64_t> ref_tree;

	typedef std::pair<decltype(container)::iterator, decltype(ref_tree)::iterator> it_pair_t;

	random_device rng;
	mt19937_64 g(rng());

	vector<it_pair_t> pool;
	for (auto i = 0; i < 10000; ++i) {
		auto val = g();
		auto it = container.insert(val);
		auto ref_it = ref_tree.insert(val);
		pool.push_back(std::make_pair(it, ref_it));
	}

	{
		auto it = container.begin();
		auto ref_it = ref_tree.begin();
		while (true) {
			if (it == container.end() || ref_it == ref_tree.end())
				break;
			assert(*it == *ref_it);
			++it;
			++ref_it;
		}
		assert(it == container.end() && ref_it == ref_tree.end());
	}

	shuffle(pool.begin(), pool.end(), g);

	for (auto &it_pair : pool) {
		container.erase(it_pair.first);
		ref_tree.erase(it_pair.second);
	}

	return 0;
}
