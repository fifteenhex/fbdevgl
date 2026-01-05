#ifndef _H_FBDEVGL
#define _H_FBDEVGL

#include <linux/fb.h>

#define FBDEVGL_ARRAY_SIZE(_a) (sizeof(_a) / sizeof(_a[0]))

struct fbdevgl_context {
	/* Native framebuffer stuff */
	int fd;
	unsigned int width;
	unsigned int height;
	unsigned int stride;
	size_t sz;
	void *fb;

	/* Rendering stuff */

	/* Geometry of the rendering area */
	unsigned short geometry[2];

	int scale;

	/* The rect damaged by the last *render pass* (yes, it feels wrong writing that.. */
	unsigned short damage_rect[2][2];

	/* private, don't touch! */
	void (*set_pixel)(struct fbdevgl_context *fbglcntx,
			  unsigned int x,
			  unsigned int y,
			  unsigned short value);
};

#define _fbdgl_scale() (fbglcntx->scale)
#define _fbdgl_stride() (fbglcntx->stride)
#define _fbdgl_geometry() (fbglcntx->geometry)
#define _fbdgl_damagerect() (fbglcntx->damage_rect)

static inline off_t start_of_line(struct fbdevgl_context *fbglcntx,
				  unsigned short y)
{
	return (y * _fbdgl_scale()) * _fbdgl_stride();
}

static inline off_t byte_in_line(struct fbdevgl_context *fbglcntx,
				 unsigned short x)
{
	switch(_fbdgl_scale()) {
	case 1: return x >> 3;
	case 2: return x >> 2;
	}

	return 0;
}

static inline unsigned int bit_in_byte(unsigned short x)
{
	return 1 << (~x & 0x7);
}

static inline unsigned int twobits_in_byte(unsigned short x)
{
	return 0b11 << ((~x & 0x3) * 2);
}

static const uint8_t twobitpatterns[][2] = {
	/* xx */
	/* xx */
	{
		0b11111111,
		0b11111111,
	},
	/* x. */
	/* xx */
	{
		0b10101010,
		0b11111111,
	},
	/* x. */
	/* .x */
	{
		0b10101010,
		0b01010101,
	},
	/* .x */
	/* .. */
	{
		0b01010101,
		0b00000000,
	},
/* not sure if these are useful or not? */
	/* xx */
	/* .x */
	{
		0b11111111,
		0b01010101,
	},
	/* .x */
	/* x. */
	{
		0b01010101,
		0b10101010,
	},
	/* .. */
	/* x. */
	{
		0b00000000,
		0b01010101,
	},
};

static inline void fbdevgl_reset_damage_rect(struct fbdevgl_context *fbglcntx)
{
	_fbdgl_damagerect()[0][0] = _fbdgl_geometry()[0];
	_fbdgl_damagerect()[0][1] = 0;
	_fbdgl_damagerect()[1][0] = _fbdgl_geometry()[1];
	_fbdgl_damagerect()[1][1] = 0;
}

static inline void fbdevgl_clear_damaged_area(struct fbdevgl_context *fbglcntx)
{
	/* Work out where we need to clear the framebuffer and do it */
	off_t damaged_line_start, damaged_line_end;
	size_t damage_sz;
	damaged_line_start = start_of_line(fbglcntx, _fbdgl_damagerect()[1][0]);
	damaged_line_end = start_of_line(fbglcntx, _fbdgl_damagerect()[1][1] + 1);
	damage_sz = damaged_line_end - damaged_line_start;

#if 0
	printf("damage rect %d:%d, %d:%d\n",
		(int) damage_rect[0][0],
		(int) damage_rect[0][1],
		(int) damage_rect[1][0],
		(int) damage_rect[1][1]);
#endif

	memset(fbglcntx->fb + damaged_line_start, 0, damaged_line_end);
}

static inline uint8_t *_fbdevgl_calculate_pixel_addr(struct fbdevgl_context *fbglcntx,
						     unsigned int x,
						     unsigned int y)
{
	unsigned int line = start_of_line(fbglcntx, y);
	unsigned int byteinline = byte_in_line(fbglcntx, x);
	unsigned int fboff = line + byteinline;
	uint8_t *fbaddr = (uint8_t *)(fbglcntx->fb + fboff);

	return fbaddr;
}

static inline void _fbdevgl_set_pixel_2(struct fbdevgl_context *fbglcntx,
			      unsigned int x,
			      unsigned int y,
			      unsigned short value)
{
	unsigned int patternidx = value % FBDEVGL_ARRAY_SIZE(twobitpatterns);
	uint8_t mask = twobits_in_byte(x);
	const uint8_t *pattern = twobitpatterns[patternidx];
	uint8_t *fbaddr = _fbdevgl_calculate_pixel_addr(fbglcntx, x, y);
	uint8_t *nextline = fbaddr + _fbdgl_stride();

	*fbaddr = (*fbaddr & ~mask) | (pattern[0] & mask);
	*nextline = (*nextline & ~mask) | (pattern[1] & mask);
}

static inline void _fbdevgl_set_pixel_1(struct fbdevgl_context *fbglcntx,
			      unsigned int x,
			      unsigned int y,
			      unsigned short value)
{	uint8_t *fbaddr = _fbdevgl_calculate_pixel_addr(fbglcntx, x, y);

	*fbaddr |= bit_in_byte(x);
}

static inline int fbdevgl_init(const char *fbdev_path, struct fbdevgl_context *fbglcntx)
{
	struct fb_var_screeninfo vscrinfo;
	unsigned int stride;
	int fbfd, ret;
	size_t sz;
	void *fb;

	fbfd = open(fbdev_path, O_RDWR);
	if (fbfd < 0) {
		printf("failed to open fbdev %s: %d\n", fbdev_path, fbfd);
		return 1;
	}

	ret = ioctl(fbfd, FBIOGET_VSCREENINFO, &vscrinfo);
	if (ret) {
		printf("failed to get var screeninfo: %d\n", ret);
		return 1;
	}

	stride = (vscrinfo.xres * vscrinfo.bits_per_pixel) / 8;
	sz = ((vscrinfo.xres * vscrinfo.yres) * vscrinfo.bits_per_pixel) / 8;
	printf("framebuffer is %d x %d @ %d bpp, %d bytes\n",
		vscrinfo.xres, vscrinfo.yres, vscrinfo.bits_per_pixel, (unsigned) sz);

	fb = mmap(0, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
	if (fb == MAP_FAILED) {
		printf("failed to map framebuffer.\n");
		return 1;
	}

	printf("framebuffer mapped to 0x%lx\n", (unsigned long) fb);

	fbglcntx->fd = fbfd;
	fbglcntx->sz = sz;
	fbglcntx->fb = fb;
	fbglcntx->width = vscrinfo.xres;
	fbglcntx->height = vscrinfo.yres;
	fbglcntx->stride = stride;

	fbglcntx->scale = 2;
	switch(_fbdgl_scale()) {
	case 2: {
		fbglcntx->set_pixel = _fbdevgl_set_pixel_2;
		break;
	}
	case 1:
		fbglcntx->set_pixel = _fbdevgl_set_pixel_1;
		break;
	}
	/* Stash the size of the framebuffer and reset the damage rect */
	_fbdgl_geometry()[0] = fbglcntx->width / fbglcntx->scale;
	_fbdgl_geometry()[1] = fbglcntx->height / fbglcntx->scale;

	return 0;
}

static inline void _fbdevgl_update_damage_rect(struct fbdevgl_context *fbglcntx,
					       unsigned int x,
					       unsigned y)
{
	/* Slide the damage rect x start out from the right */
	if (x < _fbdgl_damagerect()[0][0])
		_fbdgl_damagerect()[0][0] = x;

	/* Slide the damage rect x end out from the left */
	if (x > _fbdgl_damagerect()[0][1])
		_fbdgl_damagerect()[0][1] = x;

	/* Slide the damage rect y start out from the bottom */
	if (y < _fbdgl_damagerect()[1][0])
		_fbdgl_damagerect()[1][0] = y;

	/* Slide the damage rect y end out from the top */
	if (y > _fbdgl_damagerect()[1][1])
		_fbdgl_damagerect()[1][1] = y;
}

static inline void fbdevgl_set_pixel(struct fbdevgl_context *fbglcntx,
			      unsigned int x,
			      unsigned int y,
			      unsigned short value)
{
	_fbdevgl_update_damage_rect(fbglcntx, x, y);

	fbglcntx->set_pixel(fbglcntx, x, y, value);
}
#endif // _H_FBDEVGL
