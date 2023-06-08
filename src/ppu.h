#pragma once
#include "types.h"

typedef struct {
	u8 y;
	u8 x;
	u8 tile_num;
	u8 flags;
} OamEntry;

typedef struct {
	struct Ppu* ppu;
	const OamEntry* sprite;
	u32 cycle;
	u32 fifo;
	u16 tile_data_area;
	u16 tile_data_off;
	u8 tile_num;
	u8 tile_low;
	u8 tile_high;
	u8 fetch_tile_x;
	bool rendering_window;
	u8 window_line;
	bool first_on_line;
	u8 fifo_size;
} PpuPixelFetcher;

typedef enum {
	PPU_MODE_OAM_SCAN,
	PPU_MODE_DRAW,
	PPU_MODE_H_BLANK,
	PPU_MODE_V_BLANK
} PpuMode;

typedef struct Ppu {
	struct Bus* bus;
	u32* texture;
	u32 scanline_cycle;
	u32 v_blank_scanline_cycle;
	PpuMode mode;
	OamEntry sprites[10];
	PpuPixelFetcher bg_fetcher;
	PpuPixelFetcher sp_fetcher;
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
	u8 to_discard;
	bool h_blanked;
	bool skip_oam;
	u8 sprite_count;
	bool no_sprite_update;

	u8 window_line;
} Ppu;

void ppu_clock(Ppu* self);
