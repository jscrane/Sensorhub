#include <getopt.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "sensorlib.h"

static int lcd = -1, mux = -1;
static bool verbose = false;
#define TEMP	"tiny_temp"
#define BATT	"tiny_batt"
#define HUMI	"tiny_humi"
#define WIRED	"temp"
#define TIMEOUT_SECS	600

static int width, height;

struct reading {
	sensor s;
	time_t last;
};
struct reading readings[MAX_SENSORS];

void close_sockets() {
	if (lcd >= 0)
		close(lcd);
	if (mux >= 0)
		close(mux);
}

void signal_handler(int signo) {
	fatal("Caught: %s\n", strsignal(signo));
}

int lcdproc(char *buf, int len, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int n = vsnprintf(buf, len, fmt, args);
	if (verbose)
		printf("%d: %s", lcd, buf);
	write(lcd, buf, n);
	n = sock_read_line(lcd, buf, len);
	if (verbose)
		printf("%d: %d [%s]\n", lcd, n, buf);
	va_end(args);
	return n;
}

void parse_lcdproc_header(char *buf, int n) {
	int i = 0;
	for (char *p = buf, *q = 0; p; p = q) {
		q = strchr(p, ' ');
		if (q)
			*q++ = 0;
		switch (i) {
		case 7:
			width = atoi(p);
			break;
		case 9:
			height = atoi(p);
			break;
		}
		i++;
	}
}

void update_lcd(int sid, const char *screen, const char *t) {
	char buf[64];
	int x = 1, y = sid;
	if (y > height) {
		y -= height;
		x += width / 2;
	}
	lcdproc(buf, sizeof(buf), "widget_set %s sensor%d %d %d {%s}\n", screen, sid, x, y, t);
}

void update_temp(sensor &s, int sid, const char *screen) {
	char t[16];
	snprintf(t, sizeof(t), "%.4s %4.1f", s.short_name, s.temperature);
	update_lcd(sid, screen, t);
}

void update_batt(sensor &s, int sid, const char *screen) {
	char t[16];
	snprintf(t, sizeof(t), "%.4s %4.2f", s.short_name, s.battery);
	update_lcd(sid, screen, t);
}

void update_humi(sensor &s, int sid, const char *screen) {
	char t[16];
	snprintf(t, sizeof(t), "%.4s %4.1f", s.short_name, s.humidity);
	update_lcd(sid, screen, t);
}

void blank_sensor(sensor &s, const char *screen) {
	char t[16];
	snprintf(t, sizeof(t), "%.4s     ", s.short_name);
	update_lcd(s.node_id, screen, t);
}

void check_timeout(sensor &s, const char *screen, time_t &now) {
	reading &r = readings[s.node_id];
	r.s = s;
	r.last = now;
	for (int i = 0; i < MAX_SENSORS; i++) {
		struct reading &r = readings[i];
		if (r.s.short_name[0] && now - r.last > TIMEOUT_SECS)
			blank_sensor(r.s, screen);
	}
}

void update_time(time_t &now) {
	char t[16], buf[64];
	strftime(t, sizeof(t), "%H:%M", localtime(&now));
	lcdproc(buf, sizeof(buf), "widget_set " TEMP " update %d %d {%s}\n", width-strlen(t)+1, height, t);
}

void init_lcd() {
	char buf[128];
	int n = lcdproc(buf, sizeof(buf), "hello\n");
	height = width = 0;
	for (;;) {
		parse_lcdproc_header(buf, n);
		if (width > 0)
			break;
		n = lcdproc(buf, sizeof(buf), "");
	}

	lcdproc(buf, sizeof(buf), "client_set name {Sensors}\n");
	lcdproc(buf, sizeof(buf), "screen_add " TEMP "\n");
	lcdproc(buf, sizeof(buf), "screen_set " TEMP " name {Wireless}\n");
	lcdproc(buf, sizeof(buf), "screen_add " HUMI "\n");
	lcdproc(buf, sizeof(buf), "screen_set " HUMI " name {Humidity}\n");
	lcdproc(buf, sizeof(buf), "screen_add " BATT "\n");
	lcdproc(buf, sizeof(buf), "screen_set " BATT " name {Battery}\n");
	lcdproc(buf, sizeof(buf), "screen_add " WIRED "\n");
	lcdproc(buf, sizeof(buf), "screen_set " WIRED " name {Wired}\n");

	for (int i = 0; i < MAX_SENSORS; i++) {
		lcdproc(buf, sizeof(buf), "widget_add " WIRED " sensor%d string\n", i);
		lcdproc(buf, sizeof(buf), "widget_add " TEMP " sensor%d string\n", i);
		lcdproc(buf, sizeof(buf), "widget_add " BATT " sensor%d string\n", i);
		lcdproc(buf, sizeof(buf), "widget_add " HUMI " sensor%d string\n", i);
	}
		
	lcdproc(buf, sizeof(buf), "widget_add " TEMP " update string\n");

	lcdproc(buf, sizeof(buf), "widget_add " BATT " units string\n");
	lcdproc(buf, sizeof(buf), "widget_set " BATT " units %d %d {Vbatt}\n", width-4, height);
	lcdproc(buf, sizeof(buf), "widget_add " HUMI " units string\n");
	lcdproc(buf, sizeof(buf), "widget_set " HUMI " units %d %d {Hum %}\n", width-4, height);

	lcdproc(buf, sizeof(buf), "backlight off\n");
}

int main(int argc, char *argv[]) {
	int opt;
	bool daemon = true;
	const char *lcd_host = "localhost", *mux_host = "localhost";

	atexit(close_sockets);
	while ((opt = getopt(argc, argv, "l:m:vf")) != -1)
		switch (opt) {
		case 'l':
			lcd_host = optarg;
			break;
		case 'm':
			mux_host = optarg;
			break;
		case 'v':
			verbose = true;
			daemon = false;
			break;
		case 'f':
			daemon = false;
			break;
		default:
			fatal("Usage: %s: -l lcd:port -m mux:port [-v] [-f]\n", argv[0]);
		}
		
	if (daemon)
		daemon_mode();

	signal(SIGINT, signal_handler);
	signal(SIGPIPE, SIG_IGN);

	fd_set rd, wr, srd, swr;
	FD_ZERO(&rd);
	FD_ZERO(&srd);
	FD_ZERO(&wr);
	FD_ZERO(&swr);
	for (;;) {
		char buf[128];
		int n;
		if (lcd < 0) {
			lcd = connect_nonblock(lcd_host, 13666);
			if (lcd >= 0)
				FD_SET(lcd, &swr);
		}
		if (mux < 0) {
			mux = connect_nonblock(mux_host, 5678);
			if (mux >= 0)
				FD_SET(mux, &swr);
		}

		rd = srd;
		wr = swr;
		if (0 > select(1 + (mux > lcd? mux: lcd), &rd, &wr, 0, 0))
			fatal("select: %s\n", strerror(errno));

		if (FD_ISSET(lcd, &rd)) {
			n = sock_read_line(lcd, buf, sizeof(buf));
			if (n > 0) {
				if (verbose)
					printf("%d: %d [%s]\n", lcd, n, buf);
			} else if (n == 0) {
				if (verbose)
					printf("LCD died\n");
				FD_CLR(lcd, &srd);
				close(lcd);
				lcd = -1;
			}
		} else if (FD_ISSET(lcd, &wr)) {
			FD_CLR(lcd, &swr);
			lcd = on_connect(lcd);
			if (lcd >= 0) {
				FD_SET(lcd, &srd);
				init_lcd();
			} else
				sleep(1);
		}
		if (FD_ISSET(mux, &rd)) {
			n = sock_read_line(mux, buf, sizeof(buf));
			if (verbose)
				printf("%d: %d [%s]\n", mux, n, buf);
			if (n > 0) {
				sensor s;
				s.from_csv(buf);
				if (s.is_wireless()) {
					update_temp(s, s.node_id, TEMP);
					update_batt(s, s.node_id, BATT);
					update_humi(s, s.node_id, HUMI);
					struct timeval tv;
					gettimeofday(&tv, 0);
					time_t now = tv.tv_sec;
					update_time(now);
					check_timeout(s, TEMP, now);
				} else
					update_temp(s, s.node_id - 19, WIRED);
			} else if (n == 0) {
				if (verbose)
					printf("Mux died\n");
				FD_CLR(mux, &srd);
				close(mux);
				mux = -1;
			}
		} else if (FD_ISSET(mux, &wr)) {
			FD_CLR(mux, &swr);
			mux = on_connect(mux);
			if (mux >= 0)
				FD_SET(mux, &srd);
		}
	}
}
