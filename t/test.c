#include <check.h>
#include <stdio.h>
#include <stdlib.h>

#include "../st.h"

#define TEST(name, ...) START_TEST((name)) { __VA_ARGS__ } END_TEST
#include SUITE
#undef TEST

int
main(void)
{
	Suite *s = suite_create(SUITE);
	TCase *tc = tcase_create("default");
	SRunner *r = srunner_create(NULL);

#define TEST(name, ...) tcase_add_test(tc, name);
#include SUITE
#undef TEST
	suite_add_tcase(s, tc);
	srunner_add_suite(r, s);

	srunner_set_tap(r, "-");
	srunner_run_all(r, CK_NORMAL);
	int num_failed = srunner_ntests_failed(r);

	srunner_free(r);
	exit(!!num_failed);
}
