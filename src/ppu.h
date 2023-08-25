#pragma once
#include "types.h"

typedef struct {
	u8 x;
	u8 y;
	u8 tile_num;
	u8 flags;
	u8 oam_pos;
} OamEntry;

typedef enum {
	PPU_MODE_OAM_SCAN = 2,
	PPU_MODE_DRAW = 3,
	PPU_MODE_H_BLANK = 0,
	PPU_MODE_V_BLANK = 1
} PpuMode;

typedef enum {
	FETCH_STATE_TILE_NUM,
	FETCH_STATE_TILE_LOW,
	FETCH_STATE_TILE_HIGH,
	FETCH_STATE_PUSH
} FetchState;

typedef struct {
	u32 color;
	bool bg_enable;
} Pixel;

typedef struct {
	Pixel fifo[8];
	FetchState state;
	u16 tile_data_addr;
	u8 tile_data_off;
	u8 tile_num;
	u8 tile_low;
	u8 tile_high;
	bool cycle;
	u8 fifo_size;
	u8 fifo_prod_ptr;
	u8 fifo_cons_ptr;
	bool inside_window;
	u8 window_y;
	u8 tile_x;
	u8 discard;
	bool line_has_window_px;
} PixelFetcher;

typedef struct Ppu {
	struct Bus* bus;
	u32* texture;
	PixelFetcher bg_fetcher;
	PixelFetcher sp_fetcher;
	u32 scanline_cycle;
	OamEntry sprites[10];
	u8 vram[1024 * 8];
	u8 oam[160];
	u8 lcdc;
	u8 ly;
	u8 lyc;
	u8 stat;
	u8 scy;
	u8 scx;
	u8 wy;
	u8 wx;
	u8 bg_palette;
	u8 ob_palette0;
	u8 ob_palette1;
	bool frame_ready;
	u8 pixel_x;
	u8 sprite_count;
	u8 sprite_ptr;
} Ppu;

void ppu_reset(Ppu* self);
void ppu_clock(Ppu* self);
void ppu_generate_tile_map(Ppu* self, u32 width, u32 height, u32* data);
void ppu_generate_sprite_map(Ppu* self, u32 width, u32 height, u32* data);
