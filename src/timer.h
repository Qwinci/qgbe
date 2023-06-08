#pragma once
#include "types.h"

typedef struct {
	struct Bus* bus;
	u16 divider;
	u16 cycles;

	u16 div;
	u8 tima;
	u8 tma;
	u8 tac;
} Timer;

void timer_write(Timer* self, u16 addr, u8 value);
u8 timer_read(Timer* self, u16 addr);
void timer_cycle(Timer* self);
