#pragma once

#include <algorithm>
#include <vector>

template <typename T>
void InsertByOrder(std::vector<T>& sorted, const T& item) {
    sorted.insert(std::ranges::upper_bound(sorted, item.Order, {}, &T::Order), item);
}
