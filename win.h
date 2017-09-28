/* See LICENSE for license details. */

/* Functions from x.c used by st.c */
void draw(void);

void selscroll(int, int);
int selected(int x, int y);
void xbell(void);
void xclipcopy(void);
void xloadcols(void);
int xsetcolorname(int, const char *);
void xsettitle(char *);
void xsetpointermotion(int);
void xsetsel(char *);

extern Selection sel;
