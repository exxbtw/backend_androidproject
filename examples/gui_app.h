#pragma once
#include "telemetry.h" 
#include <map>

void draw_map_window(float cur_lat, float cur_lon);
void draw_telemetry_window(const LocationData& loc, const std::map<int, PciHistory>& histories);