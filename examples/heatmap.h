#pragma once

#include <vector>
#include <mutex>
#include <GL/glew.h>
#include "telemetry.h"

struct HeatPoint {
    double lat, lon;
    float alt;
    int rsrp, rsrq, rssi, earfcn, pci;
};

enum class HeatMetric { RSRP = 0, RSRQ, RSSI, Altitude };

inline const char* heat_metric_name(HeatMetric m) {
    switch (m) {
        case HeatMetric::RSRP: return "RSRP";
        case HeatMetric::RSRQ: return "RSRQ";
        case HeatMetric::RSSI: return "RSSI";
        case HeatMetric::Altitude: return "Alt";
        default: return "?";
    }
}

struct HeatTile {
    GLuint tex_id = 0;
    bool loaded = false;
    float alpha  = 0.65f;
};

extern std::vector<HeatPoint> g_heat_points;
extern std::mutex g_heat_points_mtx;

extern HeatMetric g_heat_metric;
extern int g_heat_earfcn_filter;
extern int g_heat_pci_filter;
extern float g_idw_radius_m;
extern float g_idw_power;

void heat_add_points(const LocationData& loc);
void heat_request_tile(int zoom, int tx, int ty);
HeatTile heat_get_tile(int zoom, int tx, int ty);
void heat_invalidate_cache();
void heat_worker_thread();
