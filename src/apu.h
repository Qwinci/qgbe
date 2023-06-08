#pragma once
#include "types.h"

typedef struct {
	u8 clock;
	u8 nr10;
	u8 nr11;
	u8 nr12;
	u8 nr13;
	u8 nr14;
	u8 nr21;
	u8 nr22;
	u8 nr23;
	u8 nr24;
	u8 nr30;
	u8 nr31;
	u8 nr32;
	u8 nr33;
	u8 nr34;
	u8 nr41;
	u8 nr42;
	u8 nr43;
	u8 nr44;
	u8 nr50;
	u8 nr51;
	u8 nr52;
	u8 wave_pattern[16];

	u16 channel1_timer;
	u8 channel1_sweep_timer;
	u8 channel1_sweep_tick;
	u8 channel1_cur_duty;
	u8 channel1_vol;
	u8 channel1_len_timer;

	u16 channel2_timer;
	u8 channel2_cur_duty;
	u8 channel2_vol;
	u8 channel2_len_timer;

	u16 channel3_timer;
	u8 channel3_cur_duty;
	u8 channel3_len_timer;
	u8 channel3_sample;
	u8 channel3_sample_index;
} Apu;

void apu_clock(Apu* self);
void apu_clock_channels(Apu* self);
void apu_gen_sample(Apu* self, f32 out[2]);
