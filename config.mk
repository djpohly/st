# st version
VERSION = 0.7

# Customize below to fit your system

# paths
PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man

X11INC = /usr/X11R6/include
X11LIB = /usr/X11R6/lib

# includes and libs
PKGS = fontconfig freetype2 wayland-client wayland-cursor xkbcommon pixman-1 libdrm wld
INCS = -I$(X11INC) \
       $(shell pkg-config --cflags $(PKGS))
LIBS = -L$(X11LIB) -lm -lrt -lX11 -lutil -lXft \
       $(shell pkg-config --libs $(PKGS))

# flags
CPPFLAGS = -DVERSION=\"$(VERSION)\" -D_XOPEN_SOURCE=600
STCFLAGS = $(INCS) $(CPPFLAGS) $(CFLAGS)
STLDFLAGS = $(LIBS) $(LDFLAGS)

# compiler and linker
# CC = c99

