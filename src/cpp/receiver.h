#ifndef RECEIVER_H
#define RECEIVER_H

#include "ogg_sink.h"
#include <boost/shared_ptr.hpp>
#include <gnuradio/top_block.h>
#include <gnuradio/filter/fir_filter_ccf.h>
#include <gnuradio/filter/firdes.h>
#include <gnuradio/filter/freq_xlating_fir_filter_ccc.h>
#include <gnuradio/hier_block2.h>
#include <osmosdr/source.h>
#include <cstdio>
#include <string>

class receiver : public gr::hier_block2 {
public:
	typedef boost::shared_ptr<receiver> sptr;
	enum demod_t { NO_DEMOD, WFM_DEMOD, FM_DEMOD, AM_DEMOD,
			USB_DEMOD, LSB_DEMOD, CW_DEMOD };

	static sptr make(double src_rate, gr::top_block_sptr top_bl,
			int fds[2]);
	void set_center_freq(double freq);
	int *get_fd();
	~receiver();
	bool get_privileged();
	void set_privileged(bool val);
	void change_demod(demod_t d);
	std::string get_source_name();
	osmosdr::source::sptr get_source();
	void set_source(std::string source_name);
	bool is_ready();
	bool is_running();
	bool start();
	void stop();

private:
	receiver(double src_rate, gr::top_block_sptr top_bl, int fds[2]);
	std::string source_name;
	osmosdr::source::sptr source;
	double src_rate;
	gr::top_block_sptr top_bl;
	gr::filter::freq_xlating_fir_filter_ccc::sptr xlate;
	gr::basic_block_sptr demod;
	ogg_sink::sptr sink;
	int fds[2];
	bool privileged;
	int audio_rate;
	bool running;
	demod_t demod_type;

	void connect_blocks();
};

#endif
