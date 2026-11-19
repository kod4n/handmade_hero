#pragma once
#include "tile.h"
#include <cassert>

namespace hh {

u32 map00[k_tile_rows][k_tile_cols] = {
    //|          |           |           |
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0},
    {1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1},
};
u32 map01[k_tile_rows][k_tile_cols] = {
    //|          |           |           |
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1},
};
u32 map10[k_tile_rows][k_tile_cols] = {
    //|          |           |           |
    {1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
};
u32 map11[k_tile_rows][k_tile_cols] = {
    //|          |           |           |
    {1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
};

bool is_valid_map(World& world, i32 map_x, i32 map_y) {
    bool res = map_x >= 0 && map_y >= 0 && static_cast<u32>(map_x) < world.map_count_x
               && static_cast<u32>(map_y) < world.map_count_y;
    return res;
}

// The map must exist; check with is_valid_map first if it might not.
TileMap& get_tile_map(World& world, i32 map_x, i32 map_y) {
    assert(is_valid_map(world, map_x, map_y));
    TileMap& res = world.maps[static_cast<u32>(map_y) * world.map_count_x + static_cast<u32>(map_x)];
    return res;
}

bool is_pos_empty(World& world, TileMap& map, f32 pos_x, f32 pos_y) {
    bool res = false;

    i32 tile_x = static_cast<i32>((pos_x - world.tile_min_x) / world.tile_width);
    i32 tile_y = static_cast<i32>((pos_y - world.tile_min_y) / world.tile_height);

    if (tile_x >= 0 && tile_x < k_tile_cols && tile_y >= 0 && tile_y < k_tile_rows) {
        u32 tile_value = map.tiles[tile_y * world.tile_count_x + tile_x];
        res = tile_value == 0;
    }

    return res;
}

// Moves a position that has stepped off the edge of its map onto the
// neighbouring map. Assumes every map has the same dimensions and that a
// position never moves more than one map width/height in a single step.
WorldPos canonicalize(World& world, WorldPos pos) {
    f32 map_width = world.tile_count_x * world.tile_width;
    f32 map_height = world.tile_count_y * world.tile_height;

    if (pos.x < world.tile_min_x) {
        pos.x += map_width;
        pos.map_x--;
    } else if (pos.x >= world.tile_min_x + map_width) {
        pos.x -= map_width;
        pos.map_x++;
    }

    if (pos.y < world.tile_min_y) {
        pos.y += map_height;
        pos.map_y--;
    } else if (pos.y >= world.tile_min_y + map_height) {
        pos.y -= map_height;
        pos.map_y++;
    }

    return pos;
}

bool is_world_pos_empty(World& world, WorldPos pos) {
    bool res = false;

    WorldPos can = canonicalize(world, pos);
    if (is_valid_map(world, can.map_x, can.map_y)) {
        res = is_pos_empty(world, get_tile_map(world, can.map_x, can.map_y), can.x, can.y);
    }

    return res;
}

bool is_tile_wall(World& world, TileMap& map, u32 tile_x, u32 tile_y) {
    u32 tile_value = map.tiles[tile_y * world.tile_count_x + tile_x];
    bool res = tile_value == 1;
    return res;
}

}  // namespace hh
