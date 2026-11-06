#pragma once
// The local server's map: every voxel, with its colour.
//
// The physics' SolidMap keeps one bit per voxel, because nothing it asks needs
// a colour. A server needs more: it hands the map to every client that joins, as
// it stands NOW, with every block built since in the colour it was built in.
// So this is pyspades' MapData - a solid flag and a colour per voxel - with
// pyspades' rules for editing it (vxl.pyx build_point / destroy_point) and its
// floating-block check (vxl_c.cpp check_node), which every client repeats on
// its own and the server has to agree with.
//
// Coordinates are the protocol's: x and y 0..511, z 0..63 growing DOWNWARD,
// z = 63 the bottom (water) layer, 62 the lowest layer an edit may touch.
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

class SolidMap;

namespace host {

struct Cell { int x, y, z; };

class VoxelMap {
public:
    static constexpr int SX = 512, SY = 512, SZ = 64;
    // What a voxel with no colour of its own is written as: pyspades'
    // DEFAULT_COLOR, the brown every stock map's buried dirt is.
    static constexpr uint32_t kDefaultColor = 0xFF674028u;

    VoxelMap();

    static bool valid(int x, int y, int z) {
        return x >= 0 && x < SX && y >= 0 && y < SY && z >= 0 && z < SZ;
    }

    // A .vxl, uncompressed, as a map file holds it. False and `err` if it runs
    // short or a span points outside its column.
    bool load_vxl(const uint8_t* data, size_t len, std::string& err);
    // The map as it stands, in the same format: what a joining client is sent.
    std::vector<uint8_t> save_vxl() const;
    // The same, deflated - the MapChunk payload stream.
    std::vector<uint8_t> save_compressed() const;
    // Which voxels are solid, for the server's physics and its grenades.
    void to_solid_map(SolidMap& out) const;

    bool solid(int x, int y, int z) const {
        if (!valid(x, y, z)) return false;
        return (cols_[idx2(x, y)] >> z) & 1u;
    }
    uint64_t column(int x, int y) const {
        if (x < 0 || x >= SX || y < 0 || y >= SY) return 0;
        return cols_[idx2(x, y)];
    }
    uint32_t color(int x, int y, int z) const {
        return valid(x, y, z) ? colors_[idx3(x, y, z)] : 0u;
    }
    // Set or clear one voxel, no rules applied. The generator and the loader.
    void set(int x, int y, int z, uint32_t argb);
    void clear(int x, int y, int z);

    // pyspades get_z: the first solid voxel from `start` down, 0 if none.
    int get_z(int x, int y, int start = 0) const;
    bool has_neighbors(int x, int y, int z) const;

    // Place a block the way vxl.pyx build_point does: on the map, above the
    // bottom two layers, touching something. False if refused.
    bool build_point(int x, int y, int z, uint32_t argb);

    // Remove a block and whatever it was holding up: vxl.pyx destroy_point.
    // Every voxel removed - the block itself first - is appended to `removed`
    // (may be null). Returns how many.
    int destroy_point(int x, int y, int z, std::vector<Cell>* removed);

    // "Land" is a column whose layer 62 is solid - not open water. pyspades
    // get_random_point: a uniformly chosen land column in the box, or any
    // column in it if it has none.
    void random_point(int x1, int y1, int x2, int y2, std::mt19937& rng, int& x, int& y) const;
    int count_land(int x1, int y1, int x2, int y2) const;

private:
    static size_t idx2(int x, int y) { return static_cast<size_t>(y) * SX + x; }
    static size_t idx3(int x, int y, int z) {
        return (static_cast<size_t>(y) * SX + x) * SZ + static_cast<size_t>(z);
    }
    bool is_surface(int x, int y, int z) const;
    // Flood from (x,y,z) through solid voxels. Reaching z >= 62 means it is
    // held up and nothing happens; otherwise the whole component falls.
    int check_node(int x, int y, int z, std::vector<Cell>* removed);

    // check_node's working set, kept between fills so a destroy allocates
    // nothing: the stack, one bit per voxel of the world (idx3 order, 2MB,
    // made on the first fill), and the voxels this fill marked, which are
    // what gets unmarked after it rather than clearing all 2MB.
    std::vector<Cell> fill_stack_;
    std::vector<uint64_t> fill_seen_;
    std::vector<uint32_t> fill_marked_;

    std::vector<uint64_t> cols_;     // bit z set where solid
    std::vector<uint32_t> colors_;   // ARGB as the file stores it (B,G,R,A bytes)
};

}  // namespace host
