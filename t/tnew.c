MOCK(0, int, 1, xstartdraw)
MOCK(0, void,, xfinishdraw)
MOCK(4, void,, xdrawline, Line, l, int, x1, int, y1, int, x2)
MOCK(6, void,, xdrawcursor, int, cx, int, cy, Glyph, g, int, ox, int, oy, Glyph, og)

TEST(t_dims,
	tnew(80, 24);
	draw();
	fprintf(stderr, "xstartdraw: %d\n", num_calls(xstartdraw));
	fprintf(stderr, "xdrawline: %d\n", num_calls(xdrawline));
	fprintf(stderr, "xdrawcursor: %d\n", num_calls(xdrawcursor));
	fprintf(stderr, "xfinishdraw: %d\n", num_calls(xfinishdraw));
	struct __args_xdrawcursor *p = pop_call(xdrawcursor);
	fprintf(stderr, "U+%04x(%d/%d)\n", p->g.u, p->g.fg, p->g.bg);
	fprintf(stderr, "U+%04x(%d/%d)\n", p->og.u, p->og.fg, p->og.bg);
	free(p);
	p = pop_call(xdrawcursor);
	fprintf(stderr, "%p\n", p);
	free(p);
	clear_calls(xdrawcursor);
	clear_calls(xstartdraw);
	clear_calls(xfinishdraw);
	clear_calls(xdrawline);
)
