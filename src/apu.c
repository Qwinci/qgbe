#include "apu.h"

void apu_clock_channels(Apu* self) {
	// Trigger channel 1
	if (self->nr14 & 1 << 7) {
		self->nr14 &= ~(1 << 7);
		self->nr52 |= 1 << 0;
		u16 freq = (2048 - (self->nr13 | ((self->nr14 & 0b111) << 8)) * 4);
		self->channel1_vol = self->nr12 >> 4;
		self->channel1_timer = freq;
		self->channel1_len_timer = self->nr11 & 0b111111;
		self->channel1_sweep_timer = self->nr10 & 0b111;
		self->channel1_sweep_tick = 0;
	}

	// Trigger channel 2
	if (self->nr24 & 1 << 7) {
		self->nr24 &= ~(1 << 7);
		self->nr52 |= 1 << 1;
		u16 freq = (2048 - (self->nr23 | ((self->nr24 & 0b111) << 8))) * 4;
		self->channel2_vol = self->nr22 >> 4;
		self->channel2_timer = freq;
		self->channel2_len_timer = self->nr21 & 0b111111;
	}

	// Trigger channel 3
	if (self->nr34 & 1 << 7) {
		self->nr34 &= ~(1 << 7);
		self->nr52 |= 1 << 2;
		u16 freq = (2048 - (self->nr33 | ((self->nr34 & 0b111) << 8))) * 4;
		self->channel3_timer = freq;
		self->channel3_len_timer = self->nr31;
		self->channel3_sample_index = 1;
	}

	if (self->nr52 & 1 << 0) {
		if (self->channel1_timer == 0) {
			self->channel1_cur_duty = (self->channel1_cur_duty + 1) % 8;

			u16 freq = (2048 - (self->nr13 | ((self->nr14 & 0b111) << 8))) * 4;
			self->channel1_timer = freq;
		}
		else {
			self->channel1_timer -= 1;
		}
	}

	if (self->nr52 & 1 << 1) {
		if (self->channel2_timer == 0) {
			self->channel2_cur_duty = (self->channel2_cur_duty + 1) % 8;

			u16 freq = (2048 - (self->nr23 | ((self->nr24 & 0b111) << 8))) * 4;
			self->channel2_timer = freq;
		}
		else {
			self->channel2_timer -= 1;
		}
	}

	if (self->nr52 & 1 << 2) {
		if (self->channel3_timer == 0) {
			if (self->channel3_cur_duty == 0) {
				self->channel3_sample = self->wave_pattern[self->channel3_sample_index / 2] >> (self->channel3_sample_index % 2 * 4);
				self->channel3_sample &= 0b1111;
				self->channel3_sample_index = (self->channel3_sample_index + 1) % 32;
				self->channel3_cur_duty = 4;
			}
			else {
				self->channel3_cur_duty -= 1;
			}

			u16 freq = (2048 - (self->nr33 | ((self->nr34 & 0b111) << 8))) * 4;
			self->channel3_timer = freq;
		}
		else {
			self->channel3_timer -= 1;
		}
	}
}

void apu_clock(Apu* self) {
	if (!(self->nr52 & 1 << 7)) {
		return;
	}

	// CH1 freq sweep
	if (self->clock % 4 == 0) {
		if (self->channel1_sweep_timer == 0) {
			u8 slope = self->nr10 & 0b111;
			u16 wavelength = self->nr13 | (self->nr14 & 0b111) << 8;
			u16 change = (wavelength / 2) * slope ? (self->channel1_sweep_tick / slope) : 0;
			if (self->nr10 & 1 << 3) {
				wavelength -= change;
			}
			else {
				if (wavelength + change > 0x7FF) {
					self->nr52 &= ~(1 << 0);
				}
				else {
					wavelength += change;
				}
			}
			wavelength &= 0x7FF;

			self->nr13 = wavelength;
			self->nr14 &= ~0b111;
			self->nr14 |= wavelength >> 8;

			self->channel1_sweep_timer = self->nr10 & 0b111;
			self->channel1_sweep_tick += 1;
		}
		else {
			self->channel1_sweep_timer -= 1;
		}
	}
	// Sound length
	if (self->clock % 2 == 0) {
		bool channel1_length_enable = self->nr14 & 1 << 6;
		if (self->nr52 & 1 << 0 && channel1_length_enable) {
			self->channel1_len_timer += 1;
			if (self->channel1_len_timer == 64) {
				self->nr52 &= ~(1 << 0);
			}
		}

		bool channel2_length_enable = self->nr24 & 1 << 6;
		if (self->nr52 & 1 << 1 && channel2_length_enable) {
			self->channel2_len_timer += 1;
			if (self->channel2_len_timer == 64) {
				self->nr52 &= ~(1 << 1);
			}
		}

		bool channel3_length_enable = self->nr34 & 1 << 6;
		if (self->nr52 & 1 << 2 && channel3_length_enable) {
			self->channel3_len_timer += 1;
			if (self->channel3_len_timer == 64) {
				self->nr52 &= ~(1 << 2);
			}
		}
	}
	// Envelope Sweep
	if (self->clock % 8 == 0) {
		u8 channel1_sweep_pace = self->nr12 & 0b111;
		if (channel1_sweep_pace && self->clock % channel1_sweep_pace == 0) {
			if (self->nr12 & 1 << 3 && self->channel1_vol < 0xF) {
				self->channel1_vol += 1;
			}
			else if (!(self->nr12 & 1 << 3) && self->channel1_vol > 0) {
				self->channel1_vol -= 1;
			}
		}

		u8 channel2_sweep_pace = self->nr22 & 0b111;
		if (channel2_sweep_pace && self->clock % channel2_sweep_pace == 0) {
			if (self->nr22 & 1 << 3 && self->channel2_vol < 0xF) {
				self->channel2_vol += 1;
			}
			else if (!(self->nr22 & 1 << 3) && self->channel2_vol > 0) {
				self->channel2_vol -= 1;
			}
		}
	}

	self->clock += 1;
}

static bool DUTY_TABLE[][8] = {
	[0] = {true},
	[1] = {true, true},
	[2] = {true, true, true, true},
	[3] = {true, true, true, true, true, true}
};

static u8 CH3_SHIFT_TABLE[] = {
	[0] = 0,
	[1] = 0,
	[2] = 1,
	[3] = 2
};

void apu_gen_sample(Apu* self, f32 out[2]) {
	u8 num_channels = 0;
	f32 left = 0;
	f32 right = 0;

	if (self->nr52 & 1 << 0) {
		bool channel1_duty = DUTY_TABLE[self->nr11 >> 6][self->channel1_cur_duty];
		u8 volume = self->channel1_vol;
		f32 amp = 1.0f / 8.0f * (f32) volume;
		if (channel1_duty) {
			if (self->nr51 & 1 << 4) {
				left += amp;
			}
			if (self->nr51 & 1 << 0) {
				right += amp;
			}
		}
		num_channels += 1;
	}

	if (self->nr52 & 1 << 1) {
		bool channel2_duty = DUTY_TABLE[self->nr21 >> 6][self->channel2_cur_duty];
		u8 volume = self->channel2_vol;
		f32 amp = 1.0f / 8.0f * (f32) volume;
		if (channel2_duty) {
			if (self->nr51 & 1 << 5) {
				left += amp;
			}
			if (self->nr51 & 1 << 1) {
				right += amp;
			}
		}
		num_channels += 1;
	}

	/*if (self->nr30 & 1 << 7 && self->nr52 & 1 << 2) {
		u8 volume = self->nr32 >> 5 & 0b11;
		u8 channel3_duty = self->channel3_sample >> CH3_SHIFT_TABLE[volume];
		if (channel3_duty >> self->channel3_cur_duty & 1) {
			if (self->nr51 & 1 << 6) {
				left += 1.0f;
			}
			if (self->nr51 & 1 << 2) {
				right += 1.0f;
			}
		}
		num_channels += 1;
	}*/

	if (num_channels) {
		u8 left_vol = (self->nr50 >> 4 & 0b111) + 1;
		u8 right_vol = (self->nr50 & 0b111) + 1;
		f32 left_amp = 0.8f / 8.0f * (f32) left_vol;
		f32 right_amp = 0.8f / 8.0f * (f32) right_vol;

		left /= (f32) num_channels;
		right /= (f32) num_channels;
		left *= left_amp;
		right *= right_amp;
	}

	out[0] = left;
	out[1] = right;
}
