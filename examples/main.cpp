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

using json = nlohmann::json;

const int MAX_HISTORY = 2000;

std::vector<int> rsrp_history;
std::vector<long long> timestamp_history;

long long last_timestamp = 0;

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
};

struct LocationData {
    float lat = 0.0f;
    float lon = 0.0f;
    float alt = 0.0f;
    long long timestamp = 0;

    CellInfoData cell;
    bool has_cell = false;

    std::mutex mtx;
    volatile bool is_running;
};

void load_log_from_file() {
    std::ifstream file("location_log.json");
    if (!file.is_open()) return;

    std::string line;

    while (std::getline(file, line)) {
        try {
            auto j = json::parse(line);

            if (!j.contains("time")) continue;

            long long ts = j["time"].get<long long>();

            if (j.contains("cell_info") && !j["cell_info"].empty()) {
                auto cell = j["cell_info"][0];

                int rsrp = cell.value("rsrp", 0);

                rsrp_history.push_back(rsrp);
                timestamp_history.push_back(ts);
            }

        } catch (...) {
        }
    }

    if (rsrp_history.size() > MAX_HISTORY) {
        rsrp_history.erase(rsrp_history.begin(),
                           rsrp_history.end() - MAX_HISTORY);
        timestamp_history.erase(timestamp_history.begin(),
                                timestamp_history.end() - MAX_HISTORY);
    }

    if (!timestamp_history.empty())
        last_timestamp = timestamp_history.back();
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

            std::lock_guard<std::mutex> lock(loc->mtx);

            loc->lat = j["latitude"].get<double>();
            loc->lon = j["longitude"].get<double>();
            loc->alt = j["altitude"].get<double>();
            loc->timestamp = j["time"].get<long long>();

            loc->has_cell = false;

            if (j.contains("cell_info") && !j["cell_info"].empty()) {

                auto cell = j["cell_info"][0];

                loc->cell.type   = cell.value("type", "");
                loc->cell.pci    = cell.value("pci", 0);
                loc->cell.tac    = cell.value("tac", 0);
                loc->cell.earfcn = cell.value("earfcn", 0);

                loc->cell.rsrp   = cell.value("rsrp", 0);
                loc->cell.rsrq   = cell.value("rsrq", 0);
                loc->cell.rssi   = cell.value("rssi", 0);
                loc->cell.ta     = cell.value("ta", 0);

                loc->cell.lac   = cell.value("lac", 0);
                loc->cell.cid   = cell.value("cid", 0);
                loc->cell.bsic  = cell.value("bsic", 0);
                loc->cell.arfcn = cell.value("arfcn", 0);
                loc->cell.psc   = cell.value("psc", 0);

                loc->cell.nci      = cell.value("nci", 0LL);
                loc->cell.nrarfcn  = cell.value("nrarfcn", 0);

                loc->cell.band = cell.value("band", 0);
                loc->cell.mcc  = cell.value("mcc", 0);
                loc->cell.mnc  = cell.value("mnc", 0);

                loc->cell.asu   = cell.value("asu", 0);
                loc->cell.cqi   = cell.value("cqi", 0);
                loc->cell.rssnr = cell.value("rssnr", 0);

                loc->cell.ss_rsrp = cell.value("ss_rsrp", 0);
                loc->cell.ss_rsrq = cell.value("ss_rsrq", 0);
                loc->cell.ss_sinr = cell.value("ss_sinr", 0);

                loc->has_cell = true;
        }

            std::ofstream file("location_log.json", std::ios::app);
            file << j.dump() << std::endl;

            socket.send(zmq::str_buffer("OK"), zmq::send_flags::none);

        } catch (...) {
            socket.send(zmq::str_buffer("Error"), zmq::send_flags::none);
        }
    }
}

int main(int argc, char *argv[]) {

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
        1024,
        768,
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
        CellInfoData currentCell;
        bool hasCell;

        {
            std::lock_guard<std::mutex> lock(locationInfo.mtx);

            currentLat = locationInfo.lat;
            currentLon = locationInfo.lon;
            currentAlt = locationInfo.alt;
            currentTime = locationInfo.timestamp;
            currentCell = locationInfo.cell;
            hasCell = locationInfo.has_cell;
        }

        if (hasCell && currentTime != last_timestamp) {

            last_timestamp = currentTime;

            rsrp_history.push_back(currentCell.rsrp);
            timestamp_history.push_back(currentTime);

            if (rsrp_history.size() > MAX_HISTORY) {
                rsrp_history.erase(rsrp_history.begin());
                timestamp_history.erase(timestamp_history.begin());
            }
        }

        ImGui::Begin("Smartphone Data");

        ImGui::Text("Latitude: %.6f", currentLat);
        ImGui::Text("Longitude: %.6f", currentLon);
        ImGui::Text("Altitude: %.2f m", currentAlt);

        ImGui::Separator();

        if (hasCell) {
            ImGui::Text("Cell Type: %s", currentCell.type.c_str());
            ImGui::Text("RSRP: %d dBm", currentCell.rsrp);
            ImGui::Text("RSRQ: %d", currentCell.rsrq);
            ImGui::Text("RSSI: %d", currentCell.rssi);
        }

        ImGui::Separator();

        if (!rsrp_history.empty()) {

            if (ImPlot::BeginPlot("Signal Strength (RSRP)", ImVec2(-1,300))) {

                ImPlot::SetupAxes("Time (sec)", "RSRP (dBm)");

                std::vector<double> x(rsrp_history.size());
                std::vector<double> y(rsrp_history.size());

                long long base = timestamp_history.front();

                for (size_t i = 0; i < rsrp_history.size(); i++) {

                    x[i] = (timestamp_history[i] - base) / 1000.0;
                    y[i] = rsrp_history[i];
                }

                ImPlot::PlotLine("RSRP", x.data(), y.data(), x.size());

                ImPlot::EndPlot();
            }
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

    return 0;
}