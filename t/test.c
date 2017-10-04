#include <check.h>
#include <stdlib.h>

extern const TFun TESTS[];
extern const int NTESTS;
extern const char *const TESTSUITE;

int
main(void)
{
	Suite *s = suite_create(TESTSUITE);
	TCase *tc = tcase_create("default");
	SRunner *r = srunner_create(NULL);

	int i;
	for (i = 0; i < NTESTS; i++)
		tcase_add_test(tc, TESTS[i]);
	suite_add_tcase(s, tc);
	srunner_add_suite(r, s);

	srunner_set_tap(r, "-");
	srunner_run_all(r, CK_NORMAL);
	int num_failed = srunner_ntests_failed(r);

	srunner_free(r);
	exit(!!num_failed);
}
