.SUFFIXES: .c .a .o .h
.PHONY: clean all
# .PHONY: install uninstall


TARGET=preallocate

CFLAGS+=-Wall
CFLAGS+=-Wextra
CFLAGS+=-Wpedantic
CFLAGS+=-Wshadow
CFLAGS+=-Werror
CFLAGS+=-Wtype-limits
CFLAGS+=-Wstrict-aliasing
CFLAGS+=-O2
CFLAGS+=-std=gnu11


SOURCES+=src/preallocate.c
OBJECTS+=$(subst .c,.o, $(SOURCES))

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) -o $@ $(OBJECTS)

clean:
	rm $(TARGET) $(OBJECTS)

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@





