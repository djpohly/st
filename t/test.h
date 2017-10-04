#define START_SUITE(name) \
	Suite * \
	testsuite_##name(void) \
	{ \
		Suite *s; \
		TCase *tc_core; \
		s = suite_create(#name); \
		tc_core = tcase_create("default");
#define ADD_TEST(fn) \
		tcase_add_test(tc_core, (fn))
#define END_SUITE \
		suite_add_tcase(s, tc_core); \
		return s; \
	}

#define START_RUNNER_TAP \
	int \
	__wrap_main(void) \
	{ \
		int num_failed; \
		SRunner *r = srunner_create(NULL); \
		srunner_set_tap(r, "-");
#define ADD_SUITE(name) \
		srunner_add_suite(r, testsuite_##name());
#define END_RUNNER \
		srunner_run_all(r, CK_NORMAL); \
		num_failed = srunner_ntests_failed(r); \
		srunner_free(r); \
		return !!num_failed; \
	}
