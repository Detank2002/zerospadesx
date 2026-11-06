#pragma once
// Which voxels are solid, and nothing else: what the server's physics and its
// grenades ask of the map. One 64-bit word per (x, y) column, bit z set where
// the voxel is solid, so a column is answered from a single load. The colours
// live in VoxelMap, which fills this from its own columns.
#include <cstddef>
#include <cstdint>
#include <vector>

class SolidMap {
public:
    static constexpr int SIZE_X = 512, SIZE_Y = 512, SIZE_Z = 64;

    SolidMap() : cols_(static_cast<size_t>(SIZE_X) * SIZE_Y, 0) {}

    uint64_t column(int x, int y) const {
        if (x < 0 || x >= SIZE_X || y < 0 || y >= SIZE_Y) return 0;
        return cols_[static_cast<size_t>(y) * SIZE_X + x];
    }
    void set_column(int x, int y, uint64_t col) {
        if (x < 0 || x >= SIZE_X || y < 0 || y >= SIZE_Y) return;
        cols_[static_cast<size_t>(y) * SIZE_X + x] = col;
    }
    bool solid(int x, int y, int z) const {
        if (z < 0 || z >= SIZE_Z) return false;
        return (column(x, y) >> z) & 1u;
    }
    void set_block(int x, int y, int z, bool solid_now) {
        if (x < 0 || x >= SIZE_X || y < 0 || y >= SIZE_Y || z < 0 || z >= SIZE_Z) return;
        uint64_t& c = cols_[static_cast<size_t>(y) * SIZE_X + x];
        if (solid_now) c |= 1ull << z;
        else c &= ~(1ull << z);
    }

private:
    std::vector<uint64_t> cols_;
};
