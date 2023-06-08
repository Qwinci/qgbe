#include "ppu.h"
#include "bus.h"

#define LCD_WIDTH 160
#define LCD_HEIGHT 144

#define SCANLINE_CYCLES 456

void ppu_clock_bg_fetcher(PpuPixelFetcher* self) {
	if (self->cycle == 0) {
		u8 lcdc = self->ppu->lcdc;
		self->rendering_window = lcdc & 1 << 5 && self->ppu->ly <= self->ppu->wy && self->ppu->pixel_x >= self->ppu->wx - 7;

		u16 tilemap_addr;
		if ((self->rendering_window && lcdc & 1 << 6) || (!self->rendering_window && lcdc & 1 << 3)) {
			tilemap_addr = 0x9C00 - 0x8000;
		}
		else {
			tilemap_addr = 0x9800 - 0x8000;
		}

		u32 off;
		if (self->rendering_window) {
			off = 32 * (self->window_line / 8) + self->fetch_tile_x;
		}
		else {
			off = 32 * (((self->ppu->ly + self->ppu->scy) & 0xFF) / 8) + ((self->ppu->scx / 8 + self->fetch_tile_x) & 0x1F);
		}
		off &= 0x3FF;

		self->tile_num = self->ppu->vram[tilemap_addr + off];
	}
	else if (self->cycle == 2) {
		u32 off;
		if (self->rendering_window) {
			off = 2 * (self->window_line % 8);
		}
		else {
			off = 2 * ((self->ppu->ly + self->ppu->scy) % 8);
		}

		self->tile_data_area = self->ppu->lcdc & 1 << 4 ? 0x8000 - 0x8000 : 0x8800 - 0x8000;
		self->tile_data_off = off;

		if (self->tile_data_area == 0x800) {
			self->tile_low = self->ppu->vram[self->tile_data_area + (i8) self->tile_num * 16 + off];
		}
		else {
			self->tile_low = self->ppu->vram[self->tile_data_area + self->tile_num * 16 + off];
		}
	}
	else if (self->cycle == 4) {
		if (self->tile_data_area == 0x800) {
			self->tile_high = self->ppu->vram[self->tile_data_area + (i8) self->tile_num * 16 + self->tile_data_off + 1];
		}
		else {
			self->tile_high = self->ppu->vram[self->tile_data_area + self->tile_num * 16 + self->tile_data_off + 1];
		}

		if (self->first_on_line) {
			self->cycle = 0;
			self->first_on_line = false;
			return;
		}
	}
	else if (self->cycle == 6) {
		if (self->fifo_size == 0) {
			for (u8 i = 0; i < 8; ++i) {
				u8 color_id = (self->tile_low >> 7) | (self->tile_high >> 7) << 1;
				self->tile_low <<= 1;
				self->tile_high <<= 1;
				self->fifo <<= 4;
				self->fifo |= color_id;
			}
			self->fifo_size = 8;
			self->fetch_tile_x += 1;
			self->cycle = 0;
			return;
		}
		return;
	}

	self->cycle += 1;
}

#define NORMAL_HEIGHT_MASK (8 - 1)
#define TALL_HEIGHT_MASK (16 - 1)

void ppu_clock_sp_fetcher(PpuPixelFetcher* self) {
	if (self->cycle == 0) {
		bool tall = self->ppu->lcdc & 1 << 2;

		if (tall) {
			u8 tile_num_off = self->ppu->ly >= self->sprite->y + 16 + 8;
			self->tile_num = self->sprite->tile_num + tile_num_off;
		}
		else {
			self->tile_num = self->sprite->tile_num;
		}
		self->cycle += 2;
		return;
	}
	else if (self->cycle == 2) {
		u32 off;
		bool tall = self->ppu->lcdc & 1 << 2;
		u32 mask = tall ? TALL_HEIGHT_MASK : NORMAL_HEIGHT_MASK;
		// Vertically Mirrored
		if (self->sprite->flags & 1 << 6) {
			u8 start = tall ? 30 : 14;
			off = start - 2 * ((self->ppu->ly + self->sprite->y) & mask);
		}
		else {
			off = 2 * ((self->ppu->ly + self->sprite->y) & mask);
		}

		self->tile_data_area = 0x8000 - 0x8000;
		self->tile_data_off = off;

		self->tile_low = self->ppu->vram[self->tile_data_area + self->tile_num * 16 + off];
		self->cycle += 2;
	}
	else if (self->cycle == 4) {
		self->tile_high = self->ppu->vram[self->tile_data_area + self->tile_num * 16 + self->tile_data_off + 1];
		self->cycle += 2;
	}
	else if (self->cycle >= 6) {
		if (self->fifo_size == 0) {
			// Not Horizontally Mirrored
			if (!(self->sprite->flags & 1 << 5)) {
				for (u8 i = 0; i < 8; ++i) {
					u8 color_id = (self->tile_low >> 7) | (self->tile_high >> 7) << 1;
					self->tile_low <<= 1;
					self->tile_high <<= 1;
					self->fifo <<= 4;
					self->fifo |= color_id | ((self->sprite->flags >> 4 & 1) << 2) | ((self->sprite->flags >> 7) << 3);
				}
			}
			else {
				for (u8 i = 0; i < 8; ++i) {
					u8 color_id = (self->tile_low & 1) | (self->tile_high & 1) << 1;
					self->tile_low >>= 1;
					self->tile_high >>= 1;
					self->fifo <<= 4;
					self->fifo |= color_id | ((self->sprite->flags >> 4 & 1) << 2) | ((self->sprite->flags >> 7) << 3);
				}
			}
			self->fifo_size = 8;
			self->fetch_tile_x += 1;
			self->cycle = 0;
			return;
		}
	}
}

static u32 PALETTE_COLORS[] = {
	[0] = 0xFFFFFFFF,
	[1] = 0xD3D3D3FF,
	[2] = 0x5A5A5AFF,
	[3] = 0x000000FF
};

#define STAT_MODE_MASK (0b11)
#define STAT_MODE_H_BLANK 0
#define STAT_MODE_V_BLANK 1
#define STAT_MODE_OAM_SCAN 2
#define STAT_MODE_DRAW 3

#define STAT_IRQ_LYC (1 << 6)
#define STAT_IRQ_OAM_SCAN (1 << 5)
#define STAT_IRQ_V_BLANK (1 << 4)
#define STAT_IRQ_H_BLANK (1 << 3)

static inline void ppu_set_mode(Ppu* self, u8 mode) {
	self->stat &= ~STAT_MODE_MASK;
	self->stat |= mode;
}

static inline void ppu_inc_ly(Ppu* self) {
	self->ly += 1;
	if (self->ly == self->lyc) {
		self->stat |= 1 << 2;
		if (self->stat & STAT_IRQ_LYC) {
			cpu_request_irq(&self->bus->cpu, IRQ_LCD_STAT);
		}
	}
	else {
		self->stat &= ~(1 << 2);
	}
}

void ppu_clock(Ppu* self) {
	if (!(self->lcdc & 1 << 7)) {
		return;
	}

	if (self->ly >= LCD_HEIGHT) {
		if (self->scanline_cycle == 0 && self->ly == LCD_HEIGHT) {
			cpu_request_irq(&self->bus->cpu, IRQ_VBLANK);
			self->frame_ready = true;
			self->scanline_cycle += 1;
		}
		else if (self->scanline_cycle == SCANLINE_CYCLES) {
			ppu_inc_ly(self);
			if (self->ly == 154) {
				self->ly = 0;
			}
			self->scanline_cycle = 0;
		}
		else {
			self->scanline_cycle += 1;
		}
		return;
	}

	u8 fetch_tile_x = 0;
	bool inc_window = false;
	for (u8 i = 0; i < LCD_WIDTH; ++i) {
		bool rendering_window = self->lcdc & 1 << 5 && self->ly <= self->wy && i >= self->wx - 7;
		if (rendering_window) {
			inc_window = true;
		}

		u16 tilemap_addr;
		if ((rendering_window && self->lcdc & 1 << 6) || (!rendering_window && self->lcdc & 1 << 3)) {
			tilemap_addr = 0x9C00 - 0x8000;
		}
		else {
			tilemap_addr = 0x9800 - 0x8000;
		}

		u32 tilemap_off;
		if (rendering_window) {
			tilemap_off = 32 * (self->window_line / 8) + fetch_tile_x;
		}
		else {
			tilemap_off = 32 * (((self->ly + self->scy) & 0xFF) / 8) + ((self->scx / 8 + fetch_tile_x) & 0x1F);
		}
		tilemap_off &= 0x3FF;

		u8 tile_num = self->vram[tilemap_addr + tilemap_off];

		// --------------------------------------------------
		u32 off;
		if (rendering_window) {
			off = 2 * (self->window_line % 8);
		}
		else {
			off = 2 * ((self->ly + self->scy) % 8);
		}

		u16 tile_data_area = self->lcdc & 1 << 4 ? 0x8000 - 0x8000 : 0x8800 - 0x8000;

		u8 tile_low;
		u8 tile_high;
		if (tile_data_area == 0x800) {
			tile_low = self->vram[tile_data_area + (i8) tile_num * 16 + off];
			tile_high = self->vram[tile_data_area + (i8) tile_num * 16 + off + 1];
		}
		else {
			tile_low = self->vram[tile_data_area + tile_num * 16 + off];
			tile_high = self->vram[tile_data_area + tile_num * 16 + off + 1];
		}

		tile_low <<= i % 8;
		tile_high <<= i % 8;
		u8 color_id = (tile_low >> 7) | (tile_high >> 7) << 1;

		u8 palette_color = self->bg_palette >> (2 * color_id) & 0b11;
		u32 color = PALETTE_COLORS[palette_color];

		self->texture[self->ly * LCD_WIDTH + i] = color;

		if (i % 8 == 7) {
			fetch_tile_x += 1;
		}
	}

	self->scanline_cycle = 0;
	ppu_inc_ly(self);
	if (inc_window) {
		self->window_line += 1;
	}
	else {
		self->window_line = 0;
	}
}

/*void ppu_clock(Ppu* self) {
	// OAM (Mode 2) 80 T-cycles
	// Draw (Mode 3) 172-289 T-cycles
	// HBlank (Mode 0) 87-204 T-cycles

	if (!(self->lcdc & 1 << 7)) {
		return;
	}

	if (self->pixel_x == LCD_WIDTH) {
		if (!self->h_blanked) {
			// Mode HBlank
			self->stat &= ~0b11;

			if (self->stat & 1 << 3) {
				cpu_request_irq(&self->bus->cpu, IRQ_LCD_STAT);
			}
			self->h_blanked = true;
			self->bg_fetcher.fetch_tile_x = 0;
			self->sp_fetcher.fetch_tile_x = 0;
			self->bg_fetcher.cycle = 0;
			self->sp_fetcher.cycle = 0;
			self->sprite_count = 0;
			if (self->bg_fetcher.rendering_window) {
				self->bg_fetcher.window_line += 1;
			}
		}
		if (self->scanline_cycle < SCANLINE_CYCLES) {
			self->scanline_cycle += 1;
			return;
		}
		else {
			self->pixel_x = 0;
			self->h_blanked = false;
			self->scanline_cycle = 0;
			self->ly += 1;
			self->to_discard = self->scx % 8;

			if (self->ly == self->lyc) {
				self->stat |= 1 << 2;

				if (self->stat & 1 << 6) {
					cpu_request_irq(&self->bus->cpu, IRQ_LCD_STAT);
				}
			}
			else {
				self->stat &= ~(1 << 2);
			}
		}
	}

	if (self->ly == 153) {
		self->bg_fetcher.window_line = 0;
		self->bg_fetcher.first_on_line = true;
		self->ly = 0;
		// Mode OAM
		self->stat &= ~0b11;
		self->stat |= 2;
		if (self->stat & 1 << 5) {
			cpu_request_irq(&self->bus->cpu, IRQ_LCD_STAT);
		}
	}
	else if (self->ly >= LCD_HEIGHT) {
		if (self->ly == 144 && self->scanline_cycle == 0) {
			// Mode VBlank
			self->stat &= ~0b11;
			self->stat |= 1;

			if (self->stat & 1 << 4) {
				cpu_request_irq(&self->bus->cpu, IRQ_LCD_STAT);
			}
			cpu_request_irq(&self->bus->cpu, IRQ_VBLANK);
			self->frame_ready = true;
		}
		if (self->scanline_cycle == SCANLINE_CYCLES) {
			self->ly += 1;
			self->scanline_cycle = 0;

			if (self->ly == self->lyc) {
				self->stat |= 1 << 2;

				if (self->stat & 1 << 6) {
					cpu_request_irq(&self->bus->cpu, IRQ_LCD_STAT);
				}
			}
			else {
				self->stat &= ~(1 << 2);
			}
		}
		else {
			self->scanline_cycle += 1;
		}
		return;
	}

	// OAM
	if (self->scanline_cycle < 80) {
		if (self->skip_oam) {
			self->scanline_cycle += 1;
			self->skip_oam = false;
			return;
		}

		if (self->sprite_count == 10) {
			self->skip_oam = true;
			self->scanline_cycle += 1;
			return;
		}

		u8 y = self->oam[self->scanline_cycle * 2];
		u8 x = self->oam[self->scanline_cycle * 2 + 1];
		u8 tile_num = self->oam[self->scanline_cycle * 2 + 2];
		u8 flags = self->oam[self->scanline_cycle * 2 + 3];

		u8 height = self->lcdc & 1 << 2 ? 16 : 8;
		if (self->ly + 16 >= y && self->ly + 16 < y + height) {
			self->sprites[self->sprite_count++] = (OamEntry) {
				.y = y,
				.x = x,
				.tile_num = tile_num,
				.flags = flags
			};
		}

		self->skip_oam = true;
		self->scanline_cycle += 1;
		return;
	}

	self->stat &= ~0b11;
	self->stat |= 3;
	const OamEntry* sprite = NULL;
	if (self->lcdc & 1 << 1) {
		for (u8 i = 0; i < self->sprite_count; ++i) {
			const OamEntry* entry = &self->sprites[i];
			if (self->pixel_x + 8 >= entry->x && self->pixel_x < entry->x) {
				if (!sprite || entry->x < sprite->x) {
					sprite = entry;
				}
			}
		}

		if (sprite) {
			if (self->bg_fetcher.fifo_size == 0 && self->lcdc & 1) {
				ppu_clock_bg_fetcher(&self->bg_fetcher);
				self->scanline_cycle += 1;
				return;
			}
			self->sp_fetcher.sprite = sprite;

			if (self->sp_fetcher.fifo_size == 0) {
				ppu_clock_sp_fetcher(&self->sp_fetcher);
				self->scanline_cycle += 1;
				return;
			}
		}
	}

	if (!sprite) {
		if (!(self->lcdc & 1)) {
			u8 palette_color = self->bg_palette & 0b11;
			u32 color = PALETTE_COLORS[palette_color];
			self->texture[self->ly * LCD_WIDTH + self->pixel_x] = color;
			self->pixel_x += 1;
			self->scanline_cycle += 1;
			return;
		}

		ppu_clock_bg_fetcher(&self->bg_fetcher);

		if (self->bg_fetcher.fifo_size) {
			if (self->to_discard) {
				self->to_discard -= 1;
				self->scanline_cycle += 1;
				self->bg_fetcher.fifo <<= 4;
				self->bg_fetcher.fifo_size -= 1;
				return;
			}

			u8 color_id = self->bg_fetcher.fifo >> (32 - 4);
			self->bg_fetcher.fifo <<= 4;
			self->bg_fetcher.fifo_size -= 1;

			u8 palette_color = self->bg_palette >> (2 * color_id) & 0b11;
			u32 color = PALETTE_COLORS[palette_color];
			self->texture[self->ly * LCD_WIDTH + self->pixel_x] = color;
			self->pixel_x += 1;
		}
	}
	else {
		if ((self->bg_fetcher.fifo_size || !(self->lcdc & 1)) && self->sp_fetcher.fifo_size) {
			if (self->to_discard) {
				self->to_discard -= 1;
				self->scanline_cycle += 1;
				self->bg_fetcher.fifo <<= 4;
				self->sp_fetcher.fifo <<= 4;
				self->bg_fetcher.fifo_size -= 1;
				self->sp_fetcher.fifo_size -= 1;
				return;
			}

			u8 bg_color_id;
			if (self->lcdc & 1) {
				bg_color_id = self->bg_fetcher.fifo >> (32 - 4);
				self->bg_fetcher.fifo <<= 4;
				self->bg_fetcher.fifo_size -= 1;
			}
			else {
				bg_color_id = 0;
			}

			u8 sp_entry = self->sp_fetcher.fifo >> (32 - 4);

			self->sp_fetcher.fifo <<= 4;
			self->sp_fetcher.fifo_size -= 1;

			u8 bg_palette_color = self->bg_palette >> (2 * bg_color_id) & 0b11;
			u8 sp_palette = sp_entry & 1 << 2 ? self->ob_palette0 : self->ob_palette1;
			u8 sp_palette_color = sp_palette >> (2 * (sp_entry & 0b11)) & 0b11;
			bool use_bg = (sp_entry >> 3) || (sp_palette_color == 0);
			u32 color = use_bg ? PALETTE_COLORS[bg_palette_color] : PALETTE_COLORS[sp_palette_color];
			self->texture[self->ly * LCD_WIDTH + self->pixel_x] = color;
			self->pixel_x += 1;
		}
	}

	self->scanline_cycle += 1;
}*/
