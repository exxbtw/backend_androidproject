#include "heatmap.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "stb_image.h"

#include <cmath>
#include <filesystem>
#include <sstream>
#include <queue>
#include <set>
#include <condition_variable>
#include <atomic>
#include <map>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


std::vector<HeatPoint> g_heat_points;
std::mutex g_heat_points_mtx;

HeatMetric g_heat_metric = HeatMetric::RSRP;
int g_heat_earfcn_filter = 0;
int g_heat_pci_filter = 0;
float g_idw_radius_m = 25.0f;
float g_idw_power = 2.0f;


static std::mutex s_cache_mtx;
static std::map<std::tuple<int,int,int>, HeatTile> s_cache;


static std::mutex s_q_mtx;
static std::condition_variable s_q_cv;
static std::queue<std::tuple<int,int,int>> s_queue;
static std::set<std::tuple<int,int,int>> s_q_set;   
static std::atomic<bool> s_running{ true };


static std::string tile_path(int zoom, int tx, int ty) {
    std::ostringstream ss;
    ss << "build/" << zoom << "/" << tx << "/" << ty
       << "_heat_" << (int)g_heat_metric
       << "_" << g_heat_earfcn_filter
       << "_" << g_heat_pci_filter << ".png";
    return ss.str();
}

static float get_point_value(const HeatPoint& p) {
    switch (g_heat_metric) {
        case HeatMetric::RSRP: return (p.rsrp != 0 && p.rsrp > -200) ? (float)p.rsrp : NAN;
        case HeatMetric::RSRQ: return (p.rsrq != 0 && p.rsrq > -30) ? (float)p.rsrq : NAN;
        case HeatMetric::RSSI: return (p.rssi != 0 && p.rssi > -200) ? (float)p.rssi : NAN;
        case HeatMetric::Altitude: return p.alt;
        default: return NAN;
    }
}

static std::pair<float,float> metric_range() {
    switch (g_heat_metric) {
        case HeatMetric::RSRP: return { -110.f, -80.f };
        case HeatMetric::RSRQ: return { -20.f, -10.f };
        case HeatMetric::RSSI: return { -110.f, -40.f };
        case HeatMetric::Altitude: return { 0.f, 3000.f };
        default: return { -1.f, 1.f };
    }
}

static void color_gradient(float t, unsigned char& r, unsigned char& g, unsigned char& b) {
    t = std::max(0.f, std::min(1.f, t));
    if (t < 0.25f) { float s=t/0.25f; r=0; g=(unsigned char)(s*255); b=255; }
    else if (t < 0.5f) { float s=(t-0.25f)/0.25f; r=0; g=255; b=(unsigned char)((1-s)*255); }
    else if (t < 0.75f) { float s=(t-0.5f)/0.25f; r=(unsigned char)(s*255); g=255; b=0; }
    else { float s=(t-0.75f)/0.25f; r=255; g=(unsigned char)((1-s)*255); b=0; }
}


static void generate_tile(int zoom, int tx, int ty) {
    const int W = 256, H = 256;

    double n = std::pow(2.0, zoom);
    double nw_lon = tx / n * 360.0 - 180.0;
    double se_lon = (tx + 1) / n * 360.0 - 180.0;
    double nw_lat = std::atan(std::sinh(M_PI * (1.0 - 2.0 * ty / n))) * 180.0 / M_PI;
    double se_lat = std::atan(std::sinh(M_PI * (1.0 - 2.0 * (ty + 1) / n))) * 180.0 / M_PI;

    float radius = g_idw_radius_m;
    double margin = radius / 111320.0 * 2.0;

    struct Pt { double lat, lon; float val; };
    std::vector<Pt> pts;
    {
        std::lock_guard<std::mutex> lk(g_heat_points_mtx);
        for (auto& p : g_heat_points) {
            if (p.lat < se_lat - margin || p.lat > nw_lat + margin) continue;
            if (p.lon < nw_lon - margin || p.lon > se_lon + margin) continue;
            if (g_heat_earfcn_filter != 0 && p.earfcn != g_heat_earfcn_filter) continue;
            if (g_heat_pci_filter != 0 && p.pci != g_heat_pci_filter)   continue;
            float v = get_point_value(p);
            if (std::isnan(v)) continue;
            pts.push_back({ p.lat, p.lon, v });
        }
    }

    std::string path = tile_path(zoom, tx, ty);
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());

    std::vector<unsigned char> image(W * H * 4, 0);

    if (!pts.empty()) {
        auto [v_min, v_max] = metric_range();
        double lat_step = (nw_lat - se_lat) / H;
        double lon_step = (se_lon - nw_lon) / W;
        const double DEG2M = 111320.0;

        for (int py = 0; py < H; ++py) {
            double lat = nw_lat - py * lat_step;
            for (int px = 0; px < W; ++px) {
                double lon = nw_lon + px * lon_step;

                double num = 0, den = 0;
                for (auto& p : pts) {
                    double dlat = (p.lat - lat) * DEG2M;
                    double dlon = (p.lon - lon) * DEG2M * std::cos(lat * M_PI / 180.0);
                    double dist = std::sqrt(dlat*dlat + dlon*dlon);
                    if (dist > radius) continue;
                    if (dist < 0.5) { num = p.val; den = 1; break; }
                    double w = 1.0 / std::pow(dist, (double)g_idw_power);
                    num += w * p.val;
                    den += w;
                }
                if (den < 1e-12) continue;

                float t = ((float)(num / den) - v_min) / (v_max - v_min);
                unsigned char r, g, b;
                color_gradient(t, r, g, b);
                int i = (py * W + px) * 4;
                image[i]=r; image[i+1]=g; image[i+2]=b; image[i+3]=180;
            }
        }
    }

    stbi_write_png(path.c_str(), W, H, 4, image.data(), W * 4);
}


void heat_add_points(const LocationData& loc) {
    if (!loc.has_cell) return;
    std::lock_guard<std::mutex> lk(g_heat_points_mtx);
    for (auto& c : loc.cells) {
        if (c.pci <= 0) continue;
        g_heat_points.push_back({ loc.lat, loc.lon, loc.alt,
                                   c.rsrp, c.rsrq, c.rssi, c.earfcn, c.pci });
    }
}

void heat_request_tile(int zoom, int tx, int ty) {
    auto key = std::make_tuple(zoom, tx, ty);

    {
        std::lock_guard<std::mutex> lk(s_cache_mtx);
        if (s_cache.count(key)) return; 
    }

    std::lock_guard<std::mutex> lk(s_q_mtx);
    if (s_q_set.count(key)) return; 
    s_q_set.insert(key);
    s_queue.push(key);
    s_q_cv.notify_one();
}

HeatTile heat_get_tile(int zoom, int tx, int ty) {
    auto key = std::make_tuple(zoom, tx, ty);

    {
        std::lock_guard<std::mutex> lk(s_cache_mtx);
        auto it = s_cache.find(key);
        if (it != s_cache.end()) return it->second;
    }

    std::string path = tile_path(zoom, tx, ty);
    if (!std::filesystem::exists(path)) return {};

    int w, h, ch;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!data) return {};

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);

    HeatTile tile{ tex, true, 0.65f };
    std::lock_guard<std::mutex> lk(s_cache_mtx);
    s_cache[key] = tile;
    return tile;
}

void heat_invalidate_cache() {
    try {
        if (std::filesystem::exists("build")) {
            for (auto& e : std::filesystem::recursive_directory_iterator("build")) {
                if (e.is_regular_file() &&
                    e.path().filename().string().find("_heat_") != std::string::npos)
                    std::filesystem::remove(e.path());
            }
        }
    } catch (...) {}

    {
        std::lock_guard<std::mutex> lk(s_cache_mtx);
        for (auto& [k, t] : s_cache)
            if (t.tex_id) glDeleteTextures(1, &t.tex_id);
        s_cache.clear();
    }
    {
        std::lock_guard<std::mutex> lk(s_q_mtx);
        while (!s_queue.empty()) s_queue.pop();
        s_q_set.clear();
    }
}

void heat_worker_thread() {
    while (s_running) {
        std::tuple<int,int,int> task;
        {
            std::unique_lock<std::mutex> lk(s_q_mtx);
            s_q_cv.wait_for(lk, std::chrono::milliseconds(200),
                [&] { return !s_queue.empty() || !s_running; });
            if (!s_running) break;
            if (s_queue.empty()) continue;
            task = s_queue.front();
            s_queue.pop();
            s_q_set.erase(task);
        }
        auto [zoom, tx, ty] = task;
        generate_tile(zoom, tx, ty);
    }
}
