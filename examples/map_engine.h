#pragma once
#include <GL/glew.h>
#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <queue>
#include <condition_variable>

struct TileID {
    int zoom, x, y;
    bool operator==(const TileID& o) const { return zoom == o.zoom && x == o.x && y == o.y; }
};

struct TileIDHash {
    size_t operator()(const TileID& t) const {
        size_t h = std::hash<int>{}(t.zoom);
        h ^= std::hash<int>{}(t.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(t.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

struct TileTexture {
    GLuint tex_id = 0;
    int width = 0, height = 0;
    bool loaded = false;
};

struct MapView {
    double center_lat = 55.0087508;
    double center_lon = 82.9385076;
    int zoom = 15;
};

extern MapView g_map_view;
extern std::mutex tile_cache_mtx;
extern std::unordered_map<TileID, TileTexture, TileIDHash> tile_cache;
extern std::atomic<bool> tile_loader_running;

void tile_loader_thread();
void request_tile(const TileID& t);
bool load_tile_texture(const TileID& t);
bool tile_exists_on_disk(const TileID& t);
TileID lat_lon_to_tile(double lat, double lon, int zoom);
void lat_lon_to_pixel_offset(double lat, double lon, int zoom, int tile_x, int tile_y, double& px, double& py);