include ../config.mk

TESTS = tnew
OBJS = x.o st.o

LDLIBS += -lcheck

.PHONY: all clean check

all: $(TESTS)

clean:
	$(RM) $(TESTS) test.o $(OBJS)

check:
	prove $(addprefix ./,$(TESTS))

$(OBJS): %.o: ../%.c
	$(CC) $(STCFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c -o $@ $<
	objcopy --weaken $@

$(TESTS): %: %.c $(OBJS) test.o test.h
	$(CC) -o $@ $^ $(STLDFLAGS) -lcheck

$(OBJS): CPPFLAGS+=-Dstatic=
$(OBJS): CFLAGS+=-fno-inline
