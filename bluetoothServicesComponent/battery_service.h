#ifndef _BATTERY_SERVICE_H
#define _BATTERY_SERVICE_H

void battery_register_services(GDBusObjectManagerServer *services_om, size_t *num_services_registered);

#endif // _BATTERY_SERVICE_H
