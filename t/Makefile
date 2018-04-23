include ../config.mk

TESTS = tnew
OBJS = x.o st.o
TESTOBJS = $(addsuffix .o,$(TESTS))

LDLIBS += -lcheck

.PHONY: all clean check

all: $(TESTS)

clean:
	$(RM) $(TESTS) $(TESTOBJS) $(OBJS)

check: $(TESTS)
	prove $(addprefix ./,$(TESTS))

$(OBJS): %.o: ../%.c
	$(CC) $(STCFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c -o $@ $<
	objcopy --weaken $@

$(TESTOBJS): %.o: test.c %.c
	$(CC) -c -o $@ -DSUITE=\"$*.c\" $<

$(TESTS): %: %.o $(OBJS)
	$(CC) -o $@ $^ $(STLDFLAGS) -lcheck

$(OBJS): CPPFLAGS+=-Dstatic=
$(OBJS): CFLAGS+=-fno-inline
