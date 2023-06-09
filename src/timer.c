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

static u8 DIV_BIT_POS[] = {
	[0b00] = 9,
	[0b01] = 3,
	[0b10] = 5,
	[0b11] = 7
};

void timer_cycle(Timer* self) {
	self->div += 4;

	if (self->overflowed) {
		self->tima = self->tma;
		cpu_request_irq(&self->bus->cpu, IRQ_TIMER);
		self->overflowed = false;
	}

	u8 bit = self->div >> DIV_BIT_POS[self->tac & 0b11] & 0b1;
	u8 timer_enable = self->tac >> 2 & 1;
	u8 res = bit & timer_enable;

	if (self->last_res && !res) {
		if (self->tima == 0xFF) {
			self->tima = 0;
			self->overflowed = true;
		}
		else {
			self->tima += 1;
		}
	}

	self->last_res = res;
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
		//u8 old_tac = self->tac;
		self->tac = value;

		/*if (old_tac & 1 << 2) {
			bool increase;
			if (!(value & 1 << 2)) {
				increase = self->div >> DIV_BIT_POS[old_tac & 0b11] & 0b1;
			}
			else {
				increase = (self->div >> DIV_BIT_POS[old_tac & 0b11] & 0b1) && (self->div >> DIV_BIT_POS[value & 0b11] & 0b1) == 0;
			}

			if (increase) {
				if (self->tima == 0xFF) {
					self->tima = self->tma;
					cpu_request_irq(&self->bus->cpu, IRQ_TIMER);
				}
				else {
					self->tima += 1;
				}
				self->last_res = 0;
			}
		}*/
	}
}
