#include <check.h>
#include <stdio.h>
#include <stdlib.h>

#include "../st.h"


/* Preprocessing for fun and profit (enables recursive macros up to depth 32) */
/* Adapted from http://jhnet.co.uk/articles/cpp_magic */

/* Manipulating expansion passes */
#define EMPTY()
#define DEFER1(m) m EMPTY()
#define DEFER2(m) m EMPTY EMPTY()()
#define EVAL1(...) __VA_ARGS__
#define EVAL2(...) EVAL1(EVAL1(__VA_ARGS__))
#define EVAL4(...) EVAL2(EVAL2(__VA_ARGS__))
#define EVAL8(...) EVAL4(EVAL4(__VA_ARGS__))
#define EVAL16(...) EVAL8(EVAL8(__VA_ARGS__))
#define EVAL(...) EVAL16(EVAL16(__VA_ARGS__))

/* Helpers */
#define FIRST(x, ...) x
#define SECOND(x, y, ...) y
#define CAT(x, y) x##y

/* Conditionals */
#define IS_ONEARG(...) SECOND(__VA_ARGS__, 1)
#define BOOL(x) IS_ONEARG(CAT(_BOOL_, x))
#define _BOOL_0 0,0

#define _END_ARGS_() 0
#define HAS_ARGS(...) BOOL(FIRST(_END_ARGS_ __VA_ARGS__)())

#define _IF_0(...)
#define _IF_1(...) __VA_ARGS__
#define IF_ARGS(...) EVAL1(DEFER1(CAT)(_IF_, HAS_ARGS(__VA_ARGS__)))


/* "Functions" that can be used by tests */

#define num_calls(fn) ({ \
	struct __args_##fn *p; \
	int i = 0; \
	for (p = __head_##fn; p; p = p->__next) \
		i++; \
	i; })
/* Returns a pointer that should be freed by the user */
#define pop_call(fn) ({ \
	struct __args_##fn *p = __head_##fn; \
	if (p) \
		__head_##fn = p->__next; \
	p; })
#define clear_calls(fn) do { \
		while (__head_##fn) { \
			struct __args_##fn *p = __head_##fn; \
			__head_##fn = __head_##fn->__next; \
			free(p); \
		} \
	} while (0)


/* Definitions for setting up mock functions */

#define ARGS(...) IF_ARGS(__VA_ARGS__)(EVAL(_ARGS(__VA_ARGS__)))
#define _ARGS(x, y, ...) x y IF_ARGS(__VA_ARGS__)(, DEFER2(__ARGS)()(__VA_ARGS__))
#define __ARGS() _ARGS

#define STRUCT(...) IF_ARGS(__VA_ARGS__)(EVAL(_STRUCT(__VA_ARGS__)))
#define _STRUCT(x, y, ...) x y; IF_ARGS(__VA_ARGS__)(DEFER2(__STRUCT)()(__VA_ARGS__))
#define __STRUCT() _STRUCT

#define VALS(...) IF_ARGS(__VA_ARGS__)(EVAL(_VALS(__VA_ARGS__)))
#define _VALS(x, y, ...) y IF_ARGS(__VA_ARGS__)(, DEFER2(__VALS)()(__VA_ARGS__))
#define __VALS() _VALS

#define MOCK(n, rt, rv, name, ...) \
	struct __args_##name { struct __args_##name *__next; STRUCT(__VA_ARGS__) }; \
	static struct __args_##name *__head_##name; \
	rt name(ARGS(__VA_ARGS__)) { \
		struct __args_##name *__p = malloc(sizeof(*__p)); \
		*__p = (struct __args_##name) { __head_##name, VALS(__VA_ARGS__) }; \
		__head_##name = __p; \
		return rv; \
	}
#define TEST(name, ...) START_TEST((name)) { __VA_ARGS__ } END_TEST
#include SUITE
#undef TEST
#undef MOCK


int
main(void)
{
	Suite *s = suite_create(SUITE);
	TCase *tc = tcase_create("default");
	SRunner *r = srunner_create(NULL);

#define MOCK(...)
#define TEST(name, ...) tcase_add_test(tc, name);
#include SUITE
#undef TEST
#undef MOCK
	suite_add_tcase(s, tc);
	srunner_add_suite(r, s);

	srunner_set_tap(r, "-");
	srunner_run_all(r, CK_NORMAL);
	int num_failed = srunner_ntests_failed(r);

	srunner_free(r);
	exit(!!num_failed);
}
