REQ_LIBS = glib-2.0 gio-2.0 gio-unix-2.0 gobject-2.0

CFLAGS += -std=c99
CFLAGS += -Wall
CFLAGS += `pkg-config --cflags ${REQ_LIBS}`
CFLAGS += -IbluezBatteryComponent

LDFLAGS += `pkg-config --libs ${REQ_LIBS}`

fake_bs: main.o bluezBatteryComponent/battery_service.o bluezBatteryComponent/bluez_dbus.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

bluezBatteryComponent/battery_service.o: bluezBatteryComponent/battery_service.c bluezBatteryComponent/bluez_dbus.h

# Generated source
bluezBatteryComponent/bluez_dbus.h: bluezBatteryComponent/org.bluez.xml
	gdbus-codegen --header --interface-prefix org.bluez. --c-namespace Bluez --output $@ $<

bluezBatteryComponent/bluez_dbus.c: bluezBatteryComponent/org.bluez.xml
	gdbus-codegen --body --interface-prefix org.bluez. --c-namespace Bluez --output $@ $<

# Objects based on generated source
bluezBatteryComponent/bluez_dbus.o: bluezBatteryComponent/bluez_dbus.c bluezBatteryComponent/bluez_dbus.h
	$(CC) -c $< $(CFLAGS) -o $@

clean:
	$(RM) *.o
	$(RM) bluezBatteryComponent/*.o
	$(RM) fake_bs
	$(RM) bluezBatteryComponent/bluez_dbus.h bluezBatteryComponent/bluez_dbus.c
