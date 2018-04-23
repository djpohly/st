MOCK(0, int, 1, xstartdraw)
MOCK(0, void,, xfinishdraw)
MOCK(4, void,, xdrawline, Line, l, int, x1, int, y1, int, x2)
MOCK(6, void,, xdrawcursor, int, cx, int, cy, Glyph, g, int, ox, int, oy, Glyph, og)

TEST(t_dims,
	tnew(80, 24);
	draw();
	ck_assert_int_eq(num_calls(xdrawline), 24);
	struct __args_xdrawline *p = pop_call(xdrawline);
	ck_assert_ptr_nonnull(p);
	ck_assert_int_eq(p->x2 - p->x1, 80);
)
