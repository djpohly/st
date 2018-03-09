/* See LICENSE for license details. */
#include <errno.h>
#include <limits.h>
/* for BTN_* definitions */
#include <linux/input.h>
#include <locale.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <libgen.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>
#include <wld/wld.h>
#include <wld/wayland.h>
#include "wayland/xdg-shell-client-protocol.h"
#include "wayland/xdg-shell-unstable-v6-client-protocol.h"
#include <fontconfig/fontconfig.h>
#include <wchar.h>

static char *argv0;
#include "arg.h"
#include "st.h"
#include "win.h"

/* Arbitrary sizes */
#define UTF_SIZ       4
#define ESC_BUF_SIZ   (128*UTF_SIZ)
#define DRAW_BUF_SIZ  20*1024

#define MOD_MASK_ANY	UINT_MAX
#define MOD_MASK_NONE	0
#define MOD_MASK_CTRL	(1<<0)
#define MOD_MASK_ALT	(1<<1)
#define MOD_MASK_SHIFT	(1<<2)
#define MOD_MASK_LOGO	(1<<3)

#define AXIS_VERTICAL	WL_POINTER_AXIS_VERTICAL_SCROLL
#define AXIS_HORIZONTAL	WL_POINTER_AXIS_HORIZONTAL_SCROLL

/* macros */
#define IS_SET(flag)		((win.mode & (flag)) != 0)

typedef struct {
	struct xkb_context *ctx;
	struct xkb_keymap *keymap;
	struct xkb_state *state;
	xkb_mod_index_t ctrl, alt, shift, logo;
	unsigned int mods;
} XKB;

typedef struct {
	struct wl_display *dpy;
	struct wl_compositor *cmp;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct wl_keyboard *keyboard;
	struct wl_pointer *pointer;
	struct wl_data_device_manager *datadevmanager;
	struct wl_data_device *datadev;
	struct wl_data_offer *seloffer;
	struct wl_surface *surface;
	struct wl_buffer *buffer;
	struct zxdg_shell_v6 *xdgshell_v6;
	struct xdg_shell *xdgshell;
	struct xdg_surface *xdgsurface;
	/* struct xdg_popup *xdgpopup;
	   struct wl_surface *popupsurface; */
	struct zxdg_surface_v6 *xdgsurface_v6;
	struct zxdg_toplevel_v6 *xdgtoplevel;
	XKB xkb;
	int px, py; /* pointer x and y */
	int w, h; /* window width and height */
	int ch; /* char height */
	int cw; /* char width  */
	int vis;
	int cursor; /* cursor style */
	struct wl_callback * framecb;
} Wayland;

typedef struct {
	struct wld_context *ctx;
	struct wld_font_context *fontctx;
	struct wld_renderer *renderer;
	struct wld_buffer *buffer, *oldbuffer;
} WLD;

typedef struct {
	struct wl_cursor_theme *theme;
	struct wl_cursor *cursor;
	struct wl_surface *surface;
} Cursor;

typedef struct {
	uint b;
	uint mask;
	char *s;
} Mousekey;

typedef struct {
	int axis;
	int dir;
	uint mask;
	char s[ESC_BUF_SIZ];
} Axiskey;

typedef struct {
	xkb_keysym_t k;
	uint mask;
	char *s;
	/* three valued logic variables: 0 indifferent, 1 on, -1 off */
	signed char appkey;    /* application keypad */
	signed char appcursor; /* application cursor */
	signed char crlf;      /* crlf mode          */
} Key;

typedef struct {
	struct wl_data_source *source;
	char *primary;
	uint32_t tclick1, tclick2;
} WLSelection;

typedef struct {
	uint mod;
	xkb_keysym_t keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Shortcut;

typedef struct {
	char str[32];
	uint32_t key;
	int len;
	bool started;
	struct timespec last;
} Repeat;

/* function definitions used in config.h */
static void numlock(const Arg *);
static void selpaste(const Arg *);
static void zoom(const Arg *);
static void zoomabs(const Arg *);
static void zoomreset(const Arg *);

/* Config.h for applying patches and the configuration. */
#include "config.h"
#include "wlconfig.h"

/* Font structure */
typedef struct {
	int height;
	int width;
	int ascent;
	int descent;
	short lbearing;
	short rbearing;
	struct wld_font *match;
	FcFontSet *set;
	FcPattern *pattern;
} Font;

/* Drawing Context */
typedef struct {
	uint32_t col[MAX(LEN(colorname), 256)];
	Font font, bfont, ifont, ibfont;
} DC;

static void run(void);
static void cresize(int, int);

static inline int match(uint, uint);

static inline uchar sixd_to_8bit(int);
static void wldraws(char *, Glyph, int, int, int, int);
static void wldrawglyph(Glyph, int, int);
static void wlclear(int, int, int, int);
static void wldrawcursor(int, int, Glyph, int, int, Glyph);
static void wlinit(int, int);
static void wlloadcols(void);
static int wlsetcolorname(int, const char *);
static void wlloadcursor(void);
static int wlloadfont(Font *, FcPattern *);
static void wlloadfonts(char *, double);
static void wlsettitle(char *);
static void wlseturgency(int);
static void wlsetsel(char*, uint32_t);
static void wltermclear(int, int, int, int);
static void wlunloadfont(Font *f);
static void wlunloadfonts(void);
static void wlresize(int, int);

static void regglobal(void *, struct wl_registry *, uint32_t, const char *,
		uint32_t);
static void regglobalremove(void *, struct wl_registry *, uint32_t);
static void surfenter(void *, struct wl_surface *, struct wl_output *);
static void surfleave(void *, struct wl_surface *, struct wl_output *);
/* static void popupsurfenter(void *, struct wl_surface *, struct wl_output *);
static void popupsurfleave(void *, struct wl_surface *, struct wl_output *); */
static void framedone(void *, struct wl_callback *, uint32_t);
static void kbdkeymap(void *, struct wl_keyboard *, uint32_t, int32_t, uint32_t);
static void kbdenter(void *, struct wl_keyboard *, uint32_t,
		struct wl_surface *, struct wl_array *);
static void kbdleave(void *, struct wl_keyboard *, uint32_t,
		struct wl_surface *);
static void kbdkey(void *, struct wl_keyboard *, uint32_t, uint32_t, uint32_t,
		uint32_t);
static void kbdmodifiers(void *, struct wl_keyboard *, uint32_t, uint32_t,
		uint32_t, uint32_t, uint32_t);
static void kbdrepeatinfo(void *, struct wl_keyboard *, int32_t, int32_t);
static void ptrenter(void *, struct wl_pointer *, uint32_t, struct wl_surface *,
		wl_fixed_t, wl_fixed_t);
static void ptrleave(void *, struct wl_pointer *, uint32_t,
		struct wl_surface *);
static void ptrmotion(void *, struct wl_pointer *, uint32_t,
		wl_fixed_t, wl_fixed_t);
static void ptrbutton(void *, struct wl_pointer *, uint32_t, uint32_t,
		uint32_t, uint32_t);
static void ptraxis(void *, struct wl_pointer *, uint32_t, uint32_t,
		wl_fixed_t);
static void xdgshellv6ping(void *, struct zxdg_shell_v6 *, uint32_t);
static void xdgsurfv6configure(void *, struct zxdg_surface_v6 *, uint32_t);
static void xdgsurfconfigure(void *, struct xdg_surface *, int32_t, int32_t, struct wl_array*, uint32_t);
static void xdgsurfclose(void *, struct xdg_surface *);
static void xdgtopconfigure(void *, struct zxdg_toplevel_v6 *, int32_t, int32_t, struct wl_array*);
static void xdgtopclose(void *, struct zxdg_toplevel_v6 *);
static void xdgshellping(void *,struct xdg_shell *, uint32_t);
/* static void xdgpopupdone(void *, struct xdg_popup *); */
static void datadevoffer(void *, struct wl_data_device *,
		struct wl_data_offer *);
static void datadeventer(void *, struct wl_data_device *, uint32_t,
		struct wl_surface *, wl_fixed_t, wl_fixed_t, struct wl_data_offer *);
static void datadevleave(void *, struct wl_data_device *);
static void datadevmotion(void *, struct wl_data_device *, uint32_t,
		wl_fixed_t x, wl_fixed_t y);
static void datadevdrop(void *, struct wl_data_device *);
static void datadevselection(void *, struct wl_data_device *,
		struct wl_data_offer *);
static void dataofferoffer(void *, struct wl_data_offer *, const char *);
static void datasrctarget(void *, struct wl_data_source *, const char *);
static void datasrcsend(void *, struct wl_data_source *, const char *, int32_t);
static void datasrccancelled(void *, struct wl_data_source *);

static void selcopy(uint32_t);
static int x2col(int);
static int y2row(int);

static void usage(void);

static struct wl_registry_listener reglistener = { regglobal, regglobalremove };
static struct wl_surface_listener surflistener = { surfenter, surfleave };
/* static struct wl_surface_listener popupsurflistener = { popupsurfenter, popupsurfleave }; */
static struct wl_callback_listener framelistener = { framedone };
static struct wl_keyboard_listener kbdlistener =
	{ kbdkeymap, kbdenter, kbdleave, kbdkey, kbdmodifiers, kbdrepeatinfo };
static struct wl_pointer_listener ptrlistener =
	{ ptrenter, ptrleave, ptrmotion, ptrbutton, ptraxis };
static struct zxdg_shell_v6_listener shell_v6_listener = { xdgshellv6ping };
static struct zxdg_surface_v6_listener surf_v6_listener =
	{ xdgsurfv6configure };
static struct xdg_shell_listener shell_listener = { xdgshellping };
static struct xdg_surface_listener xdgsurflistener = { xdgsurfconfigure, xdgsurfclose};
static struct zxdg_toplevel_v6_listener xdgtoplevellistener =
	{ xdgtopconfigure, xdgtopclose };

/* static struct xdg_popup_listener xdgpopuplistener = { xdgpopupdone }; */
static struct wl_data_device_listener datadevlistener =
	{ datadevoffer, datadeventer, datadevleave, datadevmotion, datadevdrop,
	  datadevselection };
static struct wl_data_offer_listener dataofferlistener = { dataofferoffer };
static struct wl_data_source_listener datasrclistener =
	{ datasrctarget, datasrcsend, datasrccancelled };

/* Globals */
static TermWindow win;
static DC dc;
static Wayland wl;
static WLD wld;
static Cursor cursor;
static WLSelection wsel;
static Repeat repeat;

/* Font Ring Cache */
enum {
	FRC_NORMAL,
	FRC_ITALIC,
	FRC_BOLD,
	FRC_ITALICBOLD
};

typedef struct {
	struct wld_font *font;
	int flags;
	Rune unicodep;
} Fontcache;

/* Fontcache is an array now. A new font will be appended to the array. */
static Fontcache frc[16];
static int frclen = 0;

static bool needdraw = true;
static int oldbutton = 3;
static int oldx, oldy;
static char *usedfont = NULL;
static double usedfontsize = 0;
static double defaultfontsize = 0;

static char *opt_class = NULL;
static char **opt_cmd  = NULL;
static char *opt_embed = NULL;
static char *opt_font  = NULL;
static char *opt_io    = NULL;
static char *opt_line  = NULL;
static char *opt_name  = NULL;
static char *opt_title = NULL;

void
numlock(const Arg *dummy)
{
	win.mode ^= MODE_NUMLOCK;
}

void
zoom(const Arg *arg)
{
	Arg larg;

	larg.f = usedfontsize + arg->f;
	zoomabs(&larg);
}

void
zoomabs(const Arg *arg)
{
	wlunloadfonts();
	wlloadfonts(usedfont, arg->f);
	cresize(0, 0);
	redraw();
	/* XXX: Should the window size be updated here because wayland doesn't
	 * have a notion of hints?
	 * xhints();
	 */
}

void
zoomreset(const Arg *arg)
{
	Arg larg;

	if (defaultfontsize > 0) {
		larg.f = defaultfontsize;
		zoomabs(&larg);
	}
}

int
x2col(int x)
{
	x -= borderpx;
	LIMIT(x, 0, win.tw);
	return x / wl.cw;
}

int
y2row(int y)
{
	y -= borderpx;
	LIMIT(y, 0, win.th);
	return y / wl.ch;
}

void
mousesel(int done, uint32_t serial)
{
	int i, type = SEL_REGULAR;
	uint state = wl.xkb.mods & ~forceselmod;

	for (i = 1; i < LEN(selmasks); ++i) {
		if (match(selmasks[i], state)) {
			type = i;
			break;
		}
	}
	selextend(x2col(wl.px), y2row(wl.py), type, done);
	needdraw = true;
	if (done)
		selcopy(serial);
}

void
wlmousereport(int button, bool release, int x, int y)
{
	int len;
	char buf[40];

	if (!IS_SET(MODE_MOUSEX10)) {
		button += ((wl.xkb.mods & MOD_MASK_SHIFT) ? 4  : 0)
			+ ((wl.xkb.mods & MOD_MASK_LOGO ) ? 8  : 0)
			+ ((wl.xkb.mods & MOD_MASK_CTRL ) ? 16 : 0);
	}

	if (IS_SET(MODE_MOUSESGR)) {
		len = snprintf(buf, sizeof(buf), "\033[<%d;%d;%d%c",
				button, x+1, y+1, release ? 'm' : 'M');
	} else if (x < 223 && y < 223) {
		len = snprintf(buf, sizeof(buf), "\033[M%c%c%c",
				32+button, 32+x+1, 32+y+1);
	} else {
		return;
	}

	ttywrite(buf, len, 0);
}

void
wlmousereportbutton(uint32_t button, uint32_t state)
{
	bool release = state == WL_POINTER_BUTTON_STATE_RELEASED;

	if (!IS_SET(MODE_MOUSESGR) && release) {
		button = 3;
	} else {
		switch (button) {
		case BTN_LEFT:
			button = 0;
			break;
		case BTN_MIDDLE:
			button = 1;
			break;
		case BTN_RIGHT:
			button = 2;
			break;
		}
	}

	oldbutton = release ? 3 : button;

	/* don't report release events when in X10 mode */
	if (IS_SET(MODE_MOUSEX10) && release) {
		return;
	}

	wlmousereport(button, release, oldx, oldy);
}

void
wlmousereportmotion(wl_fixed_t fx, wl_fixed_t fy)
{
	int x = x2col(wl_fixed_to_int(fx)), y = y2row(wl_fixed_to_int(fy));

	if (x == oldx && y == oldy)
		return;
	if (!IS_SET(MODE_MOUSEMOTION) && !IS_SET(MODE_MOUSEMANY))
		return;
	/* MOUSE_MOTION: no reporting if no button is pressed */
	if (IS_SET(MODE_MOUSEMOTION) && oldbutton == 3)
		return;

	oldx = x;
	oldy = y;
	wlmousereport(oldbutton + 32, false, x, y);
}

void
wlmousereportaxis(uint32_t axis, wl_fixed_t amount)
{
	wlmousereport(64 + (axis == AXIS_VERTICAL ? 4 : 6)
		+ (amount > 0 ? 1 : 0), false, oldx, oldy);
}

void
selcopy(uint32_t serial)
{
	wlsetsel(getsel(), serial);
}

void
xclipcopy(void)
{
	selcopy(0);
}

static inline void
selwritebuf(char *buf, int len)
{
	char *repl = buf;

	/*
	 * As seen in getsel:
	 * Line endings are inconsistent in the terminal and GUI world
	 * copy and pasting. When receiving some selection data,
	 * replace all '\n' with '\r'.
	 * FIXME: Fix the computer world.
	 */
	while ((repl = memchr(repl, '\n', len))) {
		*repl++ = '\r';
	}

	if (IS_SET(MODE_BRCKTPASTE))
		ttywrite("\033[200~", 6, 0);
	ttywrite(buf, len, 1);
	if (IS_SET(MODE_BRCKTPASTE))
		ttywrite("\033[201~", 6, 0);
}

void
selpaste(const Arg *dummy)
{
	int fds[2], len, left;
	char buf[BUFSIZ], *str;

	if (wl.seloffer) {
		/* check if we are pasting from ourselves */
		if (wsel.source) {
			str = wsel.primary;
			left = strlen(wsel.primary);
			while (left > 0) {
				len = MIN(sizeof buf, left);
				memcpy(buf, str, len);
				selwritebuf(buf, len);
				left -= len;
				str += len;
			}
		} else {
			pipe(fds);
			wl_data_offer_receive(wl.seloffer, "text/plain", fds[1]);
			wl_display_flush(wl.dpy);
			close(fds[1]);
			while ((len = read(fds[0], buf, sizeof buf)) > 0) {
				selwritebuf(buf, len);
			}
			close(fds[0]);
		}
	}
}

void
wlsetsel(char *str, uint32_t serial)
{
	free(wsel.primary);
	wsel.primary = str;

	if (str) {
		wsel.source = wl_data_device_manager_create_data_source(wl.datadevmanager);
		wl_data_source_add_listener(wsel.source, &datasrclistener, NULL);
		wl_data_source_offer(wsel.source, "text/plain; charset=utf-8");
	} else {
		wsel.source = NULL;
	}
	wl_data_device_set_selection(wl.datadev, wsel.source, serial);
}

void
xsetsel(char *str)
{
	wlsetsel(str, 0);
}


void
wlresize(int col, int row)
{
	union wld_object object;

	win.tw = MAX(1, col * wl.cw);
	win.th = MAX(1, row * wl.ch);

	wld.oldbuffer = wld.buffer;
	wld.buffer = wld_create_buffer(wld.ctx, wl.w, wl.h,
			WLD_FORMAT_ARGB8888, 0);
	wld_export(wld.buffer, WLD_WAYLAND_OBJECT_BUFFER, &object);
	wl.buffer = object.ptr;
	if (wld.oldbuffer)
	{
		wld_buffer_unreference(wld.oldbuffer);
		wld.oldbuffer = 0;
	}
}

uchar
sixd_to_8bit(int x)
{
	return x == 0 ? 0 : 0x37 + 0x28 * x;
}

int
wlloadcolor(int i, const char *name, uint32_t *color)
{
	if (!name) {
		if (BETWEEN(i, 16, 255)) { /* 256 color */
			if (i < 6*6*6+16) { /* same colors as xterm */
				*color = 0xff << 24 | sixd_to_8bit(((i-16)/36)%6) << 16
					| sixd_to_8bit(((i-16)/6)%6) << 8
					| sixd_to_8bit(((i-16)/1)%6);
			} else { /* greyscale */
				*color = 0xff << 24 | (0x8 + 0xa * (i-(6*6*6+16))) * 0x10101;
			}
			return true;
		} else
			name = colorname[i];
	}

	return wld_lookup_named_color(name, color);
}

void
wlloadcols(void)
{
	int i;

	for (i = 0; i < LEN(dc.col); i++)
		if (!wlloadcolor(i, NULL, &dc.col[i])) {
			if (colorname[i])
				die("Could not allocate color '%s'\n", colorname[i]);
			else
				die("Could not allocate color %d\n", i);
		}
}

void
xloadcols(void)
{
	wlloadcols();
}

int
wlsetcolorname(int x, const char *name)
{
	uint32_t color;

	if (!BETWEEN(x, 0, LEN(dc.col)))
		return 1;

	if (!wlloadcolor(x, name, &color))
		return 1;

	dc.col[x] = color;

	return 0;
}

int
xsetcolorname(int x, const char *name)
{
	wlsetcolorname(x, name);
}

static void wlloadcursor(void)
{
	char *names[] = { mouseshape, "xterm", "ibeam", "text" };
	int i;

	cursor.theme = wl_cursor_theme_load(NULL, 32, wl.shm);

	for (i = 0; !cursor.cursor && i < LEN(names); i++)
		cursor.cursor = wl_cursor_theme_get_cursor(cursor.theme, names[i]);

	cursor.surface = wl_compositor_create_surface(wl.cmp);
}

void
wltermclear(int col1, int row1, int col2, int row2)
{
	uint32_t color = dc.col[IS_SET(MODE_REVERSE) ? defaultfg : defaultbg];
	color = (color & term_alpha << 24) | (color & 0x00FFFFFF);
	wld_fill_rectangle(wld.renderer, color, borderpx + col1 * wl.cw,
			borderpx + row1 * wl.ch, (col2-col1+1) * wl.cw,
			(row2-row1+1) * wl.ch);
}

/*
 * Absolute coordinates.
 */
void
wlclear(int x1, int y1, int x2, int y2)
{
	uint32_t color = dc.col[IS_SET(MODE_REVERSE) ? defaultfg : defaultbg];
	color = (color & term_alpha << 24) | (color & 0x00FFFFFF);
	wld_fill_rectangle(wld.renderer, color, x1, y1, x2 - x1, y2 - y1);
}

int
wlloadfont(Font *f, FcPattern *pattern)
{
	FcPattern *match;
	FcResult result;

	match = FcFontMatch(NULL, pattern, &result);
	if (!match)
		return 1;

	if (!(f->match = wld_font_open_pattern(wld.fontctx, match))) {
		FcPatternDestroy(match);
		return 1;
	}

	f->set = NULL;
	f->pattern = FcPatternDuplicate(pattern);

	f->ascent = f->match->ascent;
	f->descent = f->match->descent;
	f->lbearing = 0;
	f->rbearing = f->match->max_advance;

	f->height = f->ascent + f->descent;
	f->width = f->lbearing + f->rbearing;

	return 0;
}

void
wlloadfonts(char *fontstr, double fontsize)
{
	FcPattern *pattern;
	double fontval;
	float ceilf(float);

	if (fontstr[0] == '-') {
		/* XXX: need XftXlfdParse equivalent */
		pattern = NULL;
	} else {
		pattern = FcNameParse((FcChar8 *)fontstr);
	}

	if (!pattern)
		die("st: can't open font %s\n", fontstr);

	if (fontsize > 1) {
		FcPatternDel(pattern, FC_PIXEL_SIZE);
		FcPatternDel(pattern, FC_SIZE);
		FcPatternAddDouble(pattern, FC_PIXEL_SIZE, (double)fontsize);
		usedfontsize = fontsize;
	} else {
		if (FcPatternGetDouble(pattern, FC_PIXEL_SIZE, 0, &fontval) ==
				FcResultMatch) {
			usedfontsize = fontval;
		} else if (FcPatternGetDouble(pattern, FC_SIZE, 0, &fontval) ==
				FcResultMatch) {
			usedfontsize = -1;
		} else {
			/*
			 * Default font size is 12, if none given. This is to
			 * have a known usedfontsize value.
			 */
			FcPatternAddDouble(pattern, FC_PIXEL_SIZE, 12);
			usedfontsize = 12;
		}
		defaultfontsize = usedfontsize;
	}

	FcConfigSubstitute(0, pattern, FcMatchPattern);
	FcDefaultSubstitute(pattern);

	if (wlloadfont(&dc.font, pattern))
		die("st: can't open font %s\n", fontstr);

	if (usedfontsize < 0) {
		FcPatternGetDouble(dc.font.pattern,
		                   FC_PIXEL_SIZE, 0, &fontval);
		usedfontsize = fontval;
		if (fontsize == 0)
			defaultfontsize = fontval;
	}

	/* Setting character width and height. */
	wl.cw = ceilf(dc.font.width * cwscale);
	wl.ch = ceilf(dc.font.height * chscale);

	FcPatternDel(pattern, FC_SLANT);
	FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ITALIC);
	if (wlloadfont(&dc.ifont, pattern))
		die("st: can't open font %s\n", fontstr);

	FcPatternDel(pattern, FC_WEIGHT);
	FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_BOLD);
	if (wlloadfont(&dc.ibfont, pattern))
		die("st: can't open font %s\n", fontstr);

	FcPatternDel(pattern, FC_SLANT);
	FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ROMAN);
	if (wlloadfont(&dc.bfont, pattern))
		die("st: can't open font %s\n", fontstr);

	FcPatternDestroy(pattern);
}

void
wlunloadfont(Font *f)
{
	wld_font_close(f->match);
	FcPatternDestroy(f->pattern);
	if (f->set)
		FcFontSetDestroy(f->set);
}

void
wlunloadfonts(void)
{
	/* Free the loaded fonts in the font cache.  */
	while (frclen > 0)
		wld_font_close(frc[--frclen].font);

	wlunloadfont(&dc.font);
	wlunloadfont(&dc.bfont);
	wlunloadfont(&dc.ifont);
	wlunloadfont(&dc.ibfont);
}

void
wlinit(int cols, int rows)
{
	struct wl_registry *registry;

	if (!(wl.dpy = wl_display_connect(NULL)))
		die("Can't open display\n");

	registry = wl_display_get_registry(wl.dpy);
	wl_registry_add_listener(registry, &reglistener, NULL);

	wl_display_dispatch(wl.dpy);
	wl_display_roundtrip(wl.dpy);

	wld.ctx = wld_wayland_create_context(wl.dpy, WLD_ANY);
	if (!wld.ctx)
		die("Can't create wayland context\n");
	wld.renderer = wld_create_renderer(wld.ctx);
	if (!wld.renderer)
		die("Can't create renderer\n");
	if (!wl.shm)
		die("Display has no SHM\n");
	if (!wl.seat)
		die("Display has no seat\n");
	if (!wl.datadevmanager)
		die("Display has no data device manager\n");

	wl_display_roundtrip(wl.dpy);

	wl.keyboard = wl_seat_get_keyboard(wl.seat);
	wl_keyboard_add_listener(wl.keyboard, &kbdlistener, NULL);
	wl.pointer = wl_seat_get_pointer(wl.seat);
	wl_pointer_add_listener(wl.pointer, &ptrlistener, NULL);
	wl.datadev = wl_data_device_manager_get_data_device(wl.datadevmanager,
			wl.seat);
	wl_data_device_add_listener(wl.datadev, &datadevlistener, NULL);

	/* font */
	if (!FcInit())
		die("Could not init fontconfig.\n");

	usedfont = (opt_font == NULL)? font : opt_font;
	wld.fontctx = wld_font_create_context();
	wlloadfonts(usedfont, 0);

	wlloadcols();
	wlloadcursor();

	wl.vis = 0;
	wl.h = 2 * borderpx + rows * wl.ch;
	wl.w = 2 * borderpx + cols * wl.cw;

	wl.surface = wl_compositor_create_surface(wl.cmp);
	wl_surface_add_listener(wl.surface, &surflistener, NULL);
	if(wl.xdgshell_v6)
	{
		wl.xdgsurface_v6 = zxdg_shell_v6_get_xdg_surface(wl.xdgshell_v6, wl.surface);
		zxdg_surface_v6_add_listener(wl.xdgsurface_v6, &surf_v6_listener, NULL);
		wl.xdgtoplevel = zxdg_surface_v6_get_toplevel(wl.xdgsurface_v6);
		zxdg_toplevel_v6_add_listener(wl.xdgtoplevel, &xdgtoplevellistener, NULL);
	}
	else if(wl.xdgshell)
	{
		xdg_shell_use_unstable_version(wl.xdgshell, XDG_SHELL_VERSION_CURRENT);
		wl.xdgsurface = xdg_shell_get_xdg_surface(wl.xdgshell, wl.surface);
		xdg_shell_add_listener(wl.xdgshell, &shell_listener, NULL);
		xdg_surface_add_listener(wl.xdgsurface, &xdgsurflistener, NULL);
	}
	else
	{
		/* die("could not initialize xdgshell"); */
	}

	wl.xkb.ctx = xkb_context_new(0);
	wlsettitle(NULL);
}

/*
 * TODO: Implement something like XftDrawGlyphFontSpec in wld, and then apply a
 * similar patch to ae1923d27533ff46400d93765e971558201ca1ee
 */

void
wldraws(char *s, Glyph base, int x, int y, int charlen, int bytelen)
{
	int winx = borderpx + x * wl.cw, winy = borderpx + y * wl.ch,
	    width = charlen * wl.cw, xp, i;
	int frcflags, charexists;
	int u8fl, u8fblen, u8cblen, doesexist;
	char *u8c, *u8fs;
	Rune unicodep;
	Font *font = &dc.font;
	FcResult fcres;
	FcPattern *fcpattern, *fontpattern;
	FcFontSet *fcsets[] = { NULL };
	FcCharSet *fccharset;
	uint32_t fg, bg, temp;
	int oneatatime;

	frcflags = FRC_NORMAL;

	if (base.mode & ATTR_ITALIC) {
		if (base.fg == defaultfg)
			base.fg = defaultitalic;
		font = &dc.ifont;
		frcflags = FRC_ITALIC;
	} else if ((base.mode & ATTR_ITALIC) && (base.mode & ATTR_BOLD)) {
		if (base.fg == defaultfg)
			base.fg = defaultitalic;
		font = &dc.ibfont;
		frcflags = FRC_ITALICBOLD;
	} else if (base.mode & ATTR_UNDERLINE) {
		if (base.fg == defaultfg)
			base.fg = defaultunderline;
	}

	if (IS_TRUECOL(base.fg)) {
		fg = base.fg;
	} else {
		fg = dc.col[base.fg];
	}

	if (IS_TRUECOL(base.bg)) {
		bg = base.bg | 0xff000000;
	} else {
		bg = dc.col[base.bg];
	}

	if (base.mode & ATTR_BOLD) {
		/*
		 * change basic system colors [0-7]
		 * to bright system colors [8-15]
		 */
		if (BETWEEN(base.fg, 0, 7) && !(base.mode & ATTR_FAINT))
			fg = dc.col[base.fg + 8];

		if (base.mode & ATTR_ITALIC) {
			font = &dc.ibfont;
			frcflags = FRC_ITALICBOLD;
		} else {
			font = &dc.bfont;
			frcflags = FRC_BOLD;
		}
	}

	if (IS_SET(MODE_REVERSE)) {
		if (fg == dc.col[defaultfg]) {
			fg = dc.col[defaultbg];
		} else {
			fg = ~(fg & 0xffffff);
		}

		if (bg == dc.col[defaultbg]) {
			bg = dc.col[defaultfg];
		} else {
			bg = ~(bg & 0xffffff);
		}
	}

	if (base.mode & ATTR_REVERSE) {
		temp = fg;
		fg = bg;
		bg = temp;
	}

	if (base.mode & ATTR_FAINT && !(base.mode & ATTR_BOLD)) {
		fg = (fg & (0xff << 24))
			| ((((fg >> 16) & 0xff) / 2) << 16)
			| ((((fg >> 8) & 0xff) / 2) << 8)
			| ((fg & 0xff) / 2);
	}

	if (base.mode & ATTR_BLINK && IS_SET(MODE_BLINK))
		fg = bg;

	if (base.mode & ATTR_INVISIBLE)
		fg = bg;

	/* Intelligent cleaning up of the borders. */
	if (x == 0) {
		wlclear(0, (y == 0)? 0 : winy, borderpx,
			((winy + win.ch >= borderpx + win.th)? wl.h :
			 (winy + wl.ch)));
	}
	if (winx + width >= borderpx + win.tw) {
		wlclear(winx + width, (y == 0)? 0 : winy, wl.w,
			((winy + win.ch >= borderpx + win.th)? wl.h :
			 (winy + wl.ch)));
	}
	if (y == 0)
		wlclear(winx, 0, winx + width, borderpx);
	if (winy + win.ch >= borderpx + win.th)
		wlclear(winx, winy + wl.ch, winx + width, wl.h);

	/* Clean up the region we want to draw to. */
	wld_fill_rectangle(wld.renderer, (bg & (term_alpha << 24)) | (bg & 0x00FFFFFF), winx, winy, width, wl.ch);
	for (xp = winx; bytelen > 0;) {
		/*
		 * Search for the range in the to be printed string of glyphs
		 * that are in the main font. Then print that range. If
		 * some glyph is found that is not in the font, do the
		 * fallback dance.
		 */
		u8fs = s;
		u8fblen = 0;
		u8fl = 0;
		oneatatime = font->width != wl.cw;
		for (;;) {
			u8c = s;
			u8cblen = utf8decode(s, &unicodep, UTF_SIZ);
			s += u8cblen;
			bytelen -= u8cblen;

			doesexist = wld_font_ensure_char(font->match, unicodep);
			if (doesexist) {
					u8fl++;
					u8fblen += u8cblen;
					if (!oneatatime && bytelen > 0)
							continue;
			}

			if (u8fl > 0) {
				wld_draw_text(wld.renderer,
						font->match, fg, xp,
						winy + font->ascent,
						u8fs, u8fblen, NULL);
				xp += wl.cw * u8fl;
			}
			break;
		}
		if (doesexist) {
			if (oneatatime)
				continue;
			break;
		}

		/* Search the font cache. */
		for (i = 0; i < frclen; i++) {
			charexists = wld_font_ensure_char(frc[i].font, unicodep);
			/* Everything correct. */
			if (charexists && frc[i].flags == frcflags)
				break;
			/* We got a default font for a not found glyph. */
			if (!charexists && frc[i].flags == frcflags \
					&& frc[i].unicodep == unicodep) {
				break;
			}
		}

		/* Nothing was found. */
		if (i >= frclen) {
			if (!font->set)
				font->set = FcFontSort(0, font->pattern,
				                       1, 0, &fcres);
			fcsets[0] = font->set;

			/*
			 * Nothing was found in the cache. Now use
			 * some dozen of Fontconfig calls to get the
			 * font for one single character.
			 *
			 * Xft and fontconfig are design failures.
			 */
			fcpattern = FcPatternDuplicate(font->pattern);
			fccharset = FcCharSetCreate();

			FcCharSetAddChar(fccharset, unicodep);
			FcPatternAddCharSet(fcpattern, FC_CHARSET,
					fccharset);
			FcPatternAddBool(fcpattern, FC_SCALABLE, 1);

			FcConfigSubstitute(0, fcpattern,
					FcMatchPattern);
			FcDefaultSubstitute(fcpattern);

			fontpattern = FcFontSetMatch(0, fcsets, 1,
					fcpattern, &fcres);

			/*
			 * Overwrite or create the new cache entry.
			 */
			if (frclen >= LEN(frc)) {
				frclen = LEN(frc) - 1;
				wld_font_close(frc[frclen].font);
				frc[frclen].unicodep = 0;
			}

			frc[frclen].font = wld_font_open_pattern(wld.fontctx,
					fontpattern);
			frc[frclen].flags = frcflags;
			frc[frclen].unicodep = unicodep;

			i = frclen;
			frclen++;

			FcPatternDestroy(fcpattern);
			FcCharSetDestroy(fccharset);
		}

		wld_draw_text(wld.renderer, frc[i].font, fg,
				xp, winy + frc[i].font->ascent,
				u8c, u8cblen, NULL);

		xp += wl.cw * wcwidth(unicodep);
	}

	if (base.mode & ATTR_UNDERLINE) {
		wld_fill_rectangle(wld.renderer, fg, winx, winy + font->ascent + 1,
				width, 1);
	}

	if (base.mode & ATTR_STRUCK) {
		wld_fill_rectangle(wld.renderer, fg, winx, winy + 2 * font->ascent / 3,
				width, 1);
	}
}

void
wldrawglyph(Glyph g, int x, int y)
{
	static char buf[UTF_SIZ];
	size_t len = utf8encode(g.u, buf);
	int width = g.mode & ATTR_WIDE ? 2 : 1;

	wldraws(buf, g, x, y, width, len);
}

void
wldrawcursor(int cx, int cy, Glyph g, int ox, int oy, Glyph og)
{
	/* remove the old cursor */
	if (selected(ox, oy))
		og.mode ^= ATTR_REVERSE;
	wldrawglyph(og, ox, oy);
	if (ox != cx || oy != cy) {
		wl_surface_damage(wl.surface, borderpx + ox * wl.cw,
				borderpx + oy * wl.ch, wl.cw, wl.ch);
	}

	if (IS_SET(MODE_HIDE))
		return;

	g.mode &= ATTR_BOLD|ATTR_ITALIC|ATTR_UNDERLINE|ATTR_STRUCK|ATTR_WIDE;

	uint32_t cs = dc.col[defaultcs] & (term_alpha << 24);
	/* draw the new one */
	if (IS_SET(MODE_FOCUSED)) {
		switch (wl.cursor) {
			case 7: /* st extension: snowman (U+2603) */
				g.u = 0x2603;
			case 0: /* Blinking Block */
			case 1: /* Blinking Block (Default) */
			case 2: /* Steady Block */
				if (IS_SET(MODE_REVERSE)) {
						g.mode |= ATTR_REVERSE;
						g.fg = defaultcs;
						g.bg = defaultfg;
					}
				wldrawglyph(g, cx, cy);
				break;
			case 3: /* Blinking Underline */
			case 4: /* Steady Underline */
				wld_fill_rectangle(wld.renderer, cs,
						borderpx + cx * wl.cw,
						borderpx + (cy + 1) * wl.ch - cursorthickness,
						wl.cw, cursorthickness);
				break;
			case 5: /* Blinking bar */
			case 6: /* Steady bar */
				wld_fill_rectangle(wld.renderer, cs,
						borderpx + cx * wl.cw,
						borderpx + cy * wl.ch,
						cursorthickness, wl.ch);
				break;
		}
	} else {
		wld_fill_rectangle(wld.renderer, cs,
				borderpx + cx * wl.cw,
				borderpx + cy * wl.ch,
				wl.cw - 1, 1);
		wld_fill_rectangle(wld.renderer, cs,
				borderpx + cx * wl.cw,
				borderpx + cy * wl.ch,
				1, wl.ch - 1);
		wld_fill_rectangle(wld.renderer, cs,
				borderpx + (cx + 1) * wl.cw - 1,
				borderpx + cy * wl.ch,
				1, wl.ch - 1);
		wld_fill_rectangle(wld.renderer, cs,
				borderpx + cx * wl.cw,
				borderpx + (cy + 1) * wl.ch - 1,
				wl.cw, 1);
	}
	wl_surface_damage(wl.surface, borderpx + cx * wl.cw,
			borderpx + cy * wl.ch, wl.cw, wl.ch);
}

void
xdrawcursor(int cx, int cy, Glyph g, int ox, int oy, Glyph og)
{
	wldrawcursor(cx, cy, g, ox, oy, og);
}

void
wlsettitle(char *title)
{
	DEFAULT(title, opt_title);
	DEFAULT(title, "st");
	if(wl.xdgsurface)
		xdg_surface_set_title(wl.xdgsurface, title);
	else if (wl.xdgtoplevel)
		zxdg_toplevel_v6_set_title(wl.xdgtoplevel, title);
}

void
xsettitle(char *title)
{
	wlsettitle(title);
}

void
wldrawline(Line line, int x1, int y, int x2)
{
	int ic, ib, x, ox;
	Glyph base, new;
	char buf[DRAW_BUF_SIZ];

	if (!IS_SET(MODE_VISIBLE))
		return;

	wl_surface_damage(wl.surface, 0, borderpx + y * wl.ch, wl.w, wl.ch);
	// XXX FIXME
	//wltermclear(0, y, term.col, y);
	base = line[0];
	ic = ib = ox = 0;
	for (x = x1; x < x2; x++) {
		new = line[x];
		if (new.mode == ATTR_WDUMMY)
			continue;
		if (selected(x, y))
			new.mode ^= ATTR_REVERSE;
		if (ib > 0 && (ATTRCMP(base, new)
				|| ib >= DRAW_BUF_SIZ-UTF_SIZ)) {
			wldraws(buf, base, ox, y, ic, ib);
			ic = ib = 0;
		}
		if (ib == 0) {
			ox = x;
			base = new;
		}

		ib += utf8encode(new.u, buf+ib);
		ic += (new.mode & ATTR_WIDE)? 2 : 1;
	}
	if (ib > 0)
		wldraws(buf, base, ox, y, ic, ib);
}

void
xdrawline(Line line, int x1, int y, int x2)
{
	wldrawline(line, x1, y, x2);
}

void
xsetpointermotion(int set)
{
}

int
wlstartdraw(void)
{
	return wld_set_target_buffer(wld.renderer, wld.buffer);
}

int
xstartdraw(void)
{
	return wlstartdraw();
}

void
wlfinishdraw(void)
{
	wl.framecb = wl_surface_frame(wl.surface);
	wl_callback_add_listener(wl.framecb, &framelistener, NULL);
	wld_flush(wld.renderer);
	wl_surface_attach(wl.surface, wl.buffer, 0, 0);
	wl_surface_commit(wl.surface);
	/* need to wait to destroy the old buffer until we commit the new
	 * buffer */
	if (wld.oldbuffer) {
		wld_buffer_unreference(wld.oldbuffer);
		wld.oldbuffer = 0;
	}
	needdraw = false;
}

void
xfinishdraw(void)
{
	wlfinishdraw();
}

void
xsetmode(int set, unsigned int flags)
{
	int mode = win.mode;
	MODBIT(win.mode, set, flags);
	if ((win.mode & MODE_REVERSE) != (mode & MODE_REVERSE))
		redraw();
}

int
xsetcursor(int cursor)
{
	DEFAULT(cursor, 1);
	if (!BETWEEN(cursor, 0, 6))
		return 1;
	win.cursor = cursor;
	return 0;
}

void
wlseturgency(int add)
{
	/* XXX: no urgency equivalent yet in wayland */
}

void
xbell(void)
{
}

int
match(uint mask, uint state)
{
	return mask == MOD_MASK_ANY || mask == (state & ~(ignoremod));
}

char*
kmap(xkb_keysym_t k, uint state)
{
	Key *kp;
	int i;

	/* Check for mapped keys out of X11 function keys. */
	for (i = 0; i < LEN(mappedkeys); i++) {
		if (mappedkeys[i] == k)
			break;
	}
	if (i == LEN(mappedkeys)) {
		if ((k & 0xFFFF) < 0xFD00)
			return NULL;
	}

	for (kp = key; kp < key + LEN(key); kp++) {
		if (kp->k != k)
			continue;

		if (!match(kp->mask, state))
			continue;

		if (IS_SET(MODE_APPKEYPAD) ? kp->appkey < 0 : kp->appkey > 0)
			continue;
		if (IS_SET(MODE_NUMLOCK) && kp->appkey == 2)
			continue;

		if (IS_SET(MODE_APPCURSOR) ? kp->appcursor < 0 : kp->appcursor > 0)
			continue;

		return kp->s;
	}

	return NULL;
}

void
cresize(int width, int height)
{
	int col, row;

	if (width != 0)
		wl.w = width;
	if (height != 0)
		wl.h = height;

	col = (wl.w - 2 * borderpx) / wl.cw;
	row = (wl.h - 2 * borderpx) / wl.ch;

	tresize(col, row);
	wlresize(col, row);
	ttyresize(win.tw, win.th);
}

void
regglobal(void *data, struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version)
{
	if(getenv("WTERM_DEBUG"))
		printf("interface %s\n", interface);

	if (strcmp(interface, "wl_compositor") == 0) {
		wl.cmp = wl_registry_bind(registry, name,
				&wl_compositor_interface, 3);
	} else if (strcmp(interface, "zxdg_shell_v6") == 0) {
		wl.xdgshell_v6 = wl_registry_bind(registry, name,
				&zxdg_shell_v6_interface, 1);
		zxdg_shell_v6_add_listener(wl.xdgshell_v6, &shell_v6_listener, NULL);
	} else if (strcmp(interface, "wl_shm") == 0) {
		wl.shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (strcmp(interface, "wl_seat") == 0) {
		wl.seat = wl_registry_bind(registry, name,
				&wl_seat_interface, 4);
	} else if (strcmp(interface, "wl_data_device_manager") == 0) {
		wl.datadevmanager = wl_registry_bind(registry, name,
				&wl_data_device_manager_interface, 1);
	} else if (strcmp(interface, "wl_output") == 0) {
		/* bind to outputs so we can get surface enter events */
		wl_registry_bind(registry, name, &wl_output_interface, 2);
	} else if (strcmp(interface, "xdg_shell") == 0) {
		wl.xdgshell = wl_registry_bind(registry, name,
				&xdg_shell_interface, 1);
	}
}

void
regglobalremove(void *data, struct wl_registry *registry, uint32_t name)
{
}

void
surfenter(void *data, struct wl_surface *surface, struct wl_output *output)
{
	wl.vis++;
	if (!IS_SET(MODE_VISIBLE))
		MODBIT(win.mode, 1, MODE_VISIBLE);
}

void
surfleave(void *data, struct wl_surface *surface, struct wl_output *output)
{
	if (--wl.vis == 0)
		MODBIT(win.mode, 0, MODE_VISIBLE);
}

void
popupsurfenter(void *data, struct wl_surface *surface, struct wl_output *output)
{
}

void
popupsurfleave(void *data, struct wl_surface *surface, struct wl_output *output)
{
}

void
framedone(void *data, struct wl_callback *callback, uint32_t msecs)
{
	wl_callback_destroy(callback);
	wl.framecb = NULL;
	draw();
}

void
kbdkeymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd,
		uint32_t size)
{
	char *string;

	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		close(fd);
		return;
	}

	string = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);

	if (string == MAP_FAILED) {
		close(fd);
		return;
	}

	wl.xkb.keymap = xkb_keymap_new_from_string(wl.xkb.ctx, string,
			XKB_KEYMAP_FORMAT_TEXT_V1, 0);
	munmap(string, size);
	close(fd);
	wl.xkb.state = xkb_state_new(wl.xkb.keymap);

	wl.xkb.ctrl = xkb_keymap_mod_get_index(wl.xkb.keymap, XKB_MOD_NAME_CTRL);
	wl.xkb.alt = xkb_keymap_mod_get_index(wl.xkb.keymap, XKB_MOD_NAME_ALT);
	wl.xkb.shift = xkb_keymap_mod_get_index(wl.xkb.keymap, XKB_MOD_NAME_SHIFT);
	wl.xkb.logo = xkb_keymap_mod_get_index(wl.xkb.keymap, XKB_MOD_NAME_LOGO);

	wl.xkb.mods = 0;
}

void
kbdenter(void *data, struct wl_keyboard *keyboard, uint32_t serial,
		struct wl_surface *surface, struct wl_array *keys)
{
	MODBIT(win.mode, 1, MODE_FOCUSED);
	if (IS_SET(MODE_FOCUS))
		ttywrite("\033[I", 3, 0);
	/* need to redraw the cursor */
	needdraw = true;
}

void
kbdleave(void *data, struct wl_keyboard *keyboard, uint32_t serial,
		struct wl_surface *surface)
{
	/* selection offers are invalidated when we lose keyboard focus */
	wl.seloffer = NULL;
	MODBIT(win.mode, 0, MODE_FOCUSED);
	if (IS_SET(MODE_FOCUS))
		ttywrite("\033[O", 3, 0);
	/* need to redraw the cursor */
	needdraw = true;
	/* disable key repeat */
	repeat.len = 0;
}

void
kbdkey(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time,
		uint32_t key, uint32_t state)
{
	xkb_keysym_t ksym;
	char buf[32], *str;
	int len;
	Rune c;
	Shortcut *bp;

	if (IS_SET(MODE_KBDLOCK))
		return;

	if (state == WL_KEYBOARD_KEY_STATE_RELEASED) {
		if (repeat.key == key)
			repeat.len = 0;
		return;
	}

	ksym = xkb_state_key_get_one_sym(wl.xkb.state, key + 8);
	len = xkb_keysym_to_utf8(ksym, buf, sizeof buf);
	if (len > 0)
	    --len;

	/* 1. shortcuts */
	for (bp = shortcuts; bp < shortcuts + LEN(shortcuts); bp++) {
		if (ksym == bp->keysym && match(bp->mod, wl.xkb.mods)) {
			bp->func(&(bp->arg));
			return;
		}
	}

	/* 2. custom keys from config.h */
	if ((str = kmap(ksym, wl.xkb.mods))) {
		len = strlen(str);
		goto send;
	}

	/* 3. composed string from input method */
	if (len == 0)
		return;
	if (len == 1 && wl.xkb.mods & MOD_MASK_ALT) {
		if (IS_SET(MODE_8BIT)) {
			if (*buf < 0177) {
				c = *buf | 0x80;
				len = utf8encode(c, buf);
			}
		} else {
			buf[1] = buf[0];
			buf[0] = '\033';
			len = 2;
		}
	}
	/* convert character to control character */
	else if (len == 1 && wl.xkb.mods & MOD_MASK_CTRL) {
		if ((*buf >= '@' && *buf < '\177') || *buf == ' ')
			*buf &= 0x1F;
		else if (*buf == '2') *buf = '\000';
		else if (*buf >= '3' && *buf <= '7')
			*buf -= ('3' - '\033');
		else if (*buf == '8') *buf = '\177';
		else if (*buf == '/') *buf = '_' & 0x1F;
	}

	str = buf;

send:
	memcpy(repeat.str, str, len);
	repeat.key = key;
	repeat.len = len;
	repeat.started = false;
	clock_gettime(CLOCK_MONOTONIC, &repeat.last);
	ttywrite(str, len, 1);
}

void
kbdmodifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial,
		uint32_t dep, uint32_t lat, uint32_t lck, uint32_t group)
{
	xkb_mod_mask_t mod_mask;

	xkb_state_update_mask(wl.xkb.state, dep, lat, lck, group, 0, 0);

	mod_mask = xkb_state_serialize_mods(wl.xkb.state, XKB_STATE_MODS_EFFECTIVE);
	wl.xkb.mods = 0;

	if (mod_mask & (1 << wl.xkb.ctrl))
		wl.xkb.mods |= MOD_MASK_CTRL;
	if (mod_mask & (1 << wl.xkb.alt))
		wl.xkb.mods |= MOD_MASK_ALT;
	if (mod_mask & (1 << wl.xkb.shift))
		wl.xkb.mods |= MOD_MASK_SHIFT;
	if (mod_mask & (1 << wl.xkb.logo))
		wl.xkb.mods |= MOD_MASK_LOGO;
}

void
kbdrepeatinfo(void *data, struct wl_keyboard *keyboard, int32_t rate,
		int32_t delay)
{
	keyrepeatdelay = delay;
	keyrepeatinterval = 1000 / rate;
}

void
ptrenter(void *data, struct wl_pointer *pointer, uint32_t serial,
		struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y)
{
	struct wl_cursor_image *img = cursor.cursor->images[0];
	struct wl_buffer *buffer;

	wl_pointer_set_cursor(pointer, serial, cursor.surface,
			img->hotspot_x, img->hotspot_y);
	buffer = wl_cursor_image_get_buffer(img);
	wl_surface_attach(cursor.surface, buffer, 0, 0);
	wl_surface_damage(cursor.surface, 0, 0, img->width, img->height);
	wl_surface_commit(cursor.surface);
}

void
ptrleave(void *data, struct wl_pointer *pointer, uint32_t serial,
		struct wl_surface *surface)
{
}

void
ptrmotion(void *data, struct wl_pointer * pointer, uint32_t serial,
		wl_fixed_t x, wl_fixed_t y)
{
	if (IS_SET(MODE_MOUSE)) {
		wlmousereportmotion(x, y);
		return;
	}

	wl.px = wl_fixed_to_int(x);
	wl.py = wl_fixed_to_int(y);

	mousesel(0, serial);
}

void
ptrbutton(void * data, struct wl_pointer * pointer, uint32_t serial,
		uint32_t time, uint32_t button, uint32_t state)
{
	Mousekey *mk;
	int snap;

	if (IS_SET(MODE_MOUSE) && !(wl.xkb.mods & forceselmod)) {
		wlmousereportbutton(button, state);
		return;
	}

	switch (state) {
	case WL_POINTER_BUTTON_STATE_RELEASED:
		if (button == BTN_MIDDLE) {
			selpaste(NULL);
		} else if (button == BTN_LEFT) {
			mousesel(1, serial);
		}
		break;

	case WL_POINTER_BUTTON_STATE_PRESSED:
		for (mk = mshortcuts; mk < mshortcuts + LEN(mshortcuts); mk++) {
			if (button == mk->b && match(mk->mask, wl.xkb.mods)) {
				ttywrite(mk->s, strlen(mk->s), 1);
				return;
			}
		}

		if (button == BTN_LEFT) {
			/*
			 * If the user clicks below predefined timeouts
			 * specific snapping behaviour is exposed.
			 */
			if (time - wsel.tclick2 <= tripleclicktimeout) {
				snap = SNAP_LINE;
			} else if (time - wsel.tclick1 <= doubleclicktimeout) {
				snap = SNAP_WORD;
			} else {
				snap = 0;
			}
			wsel.tclick2 = wsel.tclick1;
			wsel.tclick1 = time;

			selstart(x2col(wl.px), y2row(wl.py), snap);
		}
		break;
	}
}

void
ptraxis(void * data, struct wl_pointer * pointer, uint32_t time, uint32_t axis,
		wl_fixed_t value)
{
	Axiskey *ak;
	int dir = value > 0 ? +1 : -1;

	if (IS_SET(MODE_MOUSE) && !(wl.xkb.mods & forceselmod)) {
		wlmousereportaxis(axis, value);
		return;
	}

	for (ak = ashortcuts; ak < ashortcuts + LEN(ashortcuts); ak++) {
		if (axis == ak->axis && dir == ak->dir
				&& match(ak->mask, wl.xkb.mods)) {
			ttywrite(ak->s, strlen(ak->s), 1);
			return;
		}
	}
}

void
xdgshellv6ping(void *data, struct zxdg_shell_v6 *shell, uint32_t serial)
{
	zxdg_shell_v6_pong(shell, serial);
}

void
xdgsurfv6configure(void *data, struct zxdg_surface_v6 *surf, uint32_t serial)
{
	zxdg_surface_v6_ack_configure(surf, serial);
}

void
xdgshellping(void * data, struct xdg_shell * shell, uint32_t serial)
{
	xdg_shell_pong(shell, serial);
	if(getenv("WTERM_DEBUG")) printf("serial=%d\n", serial);
}

static void close_shell_and_exit()
{
	ttyhangup();
	exit(0);
}

void
xdgtopclose(void * data, struct zxdg_toplevel_v6 * top)
{
	close_shell_and_exit();
}

void
xdgsurfclose(void * data, struct xdg_surface * surf)
{
	close_shell_and_exit();
}

void
xdgpopupdone(void * data, struct xdg_popup * pop)
{
}

void
datadevoffer(void *data, struct wl_data_device *datadev,
		struct wl_data_offer *offer)
{
	wl_data_offer_add_listener(offer, &dataofferlistener, NULL);
}

void
datadeventer(void *data, struct wl_data_device *datadev, uint32_t serial,
		struct wl_surface *surf, wl_fixed_t x, wl_fixed_t y,
		struct wl_data_offer *offer)
{
}

void
datadevleave(void *data, struct wl_data_device *datadev)
{
}

void
datadevmotion(void *data, struct wl_data_device *datadev, uint32_t time,
		wl_fixed_t x, wl_fixed_t y)
{
}

void
datadevdrop(void *data, struct wl_data_device *datadev)
{
}

void
datadevselection(void *data, struct wl_data_device *datadev,
		struct wl_data_offer *offer)
{
	if (offer && (uintptr_t) wl_data_offer_get_user_data(offer) == 1)
		wl.seloffer = offer;
	else
		wl.seloffer = NULL;
}

void
dataofferoffer(void *data, struct wl_data_offer *offer, const char *mimetype)
{
	/* mark the offer as usable if it supports plain text */
	if (strncmp(mimetype, "text/plain", 10) == 0)
		wl_data_offer_set_user_data(offer, (void *)(uintptr_t) 1);
}

void
datasrctarget(void *data, struct wl_data_source *source, const char *mimetype)
{
}

void
datasrcsend(void *data, struct wl_data_source *source, const char *mimetype,
		int32_t fd)
{
	char *buf = wsel.primary;
	int len = strlen(wsel.primary);
	ssize_t ret;
	while ((ret = write(fd, buf, MIN(len, BUFSIZ))) > 0) {
		len -= ret;
		buf += ret;
	}
	close(fd);
}

void
datasrccancelled(void *data, struct wl_data_source *source)
{
	if (wsel.source == source) {
		wsel.source = NULL;
		selclear();
	}
	wl_data_source_destroy(source);
}

void
xdgsurfconfigure(void * data, struct xdg_surface *  surf, int32_t w, int32_t h, struct wl_array * states, uint32_t  serial)
{
	xdg_surface_ack_configure(surf, serial);
	xdg_surface_set_app_id(surf, opt_class ? opt_class : termname);
	/*
	   wl.popupsurface = wl_compositor_create_surface(wl.cmp);
	   wl_surface_add_listener(wl.popupsurface, &popupsurflistener, NULL);

	   wl.xdgpopup = xdg_shell_get_xdg_popup(wl.xdgshell, wl.popupsurface, wl.surface, wl.seat, serial, 100, 100);
	   xdg_popup_add_listener(wl.xdgpopup, &xdgpopuplistener, NULL);
	   */
	if (wl.h == h && wl.w == w) return;
	cresize(w,h);
}


void
xdgtopconfigure(void * data, struct zxdg_toplevel_v6 * top, int32_t w, int32_t h, struct wl_array * states)
{
	zxdg_toplevel_v6_set_app_id(top, opt_class ? opt_class : termname);
	if (wl.w == w && wl.h == h) return;
	cresize(w,h);
}

void
run(void)
{
	fd_set rfd;
	int wlfd = wl_display_get_fd(wl.dpy), blinkset = 0;
	int ttyfd;
	struct timespec drawtimeout, *tv = NULL, now, last, lastblink;
	ulong msecs;

	ttyfd = ttynew(opt_line, shell, opt_io, opt_cmd);
	/* Trigger initial configure */
	wl_surface_commit(wl.surface);
	wl_display_roundtrip(wl.dpy);

	clock_gettime(CLOCK_MONOTONIC, &last);
	lastblink = last;

	for (;;) {
		FD_ZERO(&rfd);
		FD_SET(ttyfd, &rfd);
		FD_SET(wlfd, &rfd);

		if (pselect(MAX(wlfd, ttyfd)+1, &rfd, NULL, NULL, tv, NULL) < 0) {
			if (errno == EINTR)
				continue;
			die("select failed: %s\n", strerror(errno));
		}

		if (FD_ISSET(ttyfd, &rfd)) {
			ttyread();
			if (blinktimeout) {
				blinkset = tattrset(ATTR_BLINK);
				if (!blinkset)
					MODBIT(win.mode, 0, MODE_BLINK);
			}
			needdraw = true;
		}

		if (FD_ISSET(wlfd, &rfd)) {
			if (wl_display_dispatch(wl.dpy) == -1)
				die("Connection error\n");
		}

		clock_gettime(CLOCK_MONOTONIC, &now);
		msecs = -1;

		if (blinkset && blinktimeout) {
			if (TIMEDIFF(now, lastblink) >= blinktimeout) {
				tsetdirtattr(ATTR_BLINK);
				win.mode ^= MODE_BLINK;
				lastblink = now;
			} else {
				msecs = MIN(msecs, blinktimeout - \
						TIMEDIFF(now, lastblink));
			}
		}
		if (repeat.len > 0) {
			if (TIMEDIFF(now, repeat.last) >= \
				(repeat.started ? keyrepeatinterval : \
					keyrepeatdelay)) {
				repeat.started = true;
				repeat.last = now;
				ttywrite(repeat.str, repeat.len, 1);
			} else {
				msecs = MIN(msecs, (repeat.started ? \
					keyrepeatinterval : keyrepeatdelay) - \
					TIMEDIFF(now, repeat.last));
			}
		}

		if (needdraw) {
			if (!wl.framecb) {
				draw();
			}
		}

		if (msecs == -1) {
			tv = NULL;
		} else {
			drawtimeout.tv_nsec = 1E6 * msecs;
			drawtimeout.tv_sec = 0;
			tv = &drawtimeout;
		}

		wl_display_dispatch_pending(wl.dpy);
		wl_display_flush(wl.dpy);
	}
}

void
usage(void)
{
	die("%s " VERSION " (c) 2010-2015 st engineers\n"
	"usage: st [-a] [-v] [-c class] [-f font] [-g geometry] [-o file]\n"
	"          [-i] [-t title] [-T title] [-w windowid] [-e command ...]"
	" [command ...]\n"
	"       st [-a] [-v] [-c class] [-f font] [-g geometry] [-o file]\n"
	"          [-i] [-t title] [-T title] [-w windowid] [-l line]"
	" [stty_args ...]\n",
	argv0);
}

int
main(int argc, char *argv[])
{
	ARGBEGIN {
	case 'a':
		allowaltscreen = 0;
		break;
	case 'c':
		opt_class = EARGF(usage());
		break;
	case 'e':
		if (argc > 0)
			--argc, ++argv;
		goto run;
	case 'f':
		opt_font = EARGF(usage());
		break;
	case 'o':
		opt_io = EARGF(usage());
		break;
	case 'l':
		opt_line = EARGF(usage());
		break;
	case 't':
	case 'T':
		opt_title = EARGF(usage());
		break;
	case 'w':
		opt_embed = EARGF(usage());
		break;
	case 'v':
	default:
		usage();
	} ARGEND;

run:
	if (argc > 0) {
		/* eat all remaining arguments */
		opt_cmd = argv;
		if (!opt_title && !opt_line)
			opt_title = basename(xstrdup(argv[0]));
	}
	setlocale(LC_CTYPE, "");
	tnew(cols, rows);
	wlinit(cols, rows);
	selinit();
	run();

	return 0;
}
