/* See LICENSE for license details. */

/* X modifiers */
#define XK_ANY_MOD    UINT_MAX
#define XK_NO_MOD     0
#define XK_SWITCH_MOD (1<<13)

void draw(void);
void drawregion(int, int, int, int);

void clipcopy(const Arg *);
void clippaste(const Arg *);
void iso14755(const Arg *);
void selpaste(const Arg *);
void xbell(void);
void xclipcopy(void);
void xloadcols(void);
int xsetcolorname(int, const char *);
void xsetenv(void);
void xsettitle(char *);
void xsetpointermotion(int);
void xsetsel(char *);
void zoom(const Arg *);
void zoomabs(const Arg *);
void zoomreset(const Arg *);
