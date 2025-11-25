#ifndef CAMERA_SERVICE_PRIV_H
#define CAMERA_SERVICE_PRIV_H

#include <zephyr/kernel.h>
#include "../include/camera_service.h"


static const struct camera_data valid_car_license_plates[] = {
	{"AAA0A00", "12345678"},
	{"BBB1B11", "23456789"},
	{"CCC2C22", "34567890"},
	{"DDD3D33", "45678901"},
};

static const struct camera_data invalid_car_license_plates[] = {
	{"INV9X99", "99999999"},
	{"FAL0H0", "00000000"},
};

ZBUS_MSG_SUBSCRIBER_DECLARE(msub_camera_cmd);

#endif /* CAMERA_SERVICE_PRIV_H */
