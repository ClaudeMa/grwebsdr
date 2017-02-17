#include "receiver.h"
#include "stuff.h"
#include "utils.h"
#include "websocket.h"
#include "http.h"
#include "config.h"
#include <iostream>
#include <cmath>
#include <cstdio>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <microhttpd.h>
#include <mutex>
#include <cstring>
#include <unordered_map>
#include <cstdlib>
#include <osmosdr/device.h>
#include <vector>

#define PORT 8080

using namespace gr;
using namespace std;

vector<osmosdr::source::sptr> osmosdr_sources;
vector<source_info_t> sources_info;
unordered_map<string, receiver::sptr> receiver_map;
string username;
string password;

top_block_sptr topbl;

void usage(const char *progname)
{
	printf("Usage: %s [options]\n\n", progname);
	printf("Options: -h                     Print help\n");
	printf("         -c certificate_file\n");
	printf("         -k private_key_file\n");
	printf("         -s                     Scan for sources\n");
	printf("         -f config_file\n");
}

string get_username()
{
	string ret;

	cout << "Enter new admin user name: ";
	cout.flush();
	getline(cin, ret);
	return ret;
}

string get_password()
{
	string ret;
	cout << "Enter new admin password: ";
	cout.flush();
	system("/usr/bin/stty -echo");
	getline(cin, ret);
	system("/usr/bin/stty echo");
	return ret;
}

bool should_use_source(string name)
{
	string answer;

	cout << "Use the following source? " << name << endl;
	while (answer != "y" && answer != "n") {
		cout << "[y/n] ";
		cout.flush();
		getline(cin, answer);
	}
	return answer == "y";
}

int ask_freq_converter_offset()
{
	int ret;
	string line;

	cout << "Enter up/down converter offset for the device: ";
	cout.flush();
	getline(cin, line);
	ret = stoi(line);
	return ret;
}

int ask_hw_freq()
{
	int ret;
	string line;

	cout << "Enter initial HW frequency: ";
	cout.flush();
	getline(cin, line);
	ret = stoi(line);
	return ret;
}

int ask_sample_rate()
{
	int ret;
	string line;

	cout << "Enter sample rate: ";
	cout.flush();
	getline(cin, line);
	ret = stoi(line);
	return ret;
}

const struct lws_protocols protocols[] = {
	{ "http-only", &http_cb, sizeof(struct http_user_data),
		HTTP_MAX_PAYLOAD, 0, nullptr},
	{ "websocket", &websocket_cb, sizeof(struct websocket_user_data),
		WEBSOCKET_MAX_PAYLOAD, 0, nullptr},
	{ nullptr, nullptr, 0, 0, 0, nullptr}
};

struct lws_context *ws_context;
struct lws_pollfd *pollfds;
int *fd_lookup;
int count_pollfds;
int max_fds;
struct lws **fd2wsi;

int run(const char *key_path, const char *cert_path)
{
	struct lws_context_creation_info info;
	int n;
	bool quitting = false;

	max_fds = getdtablesize();
	pollfds = (struct lws_pollfd *) malloc(sizeof(*pollfds) * max_fds);
	fd_lookup = (int *) malloc(sizeof(*fd_lookup) * max_fds);
	fd2wsi = (struct lws **) calloc(max_fds, sizeof(*fd2wsi));
	if (!pollfds || !fd_lookup || !fd2wsi) {
		puts("malloc failed");
		return -1;
	}
	add_pollfd(STDIN_FILENO, POLLIN);

	memset(&info, 0, sizeof(info));
	info.port = PORT;
	info.iface = nullptr;
	info.protocols = protocols;
	info.gid = -1;
	info.uid = -1;
	info.max_http_header_pool = 20;
	info.ssl_cert_filepath = cert_path;
	info.ssl_private_key_filepath = key_path;
	info.options |= LWS_SERVER_OPTION_REDIRECT_HTTP_TO_HTTPS;

	ws_context = lws_create_context(&info);
	if (!ws_context) {
		fprintf(stderr, "Failed to create Websocket context.\n");
		return -1;
	}
	puts("Starting the server. Press enter to quit.");

	while (!quitting) {
		n = poll(pollfds, count_pollfds, 50);
		if (n <= 0)
			continue;
		for (n = 0; n < count_pollfds; ++n) {
			if (!pollfds[n].revents)
				continue;
			if (pollfds[n].fd == STDIN_FILENO) {
				quitting = true;
				break;
			}
			lws_service_fd(ws_context, &pollfds[n]);
			// If lws didn't service the fd, it might be
			// a receiver fd
			if (pollfds[n].revents && fd2wsi[pollfds[n].fd]) {
				lws_callback_on_writable(fd2wsi[pollfds[n].fd]);
			}
		}
	}

	puts("Stopping the server.");
	lws_context_destroy(ws_context);
	return 0;
}

void scan_sources()
{
	osmosdr::devices_t devices;

	cout << "Looking for sources..." << endl;
	devices = osmosdr::device::find();
	for (osmosdr::device_t device : devices) {
		cout << device.to_string() << endl;
	}
}

void add_sources_interactive()
{
	osmosdr::devices_t devices;

	cout << "Looking for tuners..." << endl;
	devices = osmosdr::device::find(osmosdr::device_t("nofake"));
	for (osmosdr::device_t device : devices) {
		string str = device.to_string();
		if (should_use_source(str)) {
			int offset, freq, sample_rate;
			osmosdr::source::sptr source;
			source_info_t info;

			offset = ask_freq_converter_offset();
			freq = ask_hw_freq();
			sample_rate = ask_sample_rate();
			source = osmosdr::source::make(str);
			source->set_sample_rate(sample_rate);
			source->set_center_freq(freq);
			osmosdr_sources.push_back(source);
			info.freq_converter_offset = offset;
			info.label = str;
			sources_info.push_back(info);
		}
	}
}

int main(int argc, char **argv)
{
	const char *cert_path = nullptr, *key_path = nullptr;
	const char *config_path = nullptr;
	int c;

	while ((c = getopt(argc, argv, "hc:k:sf:")) != -1) {
		switch (c) {
		case 'h':
			usage(argv[0]);
			return 0;
		case 'c':
			cert_path = optarg;
			break;
		case 'k':
			key_path = optarg;
			break;
		case 's':
			scan_sources();
			return 0;
		case 'f':
			config_path = optarg;
			break;
		default:
			usage(argv[0]);
			return 1;
		}
	}

	if (config_path == nullptr) {
		add_sources_interactive();
	} else {
		if (!process_config(config_path))
			return 1;
	}

	if (osmosdr_sources.size() == 0) {
		cout << "No tuner selected. Quitting." << endl;
		return 0;
	}

	username = get_username();
	password = get_password();

	topbl = make_top_block("top_block");

	for (osmosdr::source::sptr src : osmosdr_sources) {
		src->set_freq_corr(0.0);
		src->set_dc_offset_mode(0);
		src->set_iq_balance_mode(0);
		src->set_gain_mode(true);
		src->set_bandwidth(0.0);
	}

	run(key_path, cert_path);

	getchar();
	topbl->stop();
	topbl->wait();

	return 0;
}
