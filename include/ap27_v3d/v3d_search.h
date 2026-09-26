#pragma once
#include <cstdint>
#include <vector>
#include <functional>

struct APHit { unsigned length; uint64_t first; };
void v3d_search_init();
void v3d_search_cleanup();
std::vector<APHit> search_ap27_k(unsigned K, unsigned start_shift, const uint64_t* n43,
                               const std::function<void(double)>& tile_done);
