#pragma once

#include <QString>

//static const float EARTH_RADIUS_KM = 6371.0;

typedef struct ip_info_t {
    QString ip_address;
    QString isp;
    float lat;
    float lon;
} IPInfo;

typedef struct server_info_t {
    QString url;
    QString name;
    QString country;
    QString country_code;
    QString host;
    QString sponsor;
    int   id;
    float lat;
    float lon;
    float distance;

} ServerInfo;

typedef struct test_config_t {
    long start_size;
    long max_size;
    long incr_size;
    long chunk_size;
    long min_test_time_ms;
    int  concurrency;
    QString label;
} TestConfig;
