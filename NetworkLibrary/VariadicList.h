#pragma once

template <typename... params>
struct TYPE_LIST {
};

template <typename First>
struct TYPE_LIST<First> {
	typedef TYPE_LIST<First> type;
	typedef null_type next;
	typedef First current;
};

template <typename First, typename... Rest>
struct TYPE_LIST<First, Rest...> {
	typedef TYPE_LIST<First, Rest...> type;
	typedef typename TYPE_LIST<Rest...> next;
	typedef First current;
};

template <typename LIST>
struct length {
	static const int val = 1 + length<LIST::next>::val;
};

template <>
struct length {
	static const int val = 0;
};

