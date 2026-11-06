#include "VoxelMap.h"

#include "SolidMap.h"

#include <zlib.h>

#include <algorithm>

namespace host {

VoxelMap::VoxelMap()
    : cols_(static_cast<size_t>(SX) * SY, 0),
      colors_(static_cast<size_t>(SX) * SY * SZ, 0) {}

void VoxelMap::set(int x, int y, int z, uint32_t argb) {
    if (!valid(x, y, z)) return;
    cols_[idx2(x, y)] |= (1ull << z);
    colors_[idx3(x, y, z)] = argb;
}

void VoxelMap::clear(int x, int y, int z) {
    if (!valid(x, y, z)) return;
    cols_[idx2(x, y)] &= ~(1ull << z);
    colors_[idx3(x, y, z)] = 0;
}

// --- the file format ---------------------------------------------------------
//
// pyspades' load_vxl, with the bounds checks it leaves to the file being good.
// A column is solid from the top down except where a span says air: each span
// names the z its top colours start at, so everything between the last span's
// end and that z is air, and the colours it lists are the visible faces.
bool VoxelMap::load_vxl(const uint8_t* v, size_t len, std::string& err) {
    std::fill(cols_.begin(), cols_.end(), ~0ull);
    std::fill(colors_.begin(), colors_.end(), kDefaultColor);
    const uint8_t* const end = v + len;
    for (int y = 0; y < SY; ++y) {
        for (int x = 0; x < SX; ++x) {
            int z = 0;
            uint64_t& col = cols_[idx2(x, y)];
            for (;;) {
                if (end - v < 4) { err = "the file ends inside a column"; return false; }
                const int chunks = v[0];
                const int top_start = v[1];
                const int top_end = v[2];   // inclusive
                if (top_start >= SZ || top_end >= SZ) { err = "a span runs past the bottom"; return false; }
                for (int i = z; i < top_start; ++i) col &= ~(1ull << i);
                const int len_top = top_end - top_start + 1;
                const uint8_t* color = v + 4;
                if (len_top > 0 && end - color < 4 * len_top) { err = "the file ends inside a span"; return false; }
                for (int zz = top_start; zz <= top_end; ++zz, color += 4)
                    colors_[idx3(x, y, zz)] = color[0] | (color[1] << 8) | (color[2] << 16) |
                                              (static_cast<uint32_t>(color[3]) << 24);
                if (chunks == 0) {
                    v += 4 * (std::max(len_top, 0) + 1);
                    break;
                }
                const int len_bottom = (chunks - 1) - std::max(len_top, 0);
                v += chunks * 4;
                if (end - v < 4) { err = "the file ends inside a column"; return false; }
                const int bottom_end = v[3];    // the next span's air start
                const int bottom_start = bottom_end - len_bottom;
                if (bottom_start < 0 || bottom_end > SZ) { err = "a span's colours run off its column"; return false; }
                for (int zz = bottom_start; zz < bottom_end; ++zz, color += 4)
                    colors_[idx3(x, y, zz)] = color[0] | (color[1] << 8) | (color[2] << 16) |
                                              (static_cast<uint32_t>(color[3]) << 24);
                z = bottom_end;
            }
            // The air span lists no colours for solid voxels it never names;
            // an air voxel has none at all.
            for (int zz = 0; zz < SZ; ++zz)
                if (!((col >> zz) & 1u)) colors_[idx3(x, y, zz)] = 0;
        }
    }
    return true;
}

bool VoxelMap::is_surface(int x, int y, int z) const {
    if (!solid(x, y, z)) return false;
    if (z == 0) return true;
    if (x > 0 && !solid(x - 1, y, z)) return true;
    if (x + 1 < SX && !solid(x + 1, y, z)) return true;
    if (y > 0 && !solid(x, y - 1, z)) return true;
    if (y + 1 < SY && !solid(x, y + 1, z)) return true;
    if (z > 0 && !solid(x, y, z - 1)) return true;
    if (z + 1 < SZ && !solid(x, y, z + 1)) return true;
    return false;
}

// stb's write_map, as vxl_c.cpp carries it: the format every client reads.
std::vector<uint8_t> VoxelMap::save_vxl() const {
    std::vector<uint8_t> out;
    out.reserve(4u << 20);
    auto put_color = [&](int x, int y, int z) {
        uint32_t c = colors_[idx3(x, y, z)];
        if (c == 0) c = kDefaultColor;
        out.push_back(static_cast<uint8_t>(c));
        out.push_back(static_cast<uint8_t>(c >> 8));
        out.push_back(static_cast<uint8_t>(c >> 16));
        out.push_back(static_cast<uint8_t>(c >> 24));
    };
    for (int j = 0; j < SY; ++j) {
        for (int i = 0; i < SX; ++i) {
            int k = 0;
            while (k < SZ) {
                const int air_start = k;
                while (k < SZ && !solid(i, j, k)) ++k;
                const int top_start = k;
                while (k < SZ && is_surface(i, j, k)) ++k;
                const int top_end = k;
                while (k < SZ && solid(i, j, k) && !is_surface(i, j, k)) ++k;
                const int bottom_start = k;
                int z = k;
                while (z < SZ && is_surface(i, j, z)) ++z;
                if (z != SZ) {
                    while (k < SZ && is_surface(i, j, k)) ++k;
                }
                const int bottom_end = k;
                const int top_len = top_end - top_start;
                const int bottom_len = bottom_end - bottom_start;
                out.push_back(static_cast<uint8_t>(k == SZ ? 0 : top_len + bottom_len + 1));
                out.push_back(static_cast<uint8_t>(top_start));
                out.push_back(static_cast<uint8_t>(top_end - 1));
                out.push_back(static_cast<uint8_t>(air_start));
                for (int t = 0; t < top_len; ++t) put_color(i, j, top_start + t);
                for (int t = 0; t < bottom_len; ++t) put_color(i, j, bottom_start + t);
            }
        }
    }
    return out;
}

std::vector<uint8_t> VoxelMap::save_compressed() const {
    const std::vector<uint8_t> raw = save_vxl();
    uLongf cap = compressBound(static_cast<uLong>(raw.size()));
    std::vector<uint8_t> out(cap);
    // Level 6 is zlib's default and about a tenth of a second for a full map.
    // The map goes over loopback, so anything slower buys nothing.
    if (compress2(out.data(), &cap, raw.data(), static_cast<uLong>(raw.size()), 6) != Z_OK)
        return {};
    out.resize(cap);
    return out;
}

void VoxelMap::to_solid_map(SolidMap& out) const {
    for (int y = 0; y < SY; ++y)
        for (int x = 0; x < SX; ++x) out.set_column(x, y, cols_[idx2(x, y)]);
}

// --- queries -----------------------------------------------------------------

int VoxelMap::get_z(int x, int y, int start) const {
    for (int z = std::max(start, 0); z < SZ; ++z)
        if (solid(x, y, z)) return z;
    return 0;
}

bool VoxelMap::has_neighbors(int x, int y, int z) const {
    return solid(x + 1, y, z) || solid(x - 1, y, z) || solid(x, y + 1, z) ||
           solid(x, y - 1, z) || solid(x, y, z + 1) || solid(x, y, z - 1);
}

int VoxelMap::count_land(int x1, int y1, int x2, int y2) const {
    int land = 0;
    for (int x = x1; x < x2; ++x)
        for (int y = y1; y < y2; ++y)
            if (solid(x, y, 62)) ++land;
    return land;
}

void VoxelMap::random_point(int x1, int y1, int x2, int y2, std::mt19937& rng, int& x,
                            int& y) const {
    auto lim = [](int v) { return std::min(std::max(v, 0), 511); };
    x1 = lim(x1); y1 = lim(y1); x2 = lim(x2); y2 = lim(y2);
    std::vector<std::pair<int, int>> land;
    for (int i = x1; i < x2; ++i)
        for (int j = y1; j < y2; ++j)
            if (solid(i, j, 62)) land.emplace_back(i, j);
    if (land.empty()) {
        x = x1 + static_cast<int>(rng() % static_cast<unsigned>(std::max(x2 - x1, 1)));
        y = y1 + static_cast<int>(rng() % static_cast<unsigned>(std::max(y2 - y1, 1)));
        return;
    }
    const auto& p = land[rng() % land.size()];
    x = p.first;
    y = p.second;
}

// --- edits -------------------------------------------------------------------

bool VoxelMap::build_point(int x, int y, int z, uint32_t argb) {
    if (!valid(x, y, z)) return false;
    if (!has_neighbors(x, y, z) || z >= 62) return false;
    set(x, y, z, argb);
    return true;
}

int VoxelMap::destroy_point(int x, int y, int z, std::vector<Cell>* removed) {
    if (!solid(x, y, z) || z >= 62) return 0;
    clear(x, y, z);
    if (removed) removed->push_back({x, y, z});
    int count = 1;
    // vxl.pyx get_neighbors' order, which decides nothing about the result
    // but keeps the search the same shape as the server it copies.
    const int n[6][3] = {{0, 0, -1}, {0, -1, 0}, {0, 1, 0}, {-1, 0, 0}, {1, 0, 0}, {0, 0, 1}};
    for (const auto& d : n) {
        const int nx = x + d[0], ny = y + d[1], nz = z + d[2];
        if (nz < 62 && solid(nx, ny, nz)) count += check_node(nx, ny, nz, removed);
    }
    return count;
}

int VoxelMap::check_node(int x, int y, int z, std::vector<Cell>* removed) {
    // Depth first, and the last neighbour pushed is the one BELOW, so the
    // search heads for the ground first: a block still attached to the
    // terrain is found to be so in a few dozen steps, not by flooding the
    // hill it is part of.
    if (fill_seen_.empty()) fill_seen_.assign(static_cast<size_t>(SX) * SY * SZ / 64, 0);
    std::vector<Cell>& stack = fill_stack_;
    std::vector<uint32_t>& marked = fill_marked_;
    stack.clear();
    marked.clear();
    // Unmarks what this fill marked, on every way out.
    struct Unmark {
        std::vector<uint64_t>& seen;
        std::vector<uint32_t>& marked;
        ~Unmark() {
            for (uint32_t k : marked) seen[k >> 6] &= ~(uint64_t{1} << (k & 63));
        }
    } unmark{fill_seen_, marked};

    stack.push_back({x, y, z});
    while (!stack.empty()) {
        const Cell c = stack.back();
        stack.pop_back();
        if (c.z >= 62) return 0;
        const uint32_t k = static_cast<uint32_t>(idx3(c.x, c.y, c.z));
        uint64_t& word = fill_seen_[k >> 6];
        const uint64_t bit = uint64_t{1} << (k & 63);
        if (word & bit) continue;
        word |= bit;
        marked.push_back(k);
        const int n[6][3] = {{0, 0, -1}, {0, -1, 0}, {0, 1, 0}, {-1, 0, 0}, {1, 0, 0}, {0, 0, 1}};
        for (const auto& d : n) {
            const int nx = c.x + d[0], ny = c.y + d[1], nz = c.z + d[2];
            if (solid(nx, ny, nz)) stack.push_back({nx, ny, nz});
        }
    }
    for (uint32_t k : marked) {
        const int zz = static_cast<int>(k % SZ);
        const int xy = static_cast<int>(k / SZ);
        const int xx = xy % SX, yy = xy / SX;
        clear(xx, yy, zz);
        if (removed) removed->push_back({xx, yy, zz});
    }
    return static_cast<int>(marked.size());
}

}  // namespace host
