#pragma once

#include <vector>
#include <string>
#include <mutex>
#include <atomic>
#include <map>
#include "json.hpp" 

using json = nlohmann::json;

struct CellInfoData {
    std::string type;
    int pci = 0;
    int tac = 0;
    int earfcn = 0;
    int rsrp = 0;
    int rsrq = 0;
    int rssi = 0;
    int ta = 0;
    int lac = 0;
    int cid = 0;
    int bsic = 0;
    int arfcn = 0;
    int psc = 0;
    long long nci = 0;
    int nrarfcn = 0;
    int band = 0;
    int mcc = 0;
    int mnc = 0;
    int asu = 0;
    int cqi = 0;
    int rssnr = 0;
    int ss_rsrp = 0;
    int ss_rsrq = 0;
    int ss_sinr = 0;
    bool is_primary = false;
};

struct PciHistory {
    std::vector<double> timestamps;
    std::vector<double> rsrp;
    std::vector<double> rssi;
    std::vector<double> sinr;
};

struct LocationData {
    float lat = 0, lon = 0, alt = 0;
    long long timestamp = 0;
    std::vector<CellInfoData> cells;
    bool has_cell = false;
    
    std::mutex mtx;
    std::atomic<bool> is_running{true};

    LocationData() : lat(0), lon(0), alt(0), timestamp(0), has_cell(false) {
        is_running.store(true);
    }

    LocationData(const LocationData& other) {
        lat = other.lat;
        lon = other.lon;
        alt = other.alt;
        timestamp = other.timestamp;
        cells = other.cells;
        has_cell = other.has_cell;
        is_running.store(other.is_running.load());
    }

    LocationData& operator=(const LocationData& other) {
        if (this != &other) {
            lat = other.lat;
            lon = other.lon;
            alt = other.alt;
            timestamp = other.timestamp;
            cells = other.cells;
            has_cell = other.has_cell;
            is_running.store(other.is_running.load());
        }
        return *this;
    }
};

extern std::mutex history_mtx;
extern std::map<int, PciHistory> pci_histories;


CellInfoData parse_cell(const json& cell);

void update_history(const std::vector<CellInfoData>& cells, long long ts);

void load_log_from_file();

void run_server(LocationData* loc);