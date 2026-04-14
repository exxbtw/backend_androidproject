#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <iostream>
#include <cmath>
#include <mutex>
#include <string>
#include <fstream>
#include <thread>
#include <atomic>
#include <zmq.hpp>
#include "json.hpp"

#include "imgui.h"
#include "implot.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"

#include <vector>
#include <map>

#include <libpq-fe.h>

PGconn* db_conn;

const char* conninfo = "host=localhost port=5432 dbname=mobile_data user=postgres password=2643";

using json = nlohmann::json;

const int MAX_HISTORY = 50000;

struct PciHistory {
    std::vector<double> timestamps;
    std::vector<double> rsrp;
    std::vector<double> rssi;
    std::vector<double> sinr;
};

long long base_time = 0;
bool base_time_set = false;

std::mutex history_mtx;
std::map<int, PciHistory> pci_histories;

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
    long long nci = 0;
    int band = 0;
    int mcc = 0;
    int mnc = 0;
    int asu = 0;
    int cqi = 0;
    int rssnr = 0;
    int bsic = 0;
    int arfcn = 0;
    int psc = 0;
    int ss_rsrp = 0;
    int ss_rsrq = 0;
    int ss_sinr = 0;
    int nrarfcn = 0;

    bool is_primary = false;
};

struct LocationData {
    float lat = 0.0f;
    float lon = 0.0f;
    float alt = 0.0f;
    long long timestamp = 0;

    std::vector<CellInfoData> cells;
    bool has_cell = false;

    std::mutex mtx;
    std::atomic<bool> is_running;
};

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

void load_log_from_file() {
    std::ifstream file("location_log.json");
    if (!file.is_open()) return;

    std::string line;

    while (std::getline(file, line)) {
        try {
            auto j = json::parse(line);

            if (!j.contains("time")) continue;

            long long ts = j["time"].get<long long>();

            if (!base_time_set) {
                base_time = ts;
                base_time_set = true;
            }
            double t_sec = (ts - base_time) / 1000.0;

            if (!j.contains("cell_info")) continue;

            for (auto& cell : j["cell_info"]) {
                int pci = cell.value("pci", -1);
                if (pci < 0) continue;

                int rsrp = cell.value("rsrp", 0);
                int rssi = cell.value("rssi", 0);
                int sinr = cell.value("ss_sinr", 0);
                if (sinr == 0) sinr = cell.value("rssnr", 0);

                if (rsrp < -500 || rsrp > 500) rsrp = 0;
                if (rssi < -200 || rssi > 0) rssi = 0;
                if (sinr < -50  || sinr > 50) sinr = 0;

                auto& h = pci_histories[pci];
                h.timestamps.push_back(t_sec);
                h.rsrp.push_back(rsrp);
                h.rssi.push_back(rssi);
                h.sinr.push_back(sinr);
            }

        } catch (...) {
        }
    }

    for (auto& [pci, h] : pci_histories) {
        if ((int)h.timestamps.size() > MAX_HISTORY) {
            int excess = (int)h.timestamps.size() - MAX_HISTORY;
            h.timestamps.erase(h.timestamps.begin(), h.timestamps.begin() + excess);
            h.rsrp.erase(h.rsrp.begin(), h.rsrp.begin() + excess);
            h.rssi.erase(h.rssi.begin(), h.rssi.begin() + excess);
            h.sinr.erase(h.sinr.begin(), h.sinr.begin() + excess);
        }
    }
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

void db_insert(LocationData* loc, const CellInfoData& c) {
    std::string query =
        "INSERT INTO cell_data ("
        "lat, lon, alt, timestamp, type, "
        "pci, tac, cid, lac, nci, "
        "earfcn, nrarfcn, arfcn, band, "
        "rsrp, rsrq, rssi, rssnr, sinr, "
        "ss_rsrp, ss_rsrq, ss_sinr, "
        "ta, cqi, asu, "
        "mcc, mnc, "
        "psc, bsic"
        ") VALUES (" +

        std::to_string(loc->lat) + "," +
        std::to_string(loc->lon) + "," +
        std::to_string(loc->alt) + "," +
        std::to_string(loc->timestamp) + ",'" +

        c.type + "'," +

        std::to_string(c.pci) + "," +
        std::to_string(c.tac) + "," +
        std::to_string(c.cid) + "," +
        std::to_string(c.lac) + "," +
        std::to_string(c.nci) + "," +

        std::to_string(c.earfcn) + "," +
        std::to_string(c.nrarfcn) + "," +
        std::to_string(c.arfcn) + "," +
        std::to_string(c.band) + "," +

        std::to_string(c.rsrp) + "," +
        std::to_string(c.rsrq) + "," +
        std::to_string(c.rssi) + "," +
        std::to_string(c.rssnr) + "," +
        std::to_string((c.ss_sinr != 0) ? c.ss_sinr : c.rssnr) + "," +

        std::to_string(c.ss_rsrp) + "," +
        std::to_string(c.ss_rsrq) + "," +
        std::to_string(c.ss_sinr) + "," +

        std::to_string(c.ta) + "," +
        std::to_string(c.cqi) + "," +
        std::to_string(c.asu) + "," +

        std::to_string(c.mcc) + "," +
        std::to_string(c.mnc) + "," +

        std::to_string(c.psc) + "," +
        std::to_string(c.bsic) +
        ");";

    PGresult* res = PQexec(db_conn, query.c_str());

    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        std::cerr << "Insert error: " << PQerrorMessage(db_conn) << std::endl;
    }

    PQclear(res);
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

    int timeout = 1000;
    socket.set(zmq::sockopt::rcvtimeo, timeout);

    while (loc->is_running) {

        zmq::message_t request;
        auto res = socket.recv(request, zmq::recv_flags::none);

        if (!res) continue;

        std::string msg_str(static_cast<char*>(request.data()), request.size());

        try {

            auto j = json::parse(msg_str);

            std::vector<CellInfoData> cells;
            if (j.contains("cell_info") && !j["cell_info"].empty()) {
                for (auto& cell : j["cell_info"]) {
                    cells.push_back(parse_cell(cell));
                }
            }

            long long ts = j["time"].get<long long>();
            float lat = j["latitude"].get<double>();
            float lon = j["longitude"].get<double>();
            float alt = j["altitude"].get<double>();

            {
                std::lock_guard<std::mutex> lock(loc->mtx);
                loc->lat = lat;
                loc->lon = lon;
                loc->alt = alt;
                loc->timestamp = ts;
                loc->cells = cells;
                loc->has_cell = !cells.empty();
            }

            update_history(cells, ts);

            std::ofstream file("location_log.json", std::ios::app);
            file << j.dump() << std::endl;

            for (auto& c : cells) {
                db_insert(loc, c);
            }

            socket.send(zmq::str_buffer("OK"), zmq::send_flags::none);

        } catch (...) {
            socket.send(zmq::str_buffer("Error"), zmq::send_flags::none);
        }
    }
}

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

int main(int argc, char *argv[]) {

    db_conn = PQconnectdb(conninfo);

    if (PQstatus(db_conn) != CONNECTION_OK) {
        std::cerr << "DB error: " << PQerrorMessage(db_conn) << std::endl;
    } else {
        std::cout << "DB OK\n";
    }

    static LocationData locationInfo;
    locationInfo.is_running = true;

    load_log_from_file();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL Init failed: " << SDL_GetError() << std::endl;
        return -1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

    SDL_Window* window = SDL_CreateWindow(
        "Backend",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1280,
        900,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);

    glewExperimental = GL_TRUE;
    glewInit();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGui::StyleColorsDark();

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    std::thread server_thread(run_server, &locationInfo);

    bool running = true;

    while (running) {

        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        float currentLat, currentLon, currentAlt;
        long long currentTime;
        std::vector<CellInfoData> currentCells;
        bool hasCell;

        {
            std::lock_guard<std::mutex> lock(locationInfo.mtx);

            currentLat = locationInfo.lat;
            currentLon = locationInfo.lon;
            currentAlt = locationInfo.alt;
            currentTime = locationInfo.timestamp;
            currentCells = locationInfo.cells;
            hasCell = locationInfo.has_cell;
        }

        std::map<int, PciHistory> histories_copy;
        {
            std::lock_guard<std::mutex> lock(history_mtx);
            histories_copy = pci_histories;
        }

        ImGui::Begin("Smartphone Data");

        ImGui::Text("Latitude: %.6f", currentLat);
        ImGui::Text("Longitude: %.6f", currentLon);
        ImGui::Text("Altitude: %.2f m", currentAlt);

        ImGui::Separator();

        if (hasCell) {
            for (auto& c : currentCells) {
                if (!c.is_primary) continue;
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
                    "[Primary] Type: %s  PCI: %d", c.type.c_str(), c.pci);
                ImGui::Text("  RSRP: %d dBm", c.rsrp);
                ImGui::Text("  RSRQ: %d", c.rsrq);
                ImGui::Text("  RSSI: %d", c.rssi);
                ImGui::Text("  SINR: %d", (c.ss_sinr != 0) ? c.ss_sinr : c.rssnr);
            }

            ImGui::Separator();

            for (auto& c : currentCells) {
                ImGui::Text("  [%s] PCI=%-4d RSRP=%-5d RSSI=%-5d SINR=%-5d %s",
                    c.type.c_str(),
                    c.pci,
                    c.rsrp,
                    c.rssi,
                    (c.ss_sinr != 0) ? c.ss_sinr : c.rssnr,
                    c.is_primary ? "<- primary" : "");
            }
        }

        ImGui::Separator();

        if (!histories_copy.empty()) {
            plot_metric("RSRP##plot", "RSRP (dBm)", histories_copy, 0);
            ImGui::Spacing();
            plot_metric("RSSI##plot", "RSSI (dBm)", histories_copy, 1);
            ImGui::Spacing();
            plot_metric("SINR##plot", "SINR (dB)",  histories_copy, 2);
        }

        ImGui::End();

        ImGui::Render();

        int display_w, display_h;

        SDL_GL_GetDrawableSize(window, &display_w, &display_h);

        glViewport(0,0,display_w,display_h);
        glClearColor(0.1f,0.1f,0.1f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        SDL_GL_SwapWindow(window);
    }

    locationInfo.is_running = false;

    if (server_thread.joinable())
        server_thread.join();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();

    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    PQfinish(db_conn);

    return 0;
}