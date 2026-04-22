#include "map_engine.h"
#include "stb_image.h"
#include <cmath>
#include <filesystem>
#include <sstream>
#include <iostream>

namespace fs = std::filesystem;

MapView g_map_view;
std::mutex tile_cache_mtx;
std::unordered_map<TileID, TileTexture, TileIDHash> tile_cache;
std::mutex tile_queue_mtx;
std::condition_variable tile_queue_cv;
std::queue<TileID> tile_load_queue;
std::atomic<bool> tile_loader_running{true};

TileID lat_lon_to_tile(double lat, double lon, int zoom) {
    double lat_rad = lat * M_PI / 180.0;
    double n = std::pow(2.0, zoom);
    int x = (int)std::floor((lon + 180.0) / 360.0 * n);
    int y = (int)std::floor((1.0 - std::log(std::tan(lat_rad) + 1.0 / std::cos(lat_rad)) / M_PI) / 2.0 * n);
    int max_idx = (int)n - 1;
    return {zoom, std::max(0, std::min(x, max_idx)), std::max(0, std::min(y, max_idx))};
}

void lat_lon_to_pixel_offset(double lat, double lon, int zoom, int tile_x, int tile_y, double& px, double& py) {
    double n = std::pow(2.0, zoom);
    double x_frac = (lon + 180.0) / 360.0 * n - tile_x;
    double lat_r = lat * M_PI / 180.0;
    double y_frac = (1.0 - std::log(std::tan(lat_r) + 1.0 / std::cos(lat_r)) / M_PI) / 2.0 * n - tile_y;
    px = x_frac * 256.0; py = y_frac * 256.0;
}

std::string tile_cache_path(const TileID& t) {
    std::ostringstream ss;
    ss << "build/" << t.zoom << "/" << t.x << "/" << t.y << ".png";
    return ss.str();
}

bool tile_exists_on_disk(const TileID& t) { return fs::exists(tile_cache_path(t)); }

bool download_tile(const TileID& t) {
    std::string path = tile_cache_path(t);
    fs::create_directories(fs::path(path).parent_path());
    std::string url = "https://tile.openstreetmap.org/" + std::to_string(t.zoom) + "/" + std::to_string(t.x) + "/" + std::to_string(t.y) + ".png";
    std::string cmd = "curl -s -A \"TileViewer/1.0\" --max-time 10 -o \"" + path + "\" \"" + url + "\"";
    return (std::system(cmd.c_str()) == 0) && fs::exists(path);
}

bool load_tile_texture(const TileID& t) {
    std::string path = tile_cache_path(t);
    int w, h, ch;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!data) return false;
    GLuint tex; glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);
    std::lock_guard<std::mutex> lk(tile_cache_mtx);
    tile_cache[t] = {tex, w, h, true};
    return true;
}

void tile_loader_thread() {
    while (tile_loader_running) {
        TileID t{};
        {
            std::unique_lock<std::mutex> lk(tile_queue_mtx);
            tile_queue_cv.wait_for(lk, std::chrono::milliseconds(200), [&]{ return !tile_load_queue.empty() || !tile_loader_running; });
            if (tile_load_queue.empty()) continue;
            t = tile_load_queue.front(); tile_load_queue.pop();
        }
        {
            std::lock_guard<std::mutex> lk(tile_cache_mtx);
            if (tile_cache.count(t) && tile_cache[t].loaded) continue;
        }
        if (!tile_exists_on_disk(t)) download_tile(t);
    }
}

void request_tile(const TileID& t) {
    { std::lock_guard<std::mutex> lk(tile_cache_mtx); if (tile_cache.count(t) && tile_cache[t].loaded) return; }
    { std::lock_guard<std::mutex> lk(tile_queue_mtx); tile_load_queue.push(t); }
    tile_queue_cv.notify_one();
}