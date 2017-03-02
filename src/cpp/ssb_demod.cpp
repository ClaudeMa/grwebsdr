/*
 * GrWebSDR: a web SDR receiver
 *
 * Copyright (C) 2017 Ondřej Lysoněk
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (see the file COPYING).  If not,
 * see <http://www.gnu.org/licenses/>.
 */

#include "ssb_demod.h"
#include <boost/math/common_factor_rt.hpp>
#include <gnuradio/filter/firdes.h>
#include <gnuradio/filter/rational_resampler_base_fff.h>
#include <gnuradio/analog/agc_cc.h>
#include <gnuradio/blocks/complex_to_mag.h>
#include <gnuradio/analog/sig_source_c.h>
#include <gnuradio/blocks/add_cc.h>
#include <gnuradio/blocks/conjugate_cc.h>
#include <gnuradio/blocks/multiply_const_ff.h>

using namespace gr;
using namespace gr::analog;
using namespace gr::blocks;
using namespace gr::filter;

ssb_demod::sptr ssb_demod::make(int in_rate, int out_rate, int cutoff, int trans)
{
	return boost::shared_ptr<ssb_demod>(new ssb_demod(in_rate, out_rate, cutoff, trans));
}

ssb_demod::ssb_demod(int in_rate, int out_rate, int cutoff, int trans)
	: hier_block2("ssb_demod", io_signature::make(1, 1, sizeof(gr_complex)),
			io_signature::make(1, 1, sizeof(float)))
{
	int div = boost::math::gcd(in_rate, out_rate);
	int interpolation = out_rate / div;
	int decimation = in_rate / div;

	agc_cc::sptr agc;
	sig_source_c::sptr carrier;
	conjugate_cc::sptr conj;
	add_cc::sptr add, add2;
	complex_to_mag::sptr mag;
	multiply_const_ff::sptr mult;
	rational_resampler_base_fff::sptr resampler;

	agc = agc_cc::make(0.01f, 0.05f);
	carrier = sig_source_c::make(in_rate, GR_SIN_WAVE, 0, 0.15);
	add = add_cc::make();
	add2 = add_cc::make();
	conj = conjugate_cc::make();
	mag = complex_to_mag::make();
	mult = multiply_const_ff::make(10);
	resampler = rational_resampler_base_fff::make(interpolation, decimation,
			firdes::low_pass(1.0, in_rate, cutoff, trans));

	connect(self(), 0, agc, 0);
	connect(agc, 0, add, 0);
	connect(agc, 0, conj, 0);
	connect(conj, 0, add, 1);
	connect(add, 0, add2, 0);
	connect(carrier, 0, add2, 1);
	connect(add2, 0, mag, 0);
	connect(mag, 0, resampler, 0);
	connect(resampler, 0, mult, 0);
	connect(mult, 0, self(), 0);
}

ssb_demod::~ssb_demod()
{
	disconnect_all();
}
