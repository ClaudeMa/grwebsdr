#include <boost/shared_ptr.hpp>
#include <gnuradio/audio/sink.h>
#include <gnuradio/analog/quadrature_demod_cf.h>
#include <gnuradio/filter/fir_filter_ccf.h>
#include <gnuradio/filter/fir_filter_fff.h>
#include <gnuradio/filter/firdes.h>
#include <gnuradio/filter/rational_resampler_base_ccc.h>
#include <gnuradio/filter/rational_resampler_base_fff.h>
#include <gnuradio/filter/freq_xlating_fir_filter_ccc.h>
#include <gnuradio/blocks/wavfile_sink.h>
#include <gnuradio/top_block.h>
#include <iostream>
#include <cmath>
#include <osmosdr/source.h>
#include <cstdio>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <microhttpd.h>

#define PORT 8080

using namespace boost;
using namespace gr;
using namespace gr::analog;
using namespace gr::filter;
using namespace gr::blocks;

/* C++ translation of the design_filter function from rational_resampler.py
 * from GNU Radio.
 */
std::vector<float> filter_f(int interpolation, int decimation, double fbw)
{
	float beta = 7.0;
	float halfband = 0.5;
	float rate = ((float) interpolation) / decimation;
	float trans_width;
	float mid;

	if (rate >= 1.0) {
		trans_width = halfband - fbw;
		mid = halfband - trans_width / 2.0f;
	} else {
		trans_width = rate * (halfband - fbw);
		mid = rate * halfband - trans_width / 2.0f;
	}
	return firdes::low_pass(interpolation, interpolation, mid, trans_width,
			firdes::WIN_KAISER, beta);
}

std::vector<gr_complex> filter_c(int interpolation, int decimation, double fbw)
{
	std::vector<float> f = filter_f(interpolation, decimation, fbw);
	std::vector<gr_complex> ret;

	for (auto i : f)
		ret.push_back(gr_complex(i, 0.0));
	return ret;
}

std::vector<gr_complex> taps_f2c(std::vector<float> vec)
{
	std::vector<gr_complex> ret;

	ret.reserve(vec.size());
	for (float i : vec)
		ret.push_back(gr_complex(i, 0.0));
	return ret;
}

int rfd;

ssize_t callback(void *cls, uint64_t pos, char *buf, size_t max)
{
	ssize_t ret;

	(void) cls;
	(void) pos;
	ret = read(rfd, buf, max);
	if (ret < 0) {
		perror("read");
		return MHD_CONTENT_READER_END_WITH_ERROR;
	} else {
		return ret;
	}
}

int answer(void *cls, struct MHD_Connection *con, const char *url,
		const char *method, const char *version,
		const char *upload_data, size_t *upload_data_size,
		void **con_cls)
{
	struct MHD_Response *response;
	int ret;

	(void) cls;
	(void) url;
	(void) version;
	(void) upload_data;
	(void) upload_data_size;

	if (*con_cls != &answer) {
		*con_cls = (void *) &answer;
		return MHD_YES;
	}
	*con_cls = NULL;
	if (strcmp(method, MHD_HTTP_METHOD_GET) != 0)
		return MHD_NO;
	response = MHD_create_response_from_callback(MHD_SIZE_UNKNOWN, 1024,
			&callback, NULL, NULL);
	MHD_add_response_header (response, "Content-Type", "audio/x-wav");
	ret = MHD_queue_response(con, MHD_HTTP_OK, response);
	return ret;
}

int main()
{
	double src_rate = 2000000.0;
	int dec1 = 8; // Pre-demodulation decimation
	double dec1_rate = src_rate / dec1; // Sample rate after first decimation
	int dec2 = dec1_rate / 1000; // Decimate down to 1kHz
	const char *fifo_name = "/tmp/wav-fifo";
	struct MHD_Daemon *daemon;

	if (mknod(fifo_name, 0600 | S_IFIFO, 0) == -1) {
		perror("mknod");
		return 1;
	}
	rfd = open(fifo_name, O_RDONLY | O_NONBLOCK);
	fcntl(rfd, F_SETPIPE_SZ, getpagesize());

	daemon = MHD_start_daemon(MHD_USE_DEBUG | MHD_USE_SELECT_INTERNALLY,
			PORT, NULL, NULL, &answer, NULL, MHD_OPTION_END);
	if (daemon == NULL) {
		fprintf(stderr, "Failed to create MHD daemon.\n");
		return 1;
	}

	osmosdr::source::sptr src = osmosdr::source::make();

	freq_xlating_fir_filter_ccc::sptr xlate =
			freq_xlating_fir_filter_ccc::make(dec1,
			taps_f2c(firdes::low_pass(1.0, src_rate, 75000, 25000)),
			0.0, src_rate);

	quadrature_demod_cf::sptr demod = quadrature_demod_cf::make(
			dec1_rate /(2*M_PI*75000));

	fir_filter_fff::sptr low_pass2 = fir_filter_fff::make(1,
			firdes::low_pass(1.0, dec1_rate,
			dec1_rate/2 - dec1_rate / 1000, dec1_rate / 1000));

	rational_resampler_base_fff::sptr resampler2 =
			rational_resampler_base_fff::make(48, dec2,
			filter_f(48, dec2, 0.4f));

	wavfile_sink::sptr sink = wavfile_sink::make(fifo_name, 1, 48000);

	src->set_sample_rate(src_rate);
	src->set_center_freq(99000000.0);
	src->set_freq_corr(0.0);
	src->set_dc_offset_mode(0);
	src->set_iq_balance_mode(0);
	src->set_gain_mode(false);
	src->set_gain(10.0);
	src->set_if_gain(20.0);
	src->set_bb_gain(20.0);
	src->set_bandwidth(0.0);

	top_block_sptr bl = make_top_block("bla");
	bl->connect(src, 0, xlate, 0);
	bl->connect(xlate, 0, demod, 0);
	bl->connect(demod, 0, low_pass2, 0);
	bl->connect(low_pass2, 0, resampler2, 0);
	bl->connect(resampler2, 0, sink, 0);

	bl->start();
	getchar();
	bl->stop();
	bl->wait();

	MHD_stop_daemon(daemon);

	return 0;
}
