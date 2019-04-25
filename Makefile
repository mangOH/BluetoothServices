REQ_LIBS = glib-2.0 gio-2.0 gio-unix-2.0 gobject-2.0

CFLAGS += -std=c99
CFLAGS += `pkg-config --cflags ${REQ_LIBS}`

LDFLAGS += `pkg-config --libs ${REQ_LIBS}`

fake_bs: main.o bluez_dbus.o freedesktop_dbus.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

main.o: main.c bluez_dbus.h freedesktop_dbus.h

# Generated source
bluez_dbus.h: org.bluez.xml
	gdbus-codegen --header --interface-prefix org.bluez. --c-namespace Bluez --output $@ $<

bluez_dbus.c: org.bluez.xml
	gdbus-codegen --body --interface-prefix org.bluez. --c-namespace Bluez --output $@ $<

freedesktop_dbus.h: org.freedesktop.DBus.xml
	gdbus-codegen --header --interface-prefix org.freedesktop.DBus. --c-namespace DBus --output $@ $<

freedesktop_dbus.c: org.freedesktop.DBus.xml
	gdbus-codegen --body --interface-prefix org.freedesktop.DBus. --c-namespace DBus --output $@ $<

# Objects based on generated source
bluez_dbus.o: bluez_dbus.c bluez_dbus.h
	$(CC) -c $< $(CFLAGS)

freedesktop_dbus.o: freedesktop_dbus.c freedesktop_dbus.h
	$(CC) -c $< $(CFLAGS)

clean:
	$(RM) *.o
	$(RM) fake_bs
	$(RM) bluez_dbus.h bluez_dbus.c freedesktop_dbus.h freedesktop_dbus.c
