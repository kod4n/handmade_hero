// Tests for src/entity.cpp: low/high entity slots, and try_move against
// walls and other entities. Built as part of tests/main.cpp; the includes are
// here so clangd can make sense of this file on its own.

#include "test.h"
#include "test_support.h"
#include "defer.h"
#include "entity.cpp"

using namespace hh;

// GameState is too big for the stack, so it lives on the heap. Zeroed, plus
// what game_render's init sets up: the world's scale and the reserved slot 0
// in both entity arrays.
static GameState* make_state() {
    GameState* state = static_cast<GameState*>(std::calloc(1, sizeof(GameState)));
    state->world.tile_side_in_meters = k_tile_side_in_meters;
    state->world.tile_side_in_pixels = k_tile_side_in_pixels;
    state->world.meters_to_pixels = k_tile_side_in_pixels / k_tile_side_in_meters;
    state->low_entity_count = 1;
    state->high_entity_count = 1;
    return state;
}

// A high entity at pos with footprint dim. Tests skip set_camera, so it's
// made high whatever its level. The handle stays valid while more entities
// are added: promoting appends, and only demoting moves high slots.
static Entity add_test_entity(GameState& state, const WorldPosition& pos, Vec2 dim, bool collides = true) {
    const u32 low_index = add_entity(state);
    LowEntity& low = state.low_entities[low_index];
    low.pos = pos;
    low.collision_dim = dim;
    low.collides = collides;
    make_entity_high(state, low_index);
    return get_entity(state, low_index);
}

// Promoting and demoting keep low.high_index and high.low_index pointing at
// each other, including after a swap-remove moves the last high entity.
TEST(entity_high_low_links_survive_swap_remove) {
    GameState* state = make_state();
    defer(std::free(state));
    const u32 a = add_test_entity(*state, {}, {}).low_index;
    const u32 b = add_test_entity(*state, {}, {}).low_index;
    const u32 c = add_test_entity(*state, {}, {}).low_index;
    CHECK_EQ(state->high_entity_count, 4);

    // Demoting the first moves the last (c) into its slot.
    make_entity_low(*state, a);
    CHECK_EQ(state->high_entity_count, 3);
    CHECK(!get_entity(*state, a).high);
    for (u32 low_index : {b, c}) {
        const Entity e = get_entity(*state, low_index);
        CHECK(e.high != nullptr);
        CHECK_EQ(e.high->low_index, low_index);
    }

    // Demoting the last needs no move; promoting again appends.
    make_entity_low(*state, b);
    make_entity_high(*state, a);
    CHECK_EQ(state->high_entity_count, 3);
    for (u32 low_index : {a, c}) {
        const Entity e = get_entity(*state, low_index);
        CHECK(e.high != nullptr);
        CHECK_EQ(e.high->low_index, low_index);
    }
    CHECK(!get_entity(*state, b).high);

    // Out-of-range and reserved indices give an empty handle.
    CHECK(!get_entity(*state, 0).low);
    CHECK(!get_entity(*state, state->low_entity_count).low);
}

// A 3x3 floor at tiles (10..12, 10..12) on level 0, ringed by wall entities,
// made high since tests skip set_camera.
static void fill_walled_room(GameState& state) {
    for (u32 y = 9; y <= 13; y++) {
        for (u32 x = 9; x <= 13; x++) {
            if (x == 9 || x == 13 || y == 9 || y == 13) {
                add_wall(state, x, y, 0);
                make_entity_high(state, state.low_entity_count - 1);
            }
        }
    }
}

// Zero size: the walls don't grow, so it collides exactly as a point.
static constexpr Vec2 k_point_dim = {};

TEST(try_move_stops_at_first_wall) {
    GameState* state = make_state();
    defer(std::free(state));
    World* world = &state->world;
    fill_walled_room(*state);
    const Entity point = add_test_entity(*state, {}, k_point_dim);
    const f32 side = world->tile_side_in_meters;
    const f32 mid = 0.5f * side;

    // Open floor: not blocked, the whole move is taken.
    const WorldPosition center = {.abs_tile = {.x = 11, .y = 11, .z = 0}, .tile_rel = {mid, mid}};
    MoveResult clear = try_move(*state, point, center, {0.3f, -0.2f});
    CHECK(!clear.blocked);
    CHECK_EQ(clear.t, 1.0f);
    CHECK_EQ(clear.new_pos.abs_tile.x, 11);
    CHECK_NEAR(clear.new_pos.tile_rel.x, mid + 0.3f, 1e-5f);
    CHECK_NEAR(clear.new_pos.tile_rel.y, mid - 0.2f, 1e-5f);

    // Into the right wall, 0.05 away with a 0.1 move: stops halfway, just
    // short of the wall, with the normal pointing back out of it.
    const WorldPosition by_right = {.abs_tile = {.x = 12, .y = 11, .z = 0}, .tile_rel = {side - 0.05f, mid}};
    MoveResult right = try_move(*state, point, by_right, {0.1f, 0.0f});
    CHECK(right.blocked);
    CHECK_NEAR(right.t, 0.5f, 1e-3f);
    CHECK_EQ(right.new_pos.abs_tile.x, 12);
    CHECK(right.new_pos.tile_rel.x < side);
    CHECK_NEAR(right.new_pos.tile_rel.x, side, 1e-4f);
    CHECK_EQ(right.wall_normal, (Vec2{-1.0f, 0.0f}));

    // Into the left wall: normal points right.
    const WorldPosition by_left = {.abs_tile = {.x = 10, .y = 11, .z = 0}, .tile_rel = {0.05f, mid}};
    MoveResult left = try_move(*state, point, by_left, {-0.1f, 0.0f});
    CHECK(left.blocked);
    CHECK_EQ(left.new_pos.abs_tile.x, 10);
    CHECK_EQ(left.wall_normal, (Vec2{1.0f, 0.0f}));

    // Down into the bottom wall: normal points up (world y is up).
    const WorldPosition by_bottom = {.abs_tile = {.x = 11, .y = 10, .z = 0}, .tile_rel = {mid, 0.05f}};
    MoveResult bottom = try_move(*state, point, by_bottom, {0.0f, -0.1f});
    CHECK(bottom.blocked);
    CHECK_EQ(bottom.new_pos.abs_tile.y, 10);
    CHECK_EQ(bottom.wall_normal, (Vec2{0.0f, 1.0f}));

    // Diagonally toward the top-right corner, nearer the right wall than the
    // top one but moving so the top face is crossed first (t 0.4 vs 0.5).
    const WorldPosition by_corner = {.abs_tile = {.x = 12, .y = 12, .z = 0}, .tile_rel = {side - 0.05f, side - 0.04f}};
    MoveResult corner = try_move(*state, point, by_corner, {0.1f, 0.1f});
    CHECK(corner.blocked);
    CHECK_NEAR(corner.t, 0.4f, 1e-3f);
    CHECK_EQ(corner.wall_normal, (Vec2{0.0f, -1.0f}));
}

TEST(try_move_does_not_tunnel_through_walls) {
    GameState* state = make_state();
    defer(std::free(state));
    World* world = &state->world;
    fill_walled_room(*state);
    const Entity point = add_test_entity(*state, {}, k_point_dim);
    // Nothing on the far side of the right wall, so only the path in
    // between is blocked.
    const f32 side = world->tile_side_in_meters;
    const f32 mid = 0.5f * side;

    const WorldPosition start = {.abs_tile = {.x = 12, .y = 11, .z = 0}, .tile_rel = {mid, mid}};
    MoveResult move = try_move(*state, point, start, {2.0f * side, 0.0f});
    CHECK(move.blocked);
    CHECK_NEAR(move.t, 0.25f, 1e-3f);
    CHECK_EQ(move.new_pos.abs_tile.x, 12);
    CHECK_EQ(move.wall_normal, (Vec2{-1.0f, 0.0f}));
}

TEST(try_move_only_blocks_moving_into_a_face) {
    GameState* state = make_state();
    defer(std::free(state));
    World* world = &state->world;
    fill_walled_room(*state);
    const Entity point = add_test_entity(*state, {}, k_point_dim);
    const f32 side = world->tile_side_in_meters;
    const f32 mid = 0.5f * side;

    // Resting exactly on the left wall's face: stepping away is free...
    const WorldPosition on_face = {.abs_tile = {.x = 10, .y = 11, .z = 0}, .tile_rel = {0.0f, mid}};
    MoveResult away = try_move(*state, point, on_face, {0.1f, 0.0f});
    CHECK(!away.blocked);
    CHECK_NEAR(away.new_pos.tile_rel.x, 0.1f, 1e-5f);

    // ...and stepping into it goes nowhere.
    MoveResult into = try_move(*state, point, on_face, {-0.1f, 0.0f});
    CHECK(into.blocked);
    CHECK_EQ(into.t, 0.0f);
    CHECK_EQ(into.new_pos.abs_tile.x, 10);
    CHECK_EQ(into.new_pos.tile_rel.x, 0.0f);

    // Moving along a wall, parallel to its face, isn't blocked.
    const WorldPosition by_wall = {.abs_tile = {.x = 10, .y = 11, .z = 0}, .tile_rel = {0.01f, mid}};
    MoveResult along = try_move(*state, point, by_wall, {0.0f, side});
    CHECK(!along.blocked);
    CHECK_EQ(along.new_pos.abs_tile.y, 12);

    // Stuck inside a wall: the faces only block from outside, so it can
    // walk back out onto the floor.
    const WorldPosition in_wall = {.abs_tile = {.x = 13, .y = 11, .z = 0}, .tile_rel = {mid, mid}};
    MoveResult out = try_move(*state, point, in_wall, {-side, 0.0f});
    CHECK(!out.blocked);
    CHECK_EQ(out.new_pos.abs_tile.x, 12);
}

TEST(try_move_keeps_entity_size_out_of_walls) {
    GameState* state = make_state();
    defer(std::free(state));
    World* world = &state->world;
    fill_walled_room(*state);
    const f32 side = world->tile_side_in_meters;
    const f32 mid = 0.5f * side;
    const Entity mover = add_test_entity(*state, {}, {0.5f, 0.5f});
    const f32 half = 0.25f;

    // From the room's center toward the right wall (the left face of tile
    // 13): the center stops half the width short of the face, not on it.
    // That's 2 * side - mid - half = 1.85 of the 2.0 move.
    const WorldPosition center = {.abs_tile = {.x = 11, .y = 11, .z = 0}, .tile_rel = {mid, mid}};
    MoveResult right = try_move(*state, mover, center, {2.0f, 0.0f});
    CHECK(right.blocked);
    CHECK_NEAR(right.t, 0.925f, 1e-3f);
    CHECK_EQ(right.new_pos.abs_tile.x, 12);
    CHECK_NEAR(right.new_pos.tile_rel.x, side - half, 1e-3f);
    CHECK_EQ(right.wall_normal, (Vec2{-1.0f, 0.0f}));

    // The center stays inside tile 12 the whole move, but the box's right
    // edge reaches into wall tile 13, so the search has to look past the
    // tiles the center passes through.
    const WorldPosition near_right = {.abs_tile = {.x = 12, .y = 11, .z = 0}, .tile_rel = {side - 0.3f, mid}};
    MoveResult edge = try_move(*state, mover, near_right, {0.1f, 0.0f});
    CHECK(edge.blocked);
    CHECK_NEAR(edge.t, 0.5f, 1e-3f);
    CHECK_NEAR(edge.new_pos.tile_rel.x, side - half, 1e-3f);

    // Same for y: the box's bottom edge hits the wall row at y = 9.
    const WorldPosition near_bottom = {.abs_tile = {.x = 11, .y = 10, .z = 0}, .tile_rel = {mid, 0.3f}};
    MoveResult down = try_move(*state, mover, near_bottom, {0.0f, -0.1f});
    CHECK(down.blocked);
    CHECK_NEAR(down.new_pos.tile_rel.y, half, 1e-3f);
    CHECK_EQ(down.wall_normal, (Vec2{0.0f, 1.0f}));
}

TEST(try_move_stops_at_entities) {
    GameState* state = make_state();
    defer(std::free(state));
    World* world = &state->world;
    fill_walled_room(*state);
    const f32 side = world->tile_side_in_meters;
    const f32 mid = 0.5f * side;

    // A 0.5 m entity in the middle of the room, and movers one tile to its
    // left heading right, past it toward the wall. Absolute x: obstacle
    // 16.1, start 14.7. The movers themselves sit at tile 0, far away.
    const WorldPosition middle = {.abs_tile = {.x = 11, .y = 11, .z = 0}, .tile_rel = {mid, mid}};
    const WorldPosition start = {.abs_tile = {.x = 10, .y = 11, .z = 0}, .tile_rel = {mid, mid}};
    const Entity obstacle = add_test_entity(*state, middle, {0.5f, 0.5f});
    const Entity point = add_test_entity(*state, {}, k_point_dim);
    const Entity mover = add_test_entity(*state, {}, {0.5f, 0.5f});

    // A point stops half the obstacle's width short of its center: at
    // 15.85, 1.15 of the 2.0 move. It's hit before the wall behind it.
    MoveResult hit = try_move(*state, point, start, {2.0f, 0.0f});
    CHECK(hit.blocked);
    CHECK_NEAR(hit.t, 0.575f, 1e-3f);
    CHECK_EQ(hit.new_pos.abs_tile.x, 11);
    CHECK_NEAR(hit.new_pos.tile_rel.x, 0.45f, 1e-3f);
    CHECK_EQ(hit.wall_normal, (Vec2{-1.0f, 0.0f}));

    // A mover with its own size stops half of both sizes short: at 15.6.
    MoveResult sized = try_move(*state, mover, start, {2.0f, 0.0f});
    CHECK(sized.blocked);
    CHECK_NEAR(sized.t, 0.45f, 1e-3f);
    CHECK_NEAR(sized.new_pos.tile_rel.x, 0.2f, 1e-3f);

    // Passing 1 m above it: further than half of both sizes (0.5), so it's
    // missed.
    const WorldPosition above = {.abs_tile = {.x = 10, .y = 12, .z = 0}, .tile_rel = {mid, 0.3f}};
    CHECK(!try_move(*state, mover, above, {1.0f, 0.0f}).blocked);

    // Overlapping it, as two players spawned on the same spot are: only
    // moving into a face blocks, so they can walk apart either way.
    CHECK(!try_move(*state, mover, middle, {0.1f, 0.0f}).blocked);
    CHECK(!try_move(*state, mover, middle, {-0.1f, 0.0f}).blocked);

    // A mover doesn't block itself, even moving from where it stands.
    CHECK(!try_move(*state, obstacle, middle, {0.1f, 0.0f}).blocked);

    // Either side not colliding lets it through.
    obstacle.low->collides = false;
    CHECK(!try_move(*state, mover, start, {1.0f, 0.0f}).blocked);
    obstacle.low->collides = true;
    mover.low->collides = false;
    CHECK(!try_move(*state, mover, start, {1.0f, 0.0f}).blocked);
    mover.low->collides = true;

    // An entity on another level isn't in the way.
    obstacle.low->pos.abs_tile.z = 1;
    CHECK(!try_move(*state, mover, start, {1.0f, 0.0f}).blocked);
    obstacle.low->pos.abs_tile.z = 0;

    // Only high entities collide: once demoted, it's out of the way.
    make_entity_low(*state, obstacle.low_index);
    CHECK(!try_move(*state, mover, start, {1.0f, 0.0f}).blocked);
}

// A wall entity fills its tile exactly, so it blocks where a wall tile would:
// a point heading right stops at the tile's left edge.
TEST(add_wall_blocks_like_a_wall_tile) {
    GameState* state = make_state();
    defer(std::free(state));
    World* world = &state->world;
    fill_walled_room(*state);
    const f32 side = world->tile_side_in_meters;
    const f32 mid = 0.5f * side;

    add_wall(*state, 12, 11, 0);
    const Entity wall = get_entity(*state, state->low_entity_count - 1);
    CHECK(wall.low->type == EntityType::Wall);
    CHECK(wall.low->collides);
    CHECK_EQ(wall.low->pos.abs_tile.x, 12);
    CHECK_EQ(wall.low->pos.tile_rel, (Vec2{mid, mid}));
    CHECK_EQ(wall.low->collision_dim, (Vec2{side, side}));
    make_entity_high(*state, wall.low_index);

    // From the middle of tile 10 (x 14.7) toward the wall entity's left face
    // at 16.8, 2.1 of the 3.0 move, before the wall tiles at 18.2.
    const Entity point = add_test_entity(*state, {}, k_point_dim);
    const WorldPosition start = {.abs_tile = {.x = 10, .y = 11, .z = 0}, .tile_rel = {mid, mid}};
    MoveResult move = try_move(*state, point, start, {3.0f, 0.0f});
    CHECK(move.blocked);
    CHECK_NEAR(move.t, 0.7f, 1e-3f);
    CHECK_EQ(move.new_pos.abs_tile.x, 11);
    CHECK_NEAR(move.new_pos.tile_rel.x, side, 1e-3f);
    CHECK_EQ(move.wall_normal, (Vec2{-1.0f, 0.0f}));
}
