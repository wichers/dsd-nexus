/*
* SACD Decoder plugin
* Copyright (c) 2011-2021 Maxim V.Anisiutkin <maxim.anisiutkin@gmail.com>
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

template<typename real_t>
class dsdpcm_converter_multistage_t : public dsdpcm_converter_t<real_t> {
	using dsdpcm_converter_t<real_t>::dsd_filter;
	using dsdpcm_converter_t<real_t>::pcm_filters;
	using dsdpcm_converter_t<real_t>::set_buffers;
public:
	dsdpcm_converter_multistage_t(dsdpcm_filter_setup_t<real_t>& flt_setup, size_t p_framerate, size_t p_dsd_samplerate, size_t p_pcm_samplerate) : dsdpcm_converter_t<real_t>(p_framerate, p_dsd_samplerate, p_pcm_samplerate) {
		auto ratio = this->dsd_to_pcm_ratio;
		if (ratio > 32) {
			dsd_filter.init(flt_setup.get_fir1_16_ctables(), flt_setup.get_fir1_16_length(), 16);
			ratio /= 16;
		}
		else {
			dsd_filter.init(flt_setup.get_fir1_8_ctables(), flt_setup.get_fir1_8_length(), 8);
			ratio /= 8;
		}
		while (ratio > 2) {
			pcm_filters.push_back(new pcmpcm_fir_t(flt_setup.get_fir2_2_coefs(), flt_setup.get_fir2_2_length(), 2));
			ratio /= 2;
		}
		if (ratio > 1) {
			if (this->is_48k) {
				pcm_filters.push_back(new pcmpcm_fir_t(flt_setup.get_fir4_147_80_coefs(), flt_setup.get_fir4_147_80_length(), 147, 80));
			}
			else {
				pcm_filters.push_back(new pcmpcm_fir_t(flt_setup.get_fir3_2_coefs(), flt_setup.get_fir3_2_length(), 2));
			}
			ratio /= 2;
		}
		else {
			if (this->is_48k) {
				pcm_filters.push_back(new pcmpcm_fir_t(flt_setup.get_fir4_147_160_coefs(), flt_setup.get_fir4_147_160_length(), 147, 160));
			}
		}
		set_buffers(this->dsd_samplerate / 8 / this->framerate);
	}
};
