#ifndef _IMMEDIATE_ALERT_SERVICE_H
#define _IMMEDIATE_ALERT_SERVICE_H

#define IMMEDIATE_ALERT_SERVICE_UUID "1802"

void alert_register_services(GDBusObjectManagerServer *services_om, size_t *num_services_registered);

#endif // _IMMEDIATE_ALERT_SERVICE_H
