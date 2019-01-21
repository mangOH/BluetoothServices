REQ_LIBS = glib-2.0 gio-2.0 gobject-2.0

CFLAGS += -std=c99
CFLAGS += `pkg-config --cflags ${REQ_LIBS}`

LDFLAGS += `pkg-config --libs ${REQ_LIBS}`

fake_bs: main.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

clean:
	$(RM) fake_bs

