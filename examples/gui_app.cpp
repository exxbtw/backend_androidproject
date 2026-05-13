#include "gui_app.h"
#include "map_engine.h"
#include "heatmap.h"
#include "imgui.h"
#include "implot.h"
#include <cmath>
#include <algorithm>


static void plot_metric(const char* plot_id, const char* y_label,
                        const std::map<int, PciHistory>& histories, int metric)
{
    if (ImPlot::BeginPlot(plot_id, ImVec2(-1, 250))) {
        ImPlot::SetupAxes("Time (sec)", y_label);
        ImPlot::SetupLegend(ImPlotLocation_NorthEast);
        for (auto& [pci, h] : histories) {
            if (h.timestamps.empty()) continue;
            const std::vector<double>* vals = nullptr;
            if (metric == 0) vals = &h.rsrp;
            else if (metric == 1) vals = &h.rssi;
            else vals = &h.sinr;
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
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
                "[Primary] Type: %s  PCI: %d", c.type.c_str(), c.pci);
            ImGui::Text(" RSRP: %d dBm", c.rsrp);
            ImGui::Text(" RSSI: %d", c.rssi);
            ImGui::Text(" SINR: %d", (c.ss_sinr != 0) ? c.ss_sinr : c.rssnr);
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
    if (histories.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "no data...");

    plot_metric("RSRP##plot", "RSRP (dBm)", histories, 0);
    ImGui::Spacing();
    plot_metric("RSSI##plot", "RSSI (dBm)", histories, 1);
    ImGui::Spacing();
    plot_metric("SINR##plot", "SINR (dB)",  histories, 2);
    ImGui::End();
}


static bool s_show_heatmap = false;
static int s_metric_idx = 0;
static int s_earfcn_filter = 0;
static int s_pci_filter = 0;


void draw_map_window(float cur_lat, float cur_lon) {
    ImGui::Begin("OpenStreetMap", nullptr, ImGuiWindowFlags_NoScrollbar);

    if (ImGui::Button(" + ")) {
        g_map_view.zoom = std::min(g_map_view.zoom + 1, 19);
        std::lock_guard<std::mutex> lk(tile_cache_mtx); tile_cache.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button(" - ")) {
        g_map_view.zoom = std::max(g_map_view.zoom - 1, 1);
        std::lock_guard<std::mutex> lk(tile_cache_mtx); tile_cache.clear();
    }
    ImGui::SameLine();
    ImGui::Text("Zoom: %d", g_map_view.zoom);
    ImGui::SameLine();
    if (ImGui::Button("Center on GPS")) {
        g_map_view.center_lat = cur_lat;
        g_map_view.center_lon = cur_lon;
    }

    //хитмап
    ImGui::Separator();
    bool metric_changed = false;

    ImGui::Checkbox("Show Heatmap", &s_show_heatmap);

    if (s_show_heatmap) {
        ImGui::SameLine();
        const char* metrics[] = {"RSRP", "RSRQ", "RSSI", "Altitude"};
        ImGui::SetNextItemWidth(100);
        if (ImGui::Combo("Metric##hm", &s_metric_idx, metrics, 4))
            metric_changed = true;

        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        if (ImGui::InputInt("EARFCN##hm", &s_earfcn_filter, 0)) {
            if (s_earfcn_filter < 0) s_earfcn_filter = 0;
            metric_changed = true;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        if (ImGui::InputInt("PCI##hm", &s_pci_filter, 0)) {
            if (s_pci_filter < 0) s_pci_filter = 0;
            metric_changed = true;
        }
        ImGui::SameLine();
        ImGui::TextColored(s_earfcn_filter == 0 && s_pci_filter == 0
            ? ImVec4(0.5f,0.8f,0.5f,1.f)
            : ImVec4(1.f,0.8f,0.3f,1.f),
            s_earfcn_filter == 0 && s_pci_filter == 0 ? "(all)" : "(filtered)");

        ImGui::SetNextItemWidth(130);
        if (ImGui::SliderFloat("Radius m##idw", &g_idw_radius_m, 10.f, 40.f, "%.0f"))
            metric_changed = true;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        if (ImGui::SliderFloat("Power##idw", &g_idw_power, 1.f, 4.f, "%.1f"))
            metric_changed = true;

        ImGui::SameLine();
        if (ImGui::Button("Regen now"))
            metric_changed = true;

        g_heat_metric = (HeatMetric)s_metric_idx;
        g_heat_earfcn_filter = s_earfcn_filter;
        g_heat_pci_filter = s_pci_filter;

        {
            std::lock_guard<std::mutex> lk(g_heat_points_mtx);
            int pci_count = 0;
            for (auto& p : g_heat_points)
                if (s_pci_filter == 0 || p.pci == s_pci_filter) pci_count++;
            ImGui::TextColored(ImVec4(0.5f,0.8f,1.f,1.f),
                "Total: %d | PCI %d: %d pts",
                (int)g_heat_points.size(), s_pci_filter, pci_count);
        }

        if (metric_changed)
            heat_invalidate_cache();
    }
    ImGui::Separator();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float win_w = avail.x, win_h = avail.y;
    const int TILE_PX = 256;
    int tiles_x = (int)std::ceil(win_w / (float)TILE_PX) + 1;
    int tiles_y = (int)std::ceil(win_h / (float)TILE_PX) + 1;

    TileID center_tile = lat_lon_to_tile(g_map_view.center_lat, g_map_view.center_lon, g_map_view.zoom);
    double center_px, center_py;
    lat_lon_to_pixel_offset(g_map_view.center_lat, g_map_view.center_lon,
                            g_map_view.zoom, center_tile.x, center_tile.y, center_px, center_py);

    int half_x = tiles_x / 2, half_y = tiles_y / 2;
    int tl_tile_x = center_tile.x - half_x;
    int tl_tile_y = center_tile.y - half_y;
    float offset_px = (float)(win_w / 2.0 - half_x * TILE_PX - center_px);
    float offset_py = (float)(win_h / 2.0 - half_y * TILE_PX - center_py);
    int max_idx = (1 << g_map_view.zoom) - 1;

    for (int ty = 0; ty < tiles_y; ++ty) {
        for (int tx = 0; tx < tiles_x; ++tx) {
            int gx = tl_tile_x + tx, gy = tl_tile_y + ty;
            if (gx < 0 || gy < 0 || gx > max_idx || gy > max_idx) continue;
            TileID tid{g_map_view.zoom, gx, gy};

            bool needs = false;
            { std::lock_guard<std::mutex> lk(tile_cache_mtx); needs = !tile_cache.count(tid); }
            if (needs) {
                if (tile_exists_on_disk(tid)) load_tile_texture(tid);
                else request_tile(tid);
            }
            {
                std::lock_guard<std::mutex> lk(tile_cache_mtx);
                auto it = tile_cache.find(tid);
                if (it != tile_cache.end() && !it->second.loaded && tile_exists_on_disk(tid))
                    load_tile_texture(tid);
            }

            if (s_show_heatmap) {
                heat_request_tile(g_map_view.zoom, gx, gy);
            }
        }
    }

    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(canvas_pos, ImVec2(canvas_pos.x + win_w, canvas_pos.y + win_h), true);

    for (int ty = 0; ty < tiles_y; ++ty) {
        for (int tx = 0; tx < tiles_x; ++tx) {
            int gx = tl_tile_x + tx, gy = tl_tile_y + ty;
            if (gx < 0 || gy < 0 || gx > max_idx || gy > max_idx) continue;
            TileID tid{g_map_view.zoom, gx, gy};

            float px0 = canvas_pos.x + offset_px + tx * TILE_PX;
            float py0 = canvas_pos.y + offset_py + ty * TILE_PX;
            float px1 = px0 + TILE_PX, py1 = py0 + TILE_PX;

            TileTexture tex{};
            { std::lock_guard<std::mutex> lk(tile_cache_mtx); auto it = tile_cache.find(tid); if (it != tile_cache.end()) tex = it->second; }
            if (tex.loaded && tex.tex_id) {
                dl->AddImage((ImTextureID)(uintptr_t)tex.tex_id, {px0,py0}, {px1,py1});
            } else {
                dl->AddRectFilled({px0,py0},{px1,py1}, IM_COL32(60,60,60,255));
                dl->AddRect({px0,py0},{px1,py1}, IM_COL32(100,100,100,255));
                char buf[32]; snprintf(buf,sizeof(buf),"%d/%d/%d",g_map_view.zoom,gx,gy);
                dl->AddText({px0+4,py0+4}, IM_COL32(150,150,150,255), buf);
            }
            //слой хитмап
            if (s_show_heatmap) {
                HeatTile htex = heat_get_tile(g_map_view.zoom, gx, gy);
                if (htex.loaded && htex.tex_id) {
                    ImU32 tint = IM_COL32(255, 255, 255, (int)(htex.alpha * 255));
                    dl->AddImage((ImTextureID)(uintptr_t)htex.tex_id,
                                 {px0,py0}, {px1,py1},
                                 {0,0}, {1,1}, tint);
                }
            }
        }
    }

    if (cur_lat != 0.0f || cur_lon != 0.0f) {
        TileID gps_tile = lat_lon_to_tile(cur_lat, cur_lon, g_map_view.zoom);
        double gx, gy;
        lat_lon_to_pixel_offset(cur_lat, cur_lon, g_map_view.zoom,
                                gps_tile.x, gps_tile.y, gx, gy);
        float mx = canvas_pos.x + offset_px + (gps_tile.x - tl_tile_x)*TILE_PX + (float)gx;
        float my = canvas_pos.y + offset_py + (gps_tile.y - tl_tile_y)*TILE_PX + (float)gy;
        dl->AddCircleFilled({mx,my}, 8.f, IM_COL32(255,50,50,220));
        dl->AddCircle({mx,my}, 8.f, IM_COL32(255,255,255,255), 0, 2.f);
    }

    if (s_show_heatmap) {
        const float LX = canvas_pos.x + win_w - 50;
        const float LY = canvas_pos.y + 15;
        const float LW = 16, LH = 120;

        for (int i = 0; i < (int)LH; ++i) {
            float t = 1.f - (float)i / LH;
            t = std::max(0.f, std::min(1.f, t));
            unsigned char r, g, b;
            if (t < 0.25f) { float s=t/0.25f; r=0; g=(unsigned char)(s*255); b=255; }
            else if (t < 0.5f) { float s=(t-0.25f)/0.25f; r=0; g=255; b=(unsigned char)((1-s)*255); }
            else if (t < 0.75f) { float s=(t-0.5f)/0.25f; r=(unsigned char)(s*255); g=255; b=0; }
            else { float s=(t-0.75f)/0.25f; r=255; g=(unsigned char)((1-s)*255); b=0; }
            dl->AddRectFilled({LX,LY+i},{LX+LW,LY+i+1}, IM_COL32(r,g,b,200));
        }
        dl->AddRect({LX,LY},{LX+LW,LY+LH}, IM_COL32(200,200,200,180));

        auto range = [&]() -> std::pair<float,float> {
            switch ((HeatMetric)s_metric_idx) {
                case HeatMetric::RSRP: return {-110.f, -80.f};
                case HeatMetric::RSRQ: return {-20.f, -10.f};
                case HeatMetric::RSSI: return {-110.f, -40.f};
                case HeatMetric::Altitude: return { 0.f, 1000.f};
                default: return { 0.f, 1.f};
            }
        }();
        char bmax[16], bmin[16];
        snprintf(bmax, sizeof(bmax), "%.0f", range.second);
        snprintf(bmin, sizeof(bmin), "%.0f", range.first);
        dl->AddText({LX-30, LY-1}, IM_COL32(255,255,255,220), bmax);
        dl->AddText({LX-30, LY+LH-1}, IM_COL32(255,255,255,220), bmin);
        dl->AddText({LX-2, LY+LH+4}, IM_COL32(200,200,200,200),
                    heat_metric_name((HeatMetric)s_metric_idx));
    }

    dl->PopClipRect();

    ImGui::InvisibleButton("map_canvas", {win_w, win_h});
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.f);
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
        double n = std::pow(2.0, g_map_view.zoom);
        double dlon = 360.0 / (n * TILE_PX);
        double dlat = dlon / std::cos(g_map_view.center_lat * M_PI / 180.0);
        g_map_view.center_lon -= delta.x * dlon;
        g_map_view.center_lat += delta.y * dlat;
    }
    if (ImGui::IsItemHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel > 0.f) { g_map_view.zoom = std::min(g_map_view.zoom+1, 19); std::lock_guard<std::mutex> lk(tile_cache_mtx); tile_cache.clear(); }
        if (wheel < 0.f) { g_map_view.zoom = std::max(g_map_view.zoom-1,  1); std::lock_guard<std::mutex> lk(tile_cache_mtx); tile_cache.clear(); }
    }

    ImGui::End();
}
