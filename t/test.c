#include <check.h>
#include <stdio.h>
#include <stdlib.h>

#include "../st.h"

#define num_calls(fn) ({ struct __args_##fn *p; int i = 0; for (p = __head_##fn; p; p = p->__next) i++; i; })
#define pop_call(fn) ({ struct __args_##fn *p = __head_##fn; if (p) __head_##fn = p->__next; p; })

#define PRI_int "%d"
#define VAL_int(x) (x)
#define PRI_Rune "U+%06x"
#define VAL_Rune(x) (x)
#define PRI_Glyph_ "U+%04x(%d/%d)"
#define VAL_Glyph_(g) ((g).u), ((g).fg), ((g).bg)
#define PRI_Line "%p"
#define VAL_Line(l) (l)

#define ARGS0() void
#define ARGS1(at, an) at an
#define ARGS2(at, an, ...) at an, ARGS1(__VA_ARGS__)
#define ARGS3(at, an, ...) at an, ARGS2(__VA_ARGS__)
#define ARGS4(at, an, ...) at an, ARGS3(__VA_ARGS__)
#define ARGS5(at, an, ...) at an, ARGS4(__VA_ARGS__)
#define ARGS6(at, an, ...) at an, ARGS5(__VA_ARGS__)
#define PRI0()
#define PRI1(at, an) fprintf(stderr, ", " PRI_##at, VAL_##at(an))
#define PRI2(at, an, ...) PRI1(at, an); PRI1(__VA_ARGS__);
#define PRI3(at, an, ...) PRI1(at, an); PRI2(__VA_ARGS__);
#define PRI4(at, an, ...) PRI1(at, an); PRI3(__VA_ARGS__);
#define PRI5(at, an, ...) PRI1(at, an); PRI4(__VA_ARGS__);
#define PRI6(at, an, ...) PRI1(at, an); PRI5(__VA_ARGS__);
#define STR0()
#define STR1(at, an) at an
#define STR2(at, an, ...) at an; STR1(__VA_ARGS__)
#define STR3(at, an, ...) at an; STR2(__VA_ARGS__)
#define STR4(at, an, ...) at an; STR3(__VA_ARGS__)
#define STR5(at, an, ...) at an; STR4(__VA_ARGS__)
#define STR6(at, an, ...) at an; STR5(__VA_ARGS__)
#define VAL0()
#define VAL1(at, an) an
#define VAL2(at, an, ...) an, VAL1(__VA_ARGS__)
#define VAL3(at, an, ...) an, VAL2(__VA_ARGS__)
#define VAL4(at, an, ...) an, VAL3(__VA_ARGS__)
#define VAL5(at, an, ...) an, VAL4(__VA_ARGS__)
#define VAL6(at, an, ...) an, VAL5(__VA_ARGS__)

#define MOCK(n, rt, rv, name, ...) \
	struct __args_##name { struct __args_##name *__next; STR##n(__VA_ARGS__); }; \
	/* XXX FREEME! */ static struct __args_##name *__head_##name; \
	rt name(ARGS##n(__VA_ARGS__)) { \
		struct __args_##name *__p = malloc(sizeof(*__p)); \
		ck_assert_ptr_nonnull(__p); \
		*__p = (struct __args_##name) { __head_##name, VAL##n(__VA_ARGS__) }; \
		__head_##name = __p; \
		fprintf(stderr, "%s", #name); \
		PRI##n(__VA_ARGS__); \
		fprintf(stderr, "\n"); \
		return rv; \
	}
#define TEST(name, ...) START_TEST((name)) { __VA_ARGS__ } END_TEST
#include SUITE
#undef TEST
#undef MOCK
#define MOCK(...)

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
