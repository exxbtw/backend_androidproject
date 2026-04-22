#include "gui_app.h"
#include "map_engine.h"
#include "imgui.h"
#include "implot.h"
#include <cmath>
#include <algorithm>

void plot_metric(const char* plot_id, const char* y_label, const std::map<int, PciHistory>& histories, int metric) {
    if (ImPlot::BeginPlot(plot_id, ImVec2(-1, 250))) {
        ImPlot::SetupAxes("Time (sec)", y_label);
        ImPlot::SetupLegend(ImPlotLocation_NorthEast);

        for (auto& [pci, h] : histories) {
            if (h.timestamps.empty()) continue;

            const std::vector<double>* vals = nullptr;
            if      (metric == 0) vals = &h.rsrp;
            else if (metric == 1) vals = &h.rssi;
            else                  vals = &h.sinr;

            char label[32];
            snprintf(label, sizeof(label), "PCI %d", pci);
            ImPlot::PlotLine(label, h.timestamps.data(), vals->data(), (int)h.timestamps.size());
        }
        ImPlot::EndPlot();
    }
}

void draw_telemetry_window(const LocationData& loc, const std::map<int, PciHistory>& histories) {
    ImGui::Begin("Smartphone Data");

    ImGui::Text("Latitude: %.6f", loc.lat);
    ImGui::Text("Longitude: %.6f", loc.lon);
    ImGui::Text("Altitude: %.2f m", loc.alt);

    ImGui::Separator();

    if (loc.has_cell) {
        for (auto& c : loc.cells) {
            if (!c.is_primary) continue;
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "[Primary] Type: %s  PCI: %d", c.type.c_str(), c.pci);
            ImGui::Text("  RSRP: %d dBm", c.rsrp);
            ImGui::Text("  RSSI: %d", c.rssi);
            ImGui::Text("  SINR: %d", (c.ss_sinr != 0) ? c.ss_sinr : c.rssnr);
        }
        ImGui::Separator();
        for (auto& c : loc.cells) {
            ImGui::Text("  [%s] PCI=%-4d RSRP=%-5d RSSI=%-5d SINR=%-5d %s",
                c.type.c_str(), c.pci, c.rsrp, c.rssi, 
                (c.ss_sinr != 0) ? c.ss_sinr : c.rssnr,
                c.is_primary ? "<- primary" : "");
        }
    }

    ImGui::Separator();
    if (histories.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "no data...");
    }

    plot_metric("RSRP##plot", "RSRP (dBm)", histories, 0);
    ImGui::Spacing();
    plot_metric("RSSI##plot", "RSSI (dBm)", histories, 1);
    ImGui::Spacing();
    plot_metric("SINR##plot", "SINR (dB)", histories, 2);

    ImGui::End();
}

void draw_map_window(float cur_lat, float cur_lon) {
    ImGui::Begin("OpenStreetMap", nullptr, ImGuiWindowFlags_NoScrollbar);

    if (ImGui::Button(" + ")) { g_map_view.zoom = std::min(g_map_view.zoom + 1, 19); 
        std::lock_guard<std::mutex> lk(tile_cache_mtx); tile_cache.clear(); }
    ImGui::SameLine();
    if (ImGui::Button(" - ")) { g_map_view.zoom = std::max(g_map_view.zoom - 1, 1);  
        std::lock_guard<std::mutex> lk(tile_cache_mtx); tile_cache.clear(); }
    ImGui::SameLine();
    ImGui::Text("Zoom: %d", g_map_view.zoom);
    ImGui::SameLine();
    if (ImGui::Button("Center on GPS")) {
        g_map_view.center_lat = cur_lat;
        g_map_view.center_lon = cur_lon;
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float win_w = avail.x; float win_h = avail.y;

    const int TILE_PX = 256;
    int tiles_x = (int)std::ceil(win_w / (float)TILE_PX) + 1; 
    int tiles_y = (int)std::ceil(win_h / (float)TILE_PX) + 1;

    TileID center_tile = lat_lon_to_tile(g_map_view.center_lat, g_map_view.center_lon, g_map_view.zoom);
    double center_px, center_py;
    lat_lon_to_pixel_offset(g_map_view.center_lat, g_map_view.center_lon, g_map_view.zoom, center_tile.x, center_tile.y, center_px, center_py);

    int half_x = tiles_x / 2; int half_y = tiles_y / 2;
    int tl_tile_x = center_tile.x - half_x; int tl_tile_y = center_tile.y - half_y;

    float offset_px = (float)(win_w / 2.0 - half_x * TILE_PX - center_px);
    float offset_py = (float)(win_h / 2.0 - half_y * TILE_PX - center_py);

    int max_idx = (1 << g_map_view.zoom) - 1;

    for (int ty = 0; ty < tiles_y; ++ty) {
        for (int tx = 0; tx < tiles_x; ++tx) {
            int gx = tl_tile_x + tx; int gy = tl_tile_y + ty;
            if (gx < 0 || gy < 0 || gx > max_idx || gy > max_idx) continue;
            TileID tid{g_map_view.zoom, gx, gy};

            bool needs_texture = false;
            {
                std::lock_guard<std::mutex> lk(tile_cache_mtx);
                if (tile_cache.find(tid) == tile_cache.end()) needs_texture = true;
            }

            if (needs_texture) {
                if (tile_exists_on_disk(tid)) load_tile_texture(tid);
                else request_tile(tid);
            }
        }
    }

    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->PushClipRect(canvas_pos, ImVec2(canvas_pos.x + win_w, canvas_pos.y + win_h), true);

    for (int ty = 0; ty < tiles_y; ++ty) {
        for (int tx = 0; tx < tiles_x; ++tx) {
            int gx = tl_tile_x + tx; int gy = tl_tile_y + ty;
            if (gx < 0 || gy < 0 || gx > max_idx || gy > max_idx) continue;

            TileID tid{g_map_view.zoom, gx, gy};
            float px0 = canvas_pos.x + offset_px + tx * TILE_PX;
            float py0 = canvas_pos.y + offset_py + ty * TILE_PX;
            float px1 = px0 + TILE_PX; float py1 = py0 + TILE_PX;

            TileTexture tex_info{};
            {
                std::lock_guard<std::mutex> lk(tile_cache_mtx);
                auto it = tile_cache.find(tid);
                if (it != tile_cache.end()) tex_info = it->second;
            }

            if (tex_info.loaded && tex_info.tex_id != 0) {
                draw_list->AddImage((ImTextureID)(uintptr_t)tex_info.tex_id, ImVec2(px0, py0), ImVec2(px1, py1));
            } else {
                draw_list->AddRectFilled(ImVec2(px0, py0), ImVec2(px1, py1), IM_COL32(60,60,60,255));
                draw_list->AddRect(ImVec2(px0, py0), ImVec2(px1, py1), IM_COL32(100,100,100,255));
            }
        }
    }

    if (cur_lat != 0.0f || cur_lon != 0.0f) {
        double gps_px, gps_py;
        TileID gps_tile = lat_lon_to_tile(cur_lat, cur_lon, g_map_view.zoom);
        lat_lon_to_pixel_offset(cur_lat, cur_lon, g_map_view.zoom, gps_tile.x, gps_tile.y, gps_px, gps_py);

        float marker_x = canvas_pos.x + offset_px + (gps_tile.x - tl_tile_x) * TILE_PX + (float)gps_px;
        float marker_y = canvas_pos.y + offset_py + (gps_tile.y - tl_tile_y) * TILE_PX + (float)gps_py;

        draw_list->AddCircleFilled(ImVec2(marker_x, marker_y), 8.0f, IM_COL32(255, 50, 50, 220));
        draw_list->AddCircle(ImVec2(marker_x, marker_y), 8.0f, IM_COL32(255,255,255,255), 0, 2.0f);
    }

    draw_list->PopClipRect();

    ImGui::InvisibleButton("map_canvas", ImVec2(win_w, win_h));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);

        double n = std::pow(2.0, g_map_view.zoom);
        double deg_per_px_lon = 360.0 / (n * TILE_PX);
        double lat_rad = g_map_view.center_lat * M_PI / 180.0;
        double deg_per_px_lat = 360.0 / (n * TILE_PX) / std::cos(lat_rad);

        g_map_view.center_lon -= delta.x * deg_per_px_lon;
        g_map_view.center_lat += delta.y * deg_per_px_lat;
    }

    if (ImGui::IsItemHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            if (wheel > 0.0f) g_map_view.zoom = std::min(g_map_view.zoom + 1, 19);
            else g_map_view.zoom = std::max(g_map_view.zoom - 1, 1);
            std::lock_guard<std::mutex> lk(tile_cache_mtx); 
            tile_cache.clear(); 
        }
    }
    ImGui::End();
}