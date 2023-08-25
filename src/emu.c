#include "emu.h"
#include "mbc/mbc1.h"
#include "mbc/no_mbc.h"
#include "utils/fsize.h"
#include <stdio.h>
#include <stdlib.h>

Emulator emu_new() {
	Emulator emu = {};
	emu.bus.cpu.bus = &emu.bus;
	emu.bus.ppu.bus = &emu.bus;
	emu.bus.timer.bus = &emu.bus;
	emu.bus.joyp = 0xFF;
	ppu_reset(&emu.bus.ppu);
	return emu;
}

bool emu_load_boot_rom(Emulator* self, const char* path) {
	usize size = fsize(path);
	if (!size || size > 0x100) {
		return false;
	}

	FILE* file = fopen(path, "rb");
	fread(&self->bus.boot_rom, size, 1, file);
	fclose(file);

	self->bus.bootrom_mapped = true;

	return true;
}

bool emu_load_rom(Emulator* self, const char* path) {
	usize size = fsize(path);
	if (!size) {
		return false;
	}

	self->bus.cart.data = malloc(size);
	if (!self->bus.cart.data) {
		return false;
	}

	FILE* file = fopen(path, "rb");
	fread(self->bus.cart.data, size, 1, file);
	fclose(file);

	const CartHdr* hdr = (const CartHdr*) (self->bus.cart.data + 0x100);

	usize rom_banks = 2 << hdr->rom_size;
	usize rom_size = (1024 * 32) * (1 << hdr->rom_size);
	usize ram_size = 0;
	if (hdr->ram_size == 1) {
		ram_size = 1024;
	}
	else if (hdr->ram_size == 2) {
		ram_size = 1024 * 8;
	}
	else if (hdr->ram_size == 3) {
		ram_size = 1024 * 32;
	}
	else if (hdr->ram_size == 4) {
		ram_size = 1024 * 128;
	}
	else if (hdr->ram_size == 5) {
		ram_size = 1024 * 64;
	}

	if (ram_size) {
		self->bus.cart.ram = malloc(ram_size);
		if (!self->bus.cart.ram) {
			return false;
		}
	}

	self->bus.cart.rom_size = rom_size;
	self->bus.cart.ram_size = ram_size;
	self->bus.cart.num_rom_banks = rom_banks;
	self->bus.cart.hdr = hdr;

	Mapper* mapper = NULL;
	// ROM ONLY
	if (hdr->type == 0) {
		mapper = no_mbc_new(&self->bus.cart);
	}
	// MBC1/MBC1+RAM/MBC1+RAM+BATTERY
	else if (hdr->type == 1 || hdr->type == 2 || hdr->type == 3) {
		mapper = mbc1_new(&self->bus.cart);
	}
	else if (hdr->type == 0x13) {
		fprintf(stderr, "warning: using mbc1 for mbc3 rom\n");
		mapper = mbc1_new(&self->bus.cart);
	}
	// MBC3+TIMER+BATTERY/MBC3+TIMER+RAM+BATTERY/MBC3/MBC3+RAM/MBC3+RAM+BATTERY
	else {
		fprintf(stderr, "error: unsupported cartridge type %X\n", hdr->type);
		exit(1);
	}

	if (!mapper) {
		return false;
	}
	self->bus.cart.mapper = mapper;

	return true;
}

#include <SDL.h>

#define REAL_WIDTH 160
#define REAL_HEIGHT 144

#define TILE_VIEWER_WIDTH (128 * 4)
#define TILE_VIEWER_HEIGHT (128 * 4)

#define SPRITE_VIEWER_WIDTH (128 * 4)
#define SPRITE_VIEWER_HEIGHT (128 * 4)

void emu_run(Emulator* self) {
	// A: 01 F: B0 B: 00 C: 13 D: 00 E: D8 H: 01 L: 4D SP: FFFE PC: 00:0100 (00 C3 13 02)
	if (!self->bus.bootrom_mapped) {
		Cpu* cpu = &self->bus.cpu;
		cpu->regs[REG_A] = 1;
		cpu->regs[REG_F] = 0xB0;
		cpu->regs[REG_B] = 0;
		cpu->regs[REG_C] = 0x13;
		cpu->regs[REG_D] = 0;
		cpu->regs[REG_E] = 0xD8;
		cpu->regs[REG_H] = 1;
		cpu->regs[REG_L] = 0x4D;
		cpu->sp = 0xFFFE;
		cpu->pc = 0x100;
		self->bus.ppu.lcdc |= 1 << 7;
	}

	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_Window* window = SDL_CreateWindow(
		"qgbe",
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		800,
		600,
		SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);

	SDL_Renderer* renderer = SDL_CreateRenderer(window, 0,  SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

	float aspect = (float) REAL_HEIGHT / (float) REAL_WIDTH;

	SDL_Rect render_rect = {0, 0, (int) (aspect * 800.0f), (int) (aspect * 600.0f)};

	SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA8888, SDL_TEXTUREACCESS_STREAMING, REAL_WIDTH, REAL_HEIGHT);
	u32* backing = (u32*) calloc(1, REAL_WIDTH * REAL_HEIGHT * 4);
	for (usize i = 0; i < REAL_WIDTH; ++i) {
		backing[i] = 0x00FF00FF;
		backing[(REAL_HEIGHT - 1) * REAL_WIDTH + i] = 0x00FF00FF;
	}
	for (usize i = 0; i < REAL_HEIGHT; ++i) {
		backing[i * REAL_WIDTH] = 0x00FF00FF;
		backing[i * REAL_WIDTH + REAL_WIDTH - 1] = 0x00FF00FF;
	}

	u32* tile_data_backing = (u32*) calloc(1, TILE_VIEWER_WIDTH * TILE_VIEWER_HEIGHT * 4);
	u32* sprite_data_backing = (u32*) calloc(1, SPRITE_VIEWER_WIDTH * SPRITE_VIEWER_HEIGHT * 4);

	self->bus.ppu.texture = backing;

	SDL_UpdateTexture(tex, NULL, backing, REAL_WIDTH * 4);

	const u8* key_state = SDL_GetKeyboardState(NULL);

	SDL_Scancode key_down = SDL_SCANCODE_DOWN;
	SDL_Scancode key_up = SDL_SCANCODE_UP;
	SDL_Scancode key_left = SDL_SCANCODE_LEFT;
	SDL_Scancode key_right = SDL_SCANCODE_RIGHT;

	SDL_Scancode key_start = SDL_SCANCODE_RETURN;
	SDL_Scancode key_select = SDL_SCANCODE_BACKSPACE;
	SDL_Scancode key_b = SDL_SCANCODE_B;
	SDL_Scancode key_a = SDL_SCANCODE_A;

	SDL_AudioSpec spec = {
		.freq = 48000,
		.format = AUDIO_F32SYS,
		.channels = 2
	};

	f32 audio_buffer[1024] = {};
	usize audio_size = 0;
	SDL_AudioDeviceID audio_dev = SDL_OpenAudioDevice(NULL, false, &spec, &spec, SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
	if (!audio_dev) {
		fprintf(stderr, "failed to open audio device: %s\n", SDL_GetError());
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);
		SDL_Quit();
		return;
	}
	SDL_PauseAudioDevice(audio_dev, false);

	SDL_Window* tile_viewer_window = NULL;
	SDL_Renderer* tile_renderer = NULL;
	SDL_Texture* tile_view_tex = NULL;

	SDL_Window* sprite_viewer_window = NULL;
	SDL_Renderer* sprite_renderer = NULL;
	SDL_Texture* sprite_view_tex = NULL;

	Uint32 main_window_id = SDL_GetWindowID(window);
	Uint32 tile_window_id = 0;
	Uint32 sprite_window_id = 0;

	bool running = true;
	while (running) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_WINDOWEVENT) {
				if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
					if (event.window.windowID == main_window_id) {
						running = false;
					}
					else if (event.window.windowID == tile_window_id) {
						SDL_DestroyTexture(tile_view_tex);
						SDL_DestroyRenderer(tile_renderer);
						SDL_DestroyWindow(tile_viewer_window);
						tile_window_id = 0;
					}
					else if (event.window.windowID == sprite_window_id) {
						SDL_DestroyTexture(sprite_view_tex);
						SDL_DestroyRenderer(sprite_renderer);
						SDL_DestroyWindow(sprite_viewer_window);
						sprite_window_id = 0;
					}
				}
				else if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
					//SDL_Rect rect = {.w = event.window.data1, .h = event.window.data2};
					//SDL_RenderSetScale(renderer, (f32) event.window.data1 / 800.0f, (f32) event.window.data2 / 800.0f);
					SDL_RenderSetLogicalSize(renderer, 800, 600);
				}
			}
			else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_t && event.key.keysym.mod & KMOD_CTRL) {
				if (!tile_window_id) {
					tile_viewer_window = SDL_CreateWindow(
						"qgbe tile viewer",
						SDL_WINDOWPOS_UNDEFINED,
						SDL_WINDOWPOS_UNDEFINED,
						TILE_VIEWER_WIDTH,
						TILE_VIEWER_HEIGHT,
						0);
					tile_window_id = SDL_GetWindowID(tile_viewer_window);
					tile_renderer = SDL_CreateRenderer(tile_viewer_window, 0, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
					tile_view_tex = SDL_CreateTexture(
						tile_renderer,
						SDL_PIXELFORMAT_BGRA8888,
						SDL_TEXTUREACCESS_STREAMING,
						TILE_VIEWER_WIDTH,
						TILE_VIEWER_HEIGHT);
				}
			}
			else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_s && event.key.keysym.mod & KMOD_CTRL) {
				if (!sprite_window_id) {
					sprite_viewer_window = SDL_CreateWindow(
						"qgbe sprite viewer",
						SDL_WINDOWPOS_UNDEFINED,
						SDL_WINDOWPOS_UNDEFINED,
						SPRITE_VIEWER_WIDTH,
						SPRITE_VIEWER_HEIGHT,
						0);
					sprite_window_id = SDL_GetWindowID(sprite_viewer_window);
					sprite_renderer = SDL_CreateRenderer(sprite_viewer_window, 0, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
					sprite_view_tex = SDL_CreateTexture(
						sprite_renderer,
						SDL_PIXELFORMAT_BGRA8888,
						SDL_TEXTUREACCESS_STREAMING,
						SPRITE_VIEWER_WIDTH,
						SPRITE_VIEWER_HEIGHT);
				}
			}
		}

		u8* joyp = &self->bus.joyp;

		// Actions
		if (*joyp & 1 << 5) {
			if (key_state[key_start]) {
				*joyp &= ~(1 << 3);
			}
			else {
				*joyp |= 1 << 3;
			}

			if (key_state[key_select]) {
				*joyp &= ~(1 << 2);
			}
			else {
				*joyp |= 1 << 2;
			}

			if (key_state[key_b]) {
				*joyp &= ~(1 << 1);
			}
			else {
				*joyp |= 1 << 1;
			}

			if (key_state[key_a]) {
				*joyp &= ~(1 << 0);
			}
			else {
				*joyp |= 1 << 0;
			}
		}
		if (*joyp & 1 << 4) {
			if (key_state[key_down]) {
				*joyp &= ~(1 << 3);
			}
			else {
				*joyp |= 1 << 3;
			}

			if (key_state[key_up]) {
				*joyp &= ~(1 << 2);
			}
			else {
				*joyp |= 1 << 2;
			}

			if (key_state[key_left]) {
				*joyp &= ~(1 << 1);
			}
			else {
				*joyp |= 1 << 1;
			}

			if (key_state[key_right]) {
				*joyp &= ~(1 << 0);
			}
			else {
				*joyp |= 1 << 0;
			}
		}

		usize cycles = 0;
		while (!self->bus.ppu.frame_ready) {
			bus_cycle(&self->bus);
			if (cycles % 22 == 0) {
				apu_gen_sample(&self->bus.apu, audio_buffer + audio_size);
				audio_size += 2;
				if (audio_size == sizeof(audio_buffer) / sizeof(*audio_buffer)) {
					SDL_QueueAudio(audio_dev, audio_buffer, audio_size * sizeof(f32));
					audio_size = 0;
				}
			}
			cycles += 1;
		}

		SDL_UpdateTexture(tex, NULL, backing, REAL_WIDTH * 4);
		self->bus.ppu.frame_ready = false;
		SDL_RenderClear(renderer);
		SDL_RenderCopy(renderer, tex, NULL, &render_rect);
		SDL_RenderPresent(renderer);

		if (tile_window_id) {
			ppu_generate_tile_map(&self->bus.ppu, TILE_VIEWER_WIDTH, TILE_VIEWER_HEIGHT, tile_data_backing);
			SDL_UpdateTexture(tile_view_tex, NULL, tile_data_backing, TILE_VIEWER_WIDTH * 4);
			SDL_RenderClear(tile_renderer);
			SDL_RenderCopy(tile_renderer, tile_view_tex, NULL, NULL);
			SDL_RenderPresent(tile_renderer);
		}

		if (sprite_window_id) {
			ppu_generate_sprite_map(&self->bus.ppu, SPRITE_VIEWER_WIDTH, SPRITE_VIEWER_HEIGHT, sprite_data_backing);
			SDL_UpdateTexture(sprite_view_tex, NULL, sprite_data_backing, SPRITE_VIEWER_WIDTH * 4);
			SDL_RenderClear(sprite_renderer);
			SDL_RenderCopy(sprite_renderer, sprite_view_tex, NULL, NULL);
			SDL_RenderPresent(sprite_renderer);
		}
	}

	if (tile_window_id) {
		SDL_DestroyTexture(tile_view_tex);
		SDL_DestroyRenderer(tile_renderer);
		SDL_DestroyWindow(tile_viewer_window);
	}
	if (sprite_window_id) {
		SDL_DestroyTexture(sprite_view_tex);
		SDL_DestroyRenderer(sprite_renderer);
		SDL_DestroyWindow(sprite_viewer_window);
	}
	SDL_DestroyTexture(tex);
	free(backing);
	free(tile_data_backing);
	free(sprite_data_backing);
	SDL_CloseAudioDevice(audio_dev);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
}