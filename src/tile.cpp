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

bool is_pos_empty(TileMap& map, f32 pos_x, f32 pos_y) {
    bool res = false;

    i32 tile_x = static_cast<i32>((pos_x - map.min_x) / map.tile_width);
    i32 tile_y = static_cast<i32>((pos_y - map.min_y) / map.tile_height);

    if (tile_x >= 0 && tile_x < k_tile_cols && tile_y >= 0 && tile_y < k_tile_rows) {
        u32 tile_value = map.tiles[tile_y * map.count_x + tile_x];
        res = tile_value == 0;
    }

    return res;
}

// Moves a position that has stepped off the edge of its map onto the
// neighbouring map. Assumes every map has the same dimensions and that a
// position never moves more than one map width/height in a single step.
WorldPos canonicalize(World& world, WorldPos pos) {
    TileMap& map = world.maps[0];
    f32 map_width = map.count_x * map.tile_width;
    f32 map_height = map.count_y * map.tile_height;

    if (pos.x < map.min_x) {
        pos.x += map_width;
        pos.map_x--;
    } else if (pos.x >= map.min_x + map_width) {
        pos.x -= map_width;
        pos.map_x++;
    }

    if (pos.y < map.min_y) {
        pos.y += map_height;
        pos.map_y--;
    } else if (pos.y >= map.min_y + map_height) {
        pos.y -= map_height;
        pos.map_y++;
    }

    return pos;
}

bool is_world_pos_empty(World& world, WorldPos pos) {
    bool res = false;

    WorldPos can = canonicalize(world, pos);
    if (is_valid_map(world, can.map_x, can.map_y)) {
        res = is_pos_empty(get_tile_map(world, can.map_x, can.map_y), can.x, can.y);
    }

    return res;
}

bool is_tile_wall(TileMap& map, u32 tile_x, u32 tile_y) {
    u32 tile_value = map.tiles[tile_y * map.count_x + tile_x];
    bool res = tile_value == 1;
    return res;
}

}  // namespace hh
