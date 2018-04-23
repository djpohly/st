TEST(t_dims,
	tnew(80, 24);
	/*ck_assert_int_eq(term.col, 80);*/
	/*ck_assert_int_eq(term.row, 24);*/
)

TEST(t_dims_2,
	tnew(120, 12);
	/*ck_assert_int_eq(term.col, 120);*/
	/*ck_assert_int_eq(term.row, 12);*/
)

TEST(t_dims_3,
	tnew(12, 120);
	/*ck_assert_int_eq(term.col, 12);*/
	/*ck_assert_int_eq(term.row, 120);*/
)
