#include <cstdio>
#include <vector>
#include <random>
#include <algorithm>


#define AVL_TREE_DEBUG
// #define AVL_TREE_DUMP
// #define AVL_SET_DEBUG
#include "avl_set.hpp"

using namespace std;

int main(void) {
	avl_set<uint64_t> container;
	random_device rng;
	mt19937_64 g(rng());

	vector<decltype(container)::iterator> pool;
	for (auto i = 0; i < 10000; ++i) {
		auto val = g();
		pool.push_back(container.insert(val));
	}

	shuffle(pool.begin(), pool.end(), g);

	for (auto &it : pool) {
		container.erase(it);
	}

	return 0;
}
