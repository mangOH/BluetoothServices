REQ_LIBS = glib-2.0 gio-2.0 gio-unix-2.0 gobject-2.0

CFLAGS += -std=c99
CFLAGS += `pkg-config --cflags ${REQ_LIBS}`

LDFLAGS += `pkg-config --libs ${REQ_LIBS}`

fake_bs: main.o gen/bluez_dbus.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

main.o: main.c gen/bluez_dbus.h

gen/bluez_dbus.h: bluez_dbus_introspection.xml
	mkdir -p gen
	gdbus-codegen --header --interface-prefix org.bluez. --c-namespace Bluez --c-generate-object-manager --output gen/bluez_dbus.h $<

gen/bluez_dbus.c: bluez_dbus_introspection.xml gen/bluez_dbus.h
	mkdir -p gen
	gdbus-codegen --body --interface-prefix org.bluez. --c-namespace Bluez --c-generate-object-manager --output gen/bluez_dbus.c $<

clean:
	$(RM) *.o
	$(RM) fake_bs
	$(RM) gen/*

