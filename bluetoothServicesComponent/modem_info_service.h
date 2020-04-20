#ifndef __MODEM_INFO_SERVICE_H_
#define __MODEM_INFO_SERVICE_H_

#define MODEM_INFO_SERVICE_UUID "180A"

void modem_info_register_services(
    GDBusObjectManagerServer *services_om,
    size_t *num_services_registered);

#endif // __MODEM_INFO_SERVICE_H_
