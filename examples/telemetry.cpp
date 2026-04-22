#include "telemetry.h"
#include "database.h"
#include <zmq.hpp>
#include <fstream>
#include <iostream>

std::mutex history_mtx;
std::map<int, PciHistory> pci_histories;
long long base_time = 0;
bool base_time_set = false;
const int MAX_HISTORY = 50000;

CellInfoData parse_cell(const json& cell) {
    CellInfoData c;
    c.type = cell.value("type", "");
    c.pci = cell.value("pci", 0);
    c.tac = cell.value("tac", 0);
    c.earfcn = cell.value("earfcn", 0);
    c.rsrp = cell.value("rsrp", 0);
    c.rsrq = cell.value("rsrq", 0);
    c.rssi = cell.value("rssi", 0);
    c.ta = cell.value("ta", 0);
    c.lac = cell.value("lac", 0);
    c.cid = cell.value("cid", 0);
    c.bsic = cell.value("bsic", 0);
    c.arfcn = cell.value("arfcn", 0);
    c.psc = cell.value("psc", 0);
    c.nci = cell.value("nci", 0LL);
    c.nrarfcn = cell.value("nrarfcn", 0);
    c.band = cell.value("band", 0);
    c.mcc = cell.value("mcc", 0);
    c.mnc = cell.value("mnc", 0);
    c.asu = cell.value("asu", 0);
    c.cqi = cell.value("cqi", 0);
    c.rssnr = cell.value("rssnr", 0);
    c.ss_rsrp = cell.value("ss_rsrp", 0);
    c.ss_rsrq = cell.value("ss_rsrq", 0);
    c.ss_sinr = cell.value("ss_sinr", 0);
    c.is_primary = cell.value("is_primary", false);
    return c;
}

void update_history(const std::vector<CellInfoData>& cells, long long ts) {
    if (!base_time_set) {
        base_time = ts;
        base_time_set = true;
    }
    double t_sec = (ts - base_time) / 1000.0;

    std::lock_guard<std::mutex> lock(history_mtx);
    for (auto& c : cells) {
        if (c.pci <= 0) continue;

        int rsrp = (c.rsrp < -200 || c.rsrp > 0) ? 0 : c.rsrp;
        int rssi = (c.rssi < -200 || c.rssi > 0) ? 0 : c.rssi;
        int sinr = (c.ss_sinr != 0) ? c.ss_sinr : c.rssnr;
        if (sinr < -50 || sinr > 50) sinr = 0;

        auto& h = pci_histories[c.pci];
        h.timestamps.push_back(t_sec);
        h.rsrp.push_back(rsrp);
        h.rssi.push_back(rssi);
        h.sinr.push_back(sinr);

        if ((int)h.timestamps.size() > MAX_HISTORY) {
            h.timestamps.erase(h.timestamps.begin());
            h.rsrp.erase(h.rsrp.begin());
            h.rssi.erase(h.rssi.begin());
            h.sinr.erase(h.sinr.begin());
        }
    }
}

void load_log_from_file() {
    std::ifstream file("location_log.json");
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        try {
            auto j = json::parse(line);
            if (!j.contains("time") || !j.contains("cell_info")) continue;

            long long ts = j["time"].get<long long>();
            if (!base_time_set) {
                base_time = ts;
                base_time_set = true;
            }
            double t_sec = (ts - base_time) / 1000.0;

            for (auto& cell_j : j["cell_info"]) {
                int pci = cell_j.value("pci", -1);
                if (pci < 0) continue;

                auto& h = pci_histories[pci];
                h.timestamps.push_back(t_sec);
                h.rsrp.push_back(cell_j.value("rsrp", 0));
                h.rssi.push_back(cell_j.value("rssi", 0));
                int sinr = cell_j.value("ss_sinr", 0);
                if (sinr == 0) sinr = cell_j.value("rssnr", 0);
                h.sinr.push_back(sinr);
            }
        } catch (...) { continue; }
    }
}

void run_server(LocationData* loc) {
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::rep);
    
    try {
        socket.bind("tcp://*:5566");
    } catch (const std::exception& e) {
        std::cerr << "ZMQ Bind Error: " << e.what() << std::endl;
        return;
    }

    socket.set(zmq::sockopt::rcvtimeo, 1000);

    while (loc->is_running) {
        zmq::message_t request;
        auto res = socket.recv(request, zmq::recv_flags::none);
        if (!res) continue;

        std::string msg_str(static_cast<char*>(request.data()), request.size());
        try {
            auto j = json::parse(msg_str);
            std::vector<CellInfoData> cells;
            if (j.contains("cell_info")) {
                for (auto& cell_j : j["cell_info"]) {
                    cells.push_back(parse_cell(cell_j));
                }
            }

            long long ts = j["time"].get<long long>();
            {
                std::lock_guard<std::mutex> lock(loc->mtx);
                loc->lat = j.value("latitude", 0.0);
                loc->lon = j.value("longitude", 0.0);
                loc->alt = j.value("altitude", 0.0);
                loc->timestamp = ts;
                loc->cells = cells;
                loc->has_cell = !cells.empty();
            }

            update_history(cells, ts);

            std::ofstream log_file("location_log.json", std::ios::app);
            log_file << j.dump() << std::endl;

            for (auto& c : cells) {
                db_insert(loc, c);
            }

            socket.send(zmq::str_buffer("OK"), zmq::send_flags::none);
        } catch (...) {
            socket.send(zmq::str_buffer("Error"), zmq::send_flags::none);
        }
    }
}