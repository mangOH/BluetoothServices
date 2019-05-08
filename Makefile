REQ_LIBS = glib-2.0 gio-2.0 gio-unix-2.0 gobject-2.0

CFLAGS += -std=c99
CFLAGS += -Wall
CFLAGS += `pkg-config --cflags ${REQ_LIBS}`
CFLAGS += -IbluetoothServicesComponent

LDFLAGS += `pkg-config --libs ${REQ_LIBS}`

bluetooth_services: main.o bluetoothServicesComponent/primary.o bluetoothServicesComponent/battery_service.o bluetoothServicesComponent/immediate_alert.o bluetoothServicesComponent/bluez_dbus.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

bluetoothServicesComponent/primary.o: bluetoothServicesComponent/primary.c bluetoothServicesComponent/bluez_dbus.h

bluetoothServicesComponent/battery_service.o: bluetoothServicesComponent/battery_service.c bluetoothServicesComponent/bluez_dbus.h

bluetoothServicesComponent/immediate_alert.o: bluetoothServicesComponent/immediate_alert.c bluetoothServicesComponent/bluez_dbus.h

bluetoothServicesComponent/bluez_dbus.c bluetoothServicesComponent/bluez_dbus.h: bluetoothServicesComponent/org.bluez.xml
	cd bluetoothServicesComponent && gdbus-codegen --generate-c-code=bluez_dbus --interface-prefix org.bluez. --c-namespace Bluez org.bluez.xml

# Objects based on generated source
bluetoothServicesComponent/bluez_dbus.o: bluetoothServicesComponent/bluez_dbus.c bluetoothServicesComponent/bluez_dbus.h
	$(CC) -c $< $(CFLAGS) $(LDFLAGS) -o $@

clean:
	$(RM) *.o
	$(RM) bluetoothServicesComponent/*.o
	$(RM) bluetooth_services
	$(RM) bluetoothServicesComponent/bluez_dbus.h bluetoothServicesComponent/bluez_dbus.c
