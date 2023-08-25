#include "ppu.h"
#include "bus.h"
#include <stdlib.h>

#define LCD_WIDTH 160
#define LCD_HEIGHT 144

#define SCANLINE_CYCLES 456

static u32 PALETTE_COLORS[] = {
	[0] = 0xFFFFFFFF,
	[1] = 0xD3D3D3FF,
	[2] = 0x5A5A5AFF,
	[3] = 0x000000FF
};

#define STAT_IRQ_LYC (1 << 6)
#define STAT_IRQ_OAM_SCAN (1 << 5)
#define STAT_IRQ_V_BLANK (1 << 4)
#define STAT_IRQ_H_BLANK (1 << 3)
#define STAT_LYC (1 << 2)

static inline void ppu_set_mode(Ppu* self, PpuMode mode) {
	self->stat &= ~0b11;
	self->stat |= mode;
}

static inline void ppu_change_mode(Ppu* self, PpuMode mode) {
	u8 irq = 0;
	switch (mode) {
		case PPU_MODE_OAM_SCAN:
			irq |= self->stat & STAT_IRQ_OAM_SCAN;

			self->scanline_cycle = 0;
			self->bg_fetcher.discard = self->scx % 8;
			self->sprite_count = 0;
			self->sprite_ptr = 0;
			break;
		case PPU_MODE_DRAW:
			self->pixel_x = 0;

			self->bg_fetcher.fifo_size = 0;
			self->bg_fetcher.fifo_cons_ptr = 0;
			self->bg_fetcher.fifo_prod_ptr = 0;
			self->bg_fetcher.tile_x = 0;
			self->bg_fetcher.state = FETCH_STATE_TILE_NUM;
			self->bg_fetcher.line_has_window_px = self->ly >= self->wy && self->wx - 7 < LCD_WIDTH;
			break;
		case PPU_MODE_H_BLANK:
			irq |= self->stat & STAT_IRQ_H_BLANK;
			break;
		case PPU_MODE_V_BLANK:
			irq |= self->stat & STAT_IRQ_V_BLANK;

			cpu_request_irq(&self->bus->cpu, IRQ_VBLANK);
			self->scanline_cycle = 0;
			self->bg_fetcher.window_y = 0;
			self->frame_ready = true;
			break;
	}

	ppu_set_mode(self, mode);
	if (irq) {
		cpu_request_irq(&self->bus->cpu, IRQ_LCD_STAT);
	}
}

static inline PpuMode ppu_get_mode(Ppu* self) {
	return (PpuMode) (self->stat & 0b11);
}

void ppu_reset(Ppu* self) {
	ppu_set_mode(self, PPU_MODE_OAM_SCAN);
}

static int oam_comp_fn(const void* a, const void* b) {
	const OamEntry* entry1 = (const OamEntry*) a;
	const OamEntry* entry2 = (const OamEntry*) b;

	int res = (int) entry1->x - (int) entry2->x;
	if (res != 0) {
		return res;
	}
	int res2 = (int) entry1->oam_pos - (int) entry2->oam_pos;
	return res2;
}

static void ppu_do_oam_scan(Ppu* self) {
	if ((self->scanline_cycle & 1) != 0 && self->sprite_count < 10 && self->scanline_cycle * 2 + 3 < sizeof(self->oam)) {
		u8 offset = self->scanline_cycle * 2;

		u8 y = self->oam[offset];
		u8 x = self->oam[offset + 1];
		u8 tile = self->oam[offset + 2];
		u8 flags = self->oam[offset + 3];

		u8 height = self->lcdc & 1 << 2 ? 16 : 8;
		if (self->ly + 16 >= y && self->ly + 16 < y + height) {
			if (height == 16) {
				if (self->ly + 16 < y - 8) {
					tile |= 1;
				}
				else {
					tile &= 0xFE;
				}
			}
			self->sprites[self->sprite_count++] = (OamEntry) {
				.x = x,
				.y = y,
				.tile_num = tile,
				.flags = flags,
				.oam_pos = offset
			};
		}
	}

	if (++self->scanline_cycle == 80) {
		qsort(self->sprites, self->sprite_count, sizeof(OamEntry),
			  oam_comp_fn);

		ppu_change_mode(self, PPU_MODE_DRAW);
	}
}

static bool ppu_fetcher_get_pixel(Ppu* self, u32* pixel) {
	if (self->bg_fetcher.fifo_size) {
		Pixel px = self->bg_fetcher.fifo[self->bg_fetcher.fifo_cons_ptr];
		self->bg_fetcher.fifo_cons_ptr = (self->bg_fetcher.fifo_cons_ptr + 1) & 7;
		self->bg_fetcher.fifo_size -= 1;
		*pixel = px.bg_enable ? px.color : PALETTE_COLORS[self->bg_palette & 0b11];
		return true;
	}

	return false;
}

static void ppu_clock_bg_fetcher(Ppu* ppu) {
	PixelFetcher* self = &ppu->bg_fetcher;

	if (!self->cycle) {
		self->cycle = true;
		return;
	}
	self->cycle = false;

	if (self->state == FETCH_STATE_TILE_NUM) {
		u16 tilemap_addr;
		if ((ppu->lcdc & 1 << 3 && !self->inside_window) || (ppu->lcdc & 1 << 6 && self->inside_window)) {
			tilemap_addr = 0x9C00 - 0x8000;
		}
		else {
			tilemap_addr = 0x9800 - 0x8000;
		}

		u8 fetch_y;
		u8 fetch_x;
		if (self->inside_window) {
			fetch_y = self->window_y / 8;
			fetch_x = self->tile_x;
		}
		else {
			fetch_y = ((ppu->ly + ppu->scy) & 0xFF) / 8;
			fetch_x = (ppu->scx / 8 + self->tile_x) & 0x1F;
		}

		u16 off = 32 * fetch_y + fetch_x;
		off &= 0x3FF;

		self->tile_num = ppu->vram[tilemap_addr + off];
		self->state = FETCH_STATE_TILE_LOW;
	}
	else if (self->state == FETCH_STATE_TILE_LOW) {
		if (ppu->lcdc & 1 << 4) {
			self->tile_data_addr = 0;
		}
		else {
			self->tile_data_addr = 0x800;
		}

		u16 off;
		if (self->inside_window) {
			off = 2 * (self->window_y % 8);
		}
		else {
			off = 2 * ((ppu->ly + ppu->scy) % 8);
		}
		self->tile_data_off = off;

		if (self->tile_data_addr == 0x800) {
			self->tile_low = ppu->vram[self->tile_data_addr + (i8) self->tile_num * 16 + off];
		}
		else {
			self->tile_low = ppu->vram[self->tile_data_addr + self->tile_num * 16 + off];
		}
		self->state = FETCH_STATE_TILE_HIGH;
	}
	else if (self->state == FETCH_STATE_TILE_HIGH) {
		if (self->tile_data_addr == 0x800) {
			self->tile_high = ppu->vram[self->tile_data_addr + (i8) self->tile_num * 16 + self->tile_data_off + 1];
		}
		else {
			self->tile_high = ppu->vram[self->tile_data_addr + self->tile_num * 16 + self->tile_data_off + 1];
		}
		self->state = FETCH_STATE_PUSH;
	}
	else {
		if (self->fifo_size == 0) {
			for (u8 i = 0; i < 8; ++i) {
				if (self->discard) {
					self->discard -= 1;
					self->tile_low <<= 1;
					self->tile_high <<= 1;
					continue;
				}

				u8 color_id = (self->tile_low >> 7) | (self->tile_high >> 7) << 1;

				u8 bg_color_id = ppu->bg_palette >> (2 * color_id) & 0b11;

				self->tile_low <<= 1;
				self->tile_high <<= 1;
				self->fifo[self->fifo_prod_ptr] = (Pixel) {
					.color = PALETTE_COLORS[bg_color_id],
					.bg_enable = ppu->lcdc & 1
				};
				self->fifo_prod_ptr = (self->fifo_prod_ptr + 1) & 7;
				self->fifo_size += 1;
			}
			self->state = FETCH_STATE_TILE_NUM;
			self->tile_x += 1;
		}
		else {
			self->cycle = true;
		}
	}
}

static void ppu_clock_fetchers(Ppu* self) {
	ppu_clock_bg_fetcher(self);
}

static void ppu_do_draw(Ppu* self) {
	ppu_clock_fetchers(self);

	u32 pixel;
	if (ppu_fetcher_get_pixel(self, &pixel)) {
		self->texture[self->ly * LCD_WIDTH + self->pixel_x] = pixel;

		if (++self->pixel_x == LCD_WIDTH) {
			if (self->bg_fetcher.line_has_window_px) {
				self->bg_fetcher.window_y += 1;
			}
			ppu_change_mode(self, PPU_MODE_H_BLANK);
		}
	}

	self->scanline_cycle += 1;
}

static void ppu_do_h_blank(Ppu* self) {
	if (++self->scanline_cycle == SCANLINE_CYCLES) {
		self->ly += 1;

		if (self->ly == self->lyc) {
			self->stat |= STAT_LYC;
			if (self->stat & STAT_IRQ_LYC) {
				cpu_request_irq(&self->bus->cpu, IRQ_LCD_STAT);
			}
		}
		else {
			self->stat &= ~STAT_LYC;
		}

		if (self->ly >= LCD_HEIGHT) {
			ppu_change_mode(self, PPU_MODE_V_BLANK);
		}
		else {
			ppu_change_mode(self, PPU_MODE_OAM_SCAN);
		}
	}
}

static void ppu_do_v_blank(Ppu* self) {
	if (++self->scanline_cycle == SCANLINE_CYCLES) {
		self->ly += 1;

		if (self->ly == self->lyc) {
			self->stat |= STAT_LYC;
			if (self->stat & STAT_IRQ_LYC) {
				cpu_request_irq(&self->bus->cpu, IRQ_LCD_STAT);
			}
		}
		else {
			self->stat &= ~STAT_LYC;
		}

		if (self->ly == 154) {
			self->ly = 0;
			ppu_change_mode(self, PPU_MODE_OAM_SCAN);
		}
		else {
			self->scanline_cycle = 0;
		}
	}
}

void ppu_clock(Ppu* self) {
	if (!(self->lcdc & 1 << 7)) {
		return;
	}

	switch (ppu_get_mode(self)) {
		case PPU_MODE_OAM_SCAN:
			ppu_do_oam_scan(self);
			break;
		case PPU_MODE_DRAW:
			ppu_do_draw(self);
			break;
		case PPU_MODE_H_BLANK:
			ppu_do_h_blank(self);
			break;
		case PPU_MODE_V_BLANK:
			ppu_do_v_blank(self);
			break;
	}
}
