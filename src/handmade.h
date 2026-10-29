#ifndef HANDEMADE_H

struct GameOffscreenBuffer {
    void* memory;
    int width;
    int height;
    int pitch;
};

static void game_render(GameOffscreenBuffer& buffer, int green_offset, int blue_offset);

#define HANDEMADE_H
#endif
