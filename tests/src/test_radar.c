#include <zephyr/ztest.h>
#include <math.h>

#define TEST_SENSOR_DISTANCE_M 1.0f
#define TEST_SPEED_LIMIT_LIGHT_KMH 80.0f
#define TEST_SPEED_LIMIT_HEAVY_KMH 60.0f
#define TEST_WARNING_THRESHOLD_PERCENT 90.0f

enum test_radar_status {
    TEST_STATUS_NORMAL,
    TEST_STATUS_WARNING,
    TEST_STATUS_INFRACTION
};

float calculate_speed_kmh(uint32_t delta_time_ms) {
    if (delta_time_ms == 0) return 0.0f;
    float time_s = (float)delta_time_ms / 1000.0f;
    float speed_ms = TEST_SENSOR_DISTANCE_M / time_s;
    return speed_ms * 3.6f;
}

enum test_radar_status check_violation_status(int axles, float speed_kmh) {
    float limit_kmh;
    if (axles <= 2) {
        limit_kmh = TEST_SPEED_LIMIT_LIGHT_KMH;
    } else {
        limit_kmh = TEST_SPEED_LIMIT_HEAVY_KMH;
    }

    float warning_limit_kmh = limit_kmh * (TEST_WARNING_THRESHOLD_PERCENT / 100.0f);

    if (speed_kmh > limit_kmh) {
        return TEST_STATUS_INFRACTION;
    } else if (speed_kmh > warning_limit_kmh) {
        return TEST_STATUS_WARNING;
    } else {
        return TEST_STATUS_NORMAL;
    }
}

ZTEST(radar_suite, test_speed_calculation)
{
    float speed1s = calculate_speed_kmh(1000);
    zassert_true(fabs(speed1s - 3.6f) < 0.01f, "Esperado 3.6 km/h, obtido %.2f", speed1s);

    float speed0_1s = calculate_speed_kmh(100);
    zassert_true(fabs(speed0_1s - 36.0f) < 0.01f, "Esperado 36.0 km/h, obtido %.2f", speed0_1s);
}

ZTEST(radar_suite, test_light_vehicle_infraction)
{
    int axles_light = 2; // Leve, Limite 80 km/h, Alerta 72 km/h

    zassert_equal(check_violation_status(axles_light, 70.0f), TEST_STATUS_NORMAL, "Leve: Esperado NORMAL");
    zassert_equal(check_violation_status(axles_light, 75.0f), TEST_STATUS_WARNING, "Leve: Esperado WARNING");
    zassert_equal(check_violation_status(axles_light, 81.0f), TEST_STATUS_INFRACTION, "Leve: Esperado INFRACTION");
}

ZTEST(radar_suite, test_heavy_vehicle_infraction)
{
    int axles_heavy = 3; // Pesado, Limite 60 km/h, Alerta 54 km/h

    zassert_equal(check_violation_status(axles_heavy, 50.0f), TEST_STATUS_NORMAL, "Pesado: Esperado NORMAL");
    zassert_equal(check_violation_status(axles_heavy, 55.0f), TEST_STATUS_WARNING, "Pesado: Esperado WARNING");
    zassert_equal(check_violation_status(axles_heavy, 61.0f), TEST_STATUS_INFRACTION, "Pesado: Esperado INFRACTION");
}

ZTEST_SUITE(radar_suite, NULL, NULL, NULL, NULL, NULL);
