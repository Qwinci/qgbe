#include "timer.h"
#include "bus.h"

u8 timer_read(Timer* self, u16 addr) {
	if (addr == 0xFF04) {
		return self->div >> 8;
	}
	else if (addr == 0xFF05) {
		return self->tima;
	}
	else if (addr == 0xFF06) {
		return self->tma;
	}
	else {
		return self->tac;
	}
}

void timer_cycle(Timer* self) {
	self->div += 1;

	if (self->tac & 1 << 2) {
		if (self->cycles + 1 == self->divider) {
			if (self->tima == 0xFF) {
				self->tima = self->tma;
				cpu_request_irq(&self->bus->cpu, IRQ_TIMER);
			}
			else {
				self->tima += 1;
			}
			self->cycles = 0;
		}
		else {
			self->cycles += 1;
		}
	}
}

void timer_write(Timer* self, u16 addr, u8 value) {
	if (addr == 0xFF04) {
		self->div = 0;
	}
	else if (addr == 0xFF05) {
		self->tima = value;
	}
	else if (addr == 0xFF06) {
		self->tma = value;
	}
	else {
		self->tac = value;
		u8 divider = value & 0b11;
		if (divider == 0b00) {
			self->divider = 1024 / 4;
		}
		else if (divider == 0b01) {
			self->divider = 16 / 4;
		}
		else if (divider == 0b10) {
			self->divider = 64 / 4;
		}
		else {
			self->divider = 256 / 4;
		}
	}
}
