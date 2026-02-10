/*
* SACD Decoder plugin
* Copyright (c) 2011-2023 Maxim V.Anisiutkin <maxim.anisiutkin@gmail.com>
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 2.1 of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with FFmpeg; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#pragma once

#include "dsdpcm_converter.h"
#include "dsdpcm_decoder.h"
#include <cstdint>
#include <vector>

void log_printf(const char* fmt, ...);

template<typename real_t>
class dsdpcm_slot_t {
public:
	dsdpcm_converter_t<real_t>* codec;
	
	std::vector<uint8_t> inp_data;
	std::vector<real_t>  out_data;

 	dsdpcm_slot_t() : codec(nullptr) {
	}
	dsdpcm_slot_t(const dsdpcm_slot_t<real_t>& slot) {
		codec = slot.codec;
		inp_data = slot.inp_data;
		out_data = slot.out_data;
	}
	dsdpcm_slot_t(dsdpcm_slot_t<real_t>&& slot) {
		codec = std::move(slot.codec);
		inp_data = std::move(slot.inp_data);
		out_data = std::move(slot.out_data);
	}
	dsdpcm_slot_t& operator=(dsdpcm_slot_t&& slot) = delete;
};

class dsdpcm_engine_t {
	size_t  channels;
	size_t  framerate;
	size_t  dsd_samplerate;
	size_t  pcm_samplerate;
	double* fir_data;
	size_t  fir_size;
	size_t  fir_decimation;
	double  conv_delay;

	std::vector<dsdpcm_slot_t<float>>  convSlots_fp32;
	dsdpcm_filter_setup_t<float>       fltSetup_fp32;
	std::vector<dsdpcm_slot_t<double>> convSlots_fp64;
	dsdpcm_filter_setup_t<double>      fltSetup_fp64;

	conv_type_e conv_type;
	bool        conv_fp64;

public:
	dsdpcm_engine_t();
	~dsdpcm_engine_t();
	double get_delay();
	int init(size_t p_channels, size_t p_framerate, size_t p_dsd_samplerate, size_t p_pcm_samplerate, conv_type_e p_conv_type, bool p_conv_fp64, double* p_fir_data = nullptr, size_t p_fir_size = 0, size_t p_fir_decimation = 0);
	void free();
	size_t convert(const uint8_t* p_dsd_data, const size_t p_dsd_size, audio_sample* p_pcm_data);
private:
	void reinit();
	template<typename real_t> bool init_slots(std::vector<dsdpcm_slot_t<real_t>>& slots, dsdpcm_filter_setup_t<real_t>& fltSetup);
	template<typename real_t> void free_slots(std::vector<dsdpcm_slot_t<real_t>>& slots);
	template<typename real_t> size_t convert(std::vector<dsdpcm_slot_t<real_t>>& slots, const uint8_t* inp_data, const size_t inp_size, audio_sample* out_data);
};
