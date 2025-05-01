all:	test_set test_heap

test_set:	test_set.cpp avl_set.hpp avl_tree.hpp
	g++ -g -Wall $< -o $@

test_heap:	test_heap.cpp avl_heap.hpp avl_tree.hpp
	g++ -g -Wall $< -o $@

.PHONY:	all

