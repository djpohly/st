/* See LICENSE for license details. */

/* X modifiers */
#define XK_ANY_MOD    UINT_MAX
#define XK_NO_MOD     0
#define XK_SWITCH_MOD (1<<13)

void draw(void);
void drawregion(int, int, int, int);

void xbell(void);
void xclipcopy(void);
void xclippaste(void);
void xloadcols(void);
int xsetcolorname(int, const char *);
void xloadfonts(char *, double);
void xsetenv(void);
void xsettitle(char *);
void xsetpointermotion(int);
void xunloadfonts(void);
void xresize(int, int);
void xselpaste(void);
unsigned long xwinid(void);
void xsetsel(char *, Time);
void zoom(const Arg *);
void zoomabs(const Arg *);
void zoomreset(const Arg *);
