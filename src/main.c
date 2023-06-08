#include "emu.h"
#include <stdio.h>

int main() {
	Emulator emu = emu_new();
	/*if (emu_load_boot_rom(&emu, "../roms/DMG_ROM.bin")) {
		//puts("boot rom loaded");
	}
	else {
		fputs("failed to load boot rom", stderr);
		return 1;
	}*/

	const char* rom = "../roms/tests/dmg-acid2/dmg-acid2.gb";
	// 0xFD: bits_bank2, bits_mode
	// failed: ramg, rom_1Mb, rom_2Mb, rom_4Mb, rom_8Mb, rom_16Mb, rom_512kb
	// stuck: ram_256kb
	if (emu_load_rom(&emu, rom)) {
		//puts("rom loaded");
	}
	else {
		fputs("failed to load rom\n", stderr);
		return 1;
	}

	emu_run(&emu);
}