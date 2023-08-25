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
	//const char* rom = "../roms/gb-test-roms/instr_timing/instr_timing.gb";
	//const char* rom = "../roms/tests/mooneye-test-suite/acceptance/jp_timing.gb";
	//const char* rom = "../roms/gb-test-roms/dmg_sound/rom_singles/04-sweep.gb";
	//const char* rom = "../roms/gb-test-roms/halt_bug.gb";

	if (emu_load_rom(&emu, rom)) {
		//puts("rom loaded");
	}
	else {
		fputs("failed to load rom\n", stderr);
		return 1;
	}

	emu_run(&emu);
}