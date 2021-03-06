#include "framebuffer.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

#include <linux/fb.h>
#include <linux/kd.h>

#include <sys/mman.h>
#include <stropts.h>

#include <string.h>

#include "text/utf8.h"
#include "font/font8x8.h"

static int init_term()
{
	int log_file;
	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	int console_fd;
	char *tty_name = ttyname(STDIN_FILENO);

	if (!tty_name)
		tty_name = "/dev/tty1";
	console_fd = open(tty_name, O_RDWR | O_NDELAY);
	if (console_fd >= 0) {
		if (ioctl(console_fd, KDSETMODE, KD_GRAPHICS) < 0) {
			perror("KDSETMODE");
			close(console_fd);
			return -1;
		}
	}

	if (isatty(STDOUT_FILENO)) {
		if (!getenv("VT"))
			return console_fd;
		log_file = open("/tmp/desco.log",
			O_WRONLY | O_CREAT | O_APPEND | O_SYNC, mode);
	} else {
		log_file = open("/var/log/desco.log",
			O_WRONLY | O_CREAT | O_APPEND | O_SYNC, mode);
	}
	if (log_file < 0) {
		perror("Cannot open log file for writing");

		return console_fd;
	}

	if (dup2(log_file, STDERR_FILENO) != STDERR_FILENO ||
		dup2(log_file, STDOUT_FILENO) != STDOUT_FILENO) {
		perror("Unable to redirect output");
		return console_fd;
	}
	return console_fd;
}


static void reset_term(int console_fd)
{
	if (console_fd >= 0) {
		if (ioctl(console_fd, KDSETMODE, KD_TEXT) < 0) {
			perror("KDSETMODE");
		}
		close(console_fd);
	}
}


struct framebuffer *open_framebuffer()
{
	int fb = 0;
	int console_fd;
	struct fb_var_screeninfo fb_info;
	struct fb_fix_screeninfo fb_finfo;

	console_fd = init_term();

	const char *fb_name = getenv("FRAMEBUFFER");
	if (!fb_name)
		fb_name = "/dev/fb0";

	fb = open(fb_name, O_RDWR);
	if (fb < 0) {
		perror("Error: cannot open framebuffer device");
		reset_term(console_fd);
		return NULL;
	}

	// Get variable screen information
	if (ioctl(fb, FBIOGET_VSCREENINFO, &fb_info)) {
		perror("Error reading variable screen info");
		close(fb);
		reset_term(console_fd);
		return NULL;
	}
	if (ioctl(fb, FBIOGET_FSCREENINFO, &fb_finfo)) {
		perror("Error reading fixed screen info.\n");
		close(fb);
		reset_term(console_fd);
		return NULL;
	}
	if (fb_info.bits_per_pixel != 16 &&
		fb_info.bits_per_pixel != 32) {
		fprintf(stderr, "%d bits per pixel is not supported."
			"Only 16 or 32 are handled.\n",
			fb_info.bits_per_pixel);
		close(fb);
		reset_term(console_fd);
		return NULL;
	}
	struct framebuffer *ret = malloc(sizeof(struct framebuffer));

	ret->width = fb_info.xres;
	ret->height = fb_info.yres;
	ret->bpp = fb_info.bits_per_pixel;

	ret->line_length = fb_finfo.line_length;
	ret->data_length = fb_finfo.smem_len;

	ret->u8_data = mmap(NULL, fb_finfo.smem_len, PROT_READ | PROT_WRITE,
		MAP_SHARED, fb, 0);
	ret->console_fd = console_fd;

	if (!ret->u8_data) {
		perror("Cannot map framebuffer");
		close(fb);
		free(ret);
		reset_term(console_fd);
		return NULL;
	}
	ret->fd = fb;

	return ret;
}

void close_framebuffer(struct framebuffer *fb)
{
	reset_term(fb->console_fd);
	munmap(fb->u8_data, fb->data_length);
	close(fb->fd);
	free(fb);
}

void pause_framebuffer(struct framebuffer *fb)
{
	if (fb->console_fd >= 0 &&
		ioctl(fb->console_fd, KDSETMODE, KD_TEXT) < 0)
		perror("KDSETMODE");
}

void resume_framebuffer(struct framebuffer *fb)
{
	if (fb->console_fd >= 0 &&
		ioctl(fb->console_fd, KDSETMODE, KD_GRAPHICS) < 0)
		perror("KDSETMODE");
}

void clear_framebuffer(struct framebuffer *fb, color_t color)
{
	unsigned int i;
	unsigned int value = color.value;

	if (fb->bpp == 16) {
		value |= (value << 16);
	}
	for (i = 0; i < fb->data_length / 4; i++)
		fb->u32_data[i] = value;
}

static void print_char(struct framebuffer *fb, unsigned int start_x, unsigned int start_y, color_t color, color_t backcolor, uint32_t c)
{
	char *char_array = NULL;
	if (c <= 0x7f)
		char_array = font8x8_basic[c];
	else if (c >= 0x80 && c <= 0x9F)
		char_array = font8x8_control[c - 0x80];
	else if (c >= 0xA0 && c <= 0xff)
		char_array = font8x8_ext_latin[c - 0xA0];
	else if (c >= 0x390 && c <= 0x3C9)
		char_array = font8x8_greek[c - 0x390];
	else if (c >= 0x2500 && c <= 0x257F)
		char_array = font8x8_box[c - 0x2500];
	else if (c >= 0x2580 && c <= 0x259F)
		char_array = font8x8_block[c- 0x2580];
	else if (c >= 0x3040 && c <= 0x309F)
		char_array = font8x8_hiragana[c - 0x3040];
	else
		return;

	int x,y;

	for (y = 0; y < 8; ++y) {
		uint8_t val = char_array[y];
		if (!val && is_transparent(backcolor))
			continue;

		for (x = 0; x < 8; ++x) {
			if (val & (1 << x)) {
				memcpy(fb->u8_data + (start_y + y) * fb->line_length + (start_x + x) * fb->bpp / 8,
					&color.value, fb->bpp / 8);
			} else if (!is_transparent(backcolor)) {
				memcpy(fb->u8_data + (start_y + y) * fb->line_length + (start_x + x) * fb->bpp / 8,
					&backcolor.value, fb->bpp / 8);
			}
		}
	}
}

void fb_print(struct framebuffer *fb, unsigned int x, unsigned int y, color_t color, color_t backcolor, const char *str)
{
	int i = 0;

	uint32_t c;

	while (1) {
		if (x + 7 > fb->width) {
			x = x%8;
			y += 8;
		}
		if (y > fb->height)
			break;
		c = u8_nextchar(str, &i);
		if (!c)
			break;
		print_char(fb, x, y, color, backcolor, c);
		x += 8;
	}
}

void fb_vprintf(struct framebuffer *fb, unsigned int x, unsigned int y, color_t color, color_t backcolor, const char *format, va_list ap)
{
	int size = 100, needed;
	char *p;

	p = alloca(size);
try_alloc:
	needed = vsnprintf(p, size, format, ap);

	if (needed < 0)
		return;
	if (needed >= size) {
		p = alloca(needed - size + 1);
		size = needed + 1;
		goto try_alloc;
	}
	fb_print(fb, x, y, color, backcolor, p);
}

void fb_printf(struct framebuffer *fb, unsigned int x, unsigned int y, color_t color, color_t backcolor, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	fb_vprintf(fb, x, y, color, backcolor, format, ap);
	va_end(ap);
}
