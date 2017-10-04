#include <stdio.h>
#include <stdlib.h>
#include <check.h>

#include "test.h"
#include "../st.h"

START_TEST(t_cursor_1)
{
	tnew(80, 24, 1, NULL);
	ck_assert_int_eq(term.cursor, 1);
}
END_TEST

START_TEST(t_cursor_3)
{
	tnew(80, 24, 3, NULL);
	ck_assert_int_eq(term.cursor, 3);
}
END_TEST

const TFun TESTS[] = {
	t_cursor_1,
	t_cursor_3,
};
const int NTESTS = sizeof(TESTS)/sizeof(*TESTS);
const char *const TESTSUITE = "tnew";
