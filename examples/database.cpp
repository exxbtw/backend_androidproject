#include "database.h"
#include "telemetry.h"
#include <iostream>

PGconn* db_conn = nullptr;
const char* conninfo = "host=localhost port=5432 dbname=mobile_data user=postgres password=2643";

void db_connect() {
    db_conn = PQconnectdb(conninfo);
    if (PQstatus(db_conn) != CONNECTION_OK) {
        std::cerr << "DB error: " << PQerrorMessage(db_conn) << std::endl;
    } else {
        std::cout << "DB OK\n";
    }
}

void db_disconnect() {
    if (db_conn) PQfinish(db_conn);
}

void db_insert(LocationData* loc, const CellInfoData& c) {
    if (!db_conn) return;
    std::string query =
        "INSERT INTO cell_data (lat, lon, alt, timestamp, type, pci, tac, cid, lac, nci, "
        "earfcn, nrarfcn, arfcn, band, rsrp, rsrq, rssi, rssnr, sinr, ss_rsrp, ss_rsrq, ss_sinr, "
        "ta, cqi, asu, mcc, mnc, psc, bsic) VALUES (" +
        std::to_string(loc->lat) + "," + std::to_string(loc->lon) + "," +
        std::to_string(loc->alt) + "," + std::to_string(loc->timestamp) + ",'" +
        c.type + "'," + std::to_string(c.pci) + "," + std::to_string(c.tac) + "," +
        std::to_string(c.cid) + "," + std::to_string(c.lac) + "," + std::to_string(c.nci) + "," +
        std::to_string(c.earfcn) + "," + std::to_string(c.nrarfcn) + "," + std::to_string(c.arfcn) + "," +
        std::to_string(c.band) + "," + std::to_string(c.rsrp) + "," + std::to_string(c.rsrq) + "," +
        std::to_string(c.rssi) + "," + std::to_string(c.rssnr) + "," +
        std::to_string((c.ss_sinr != 0) ? c.ss_sinr : c.rssnr) + "," +
        std::to_string(c.ss_rsrp) + "," + std::to_string(c.ss_rsrq) + "," + std::to_string(c.ss_sinr) + "," +
        std::to_string(c.ta) + "," + std::to_string(c.cqi) + "," + std::to_string(c.asu) + "," +
        std::to_string(c.mcc) + "," + std::to_string(c.mnc) + "," + std::to_string(c.psc) + "," +
        std::to_string(c.bsic) + ");";

    PGresult* res = PQexec(db_conn, query.c_str());
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        std::cerr << "Insert error: " << PQerrorMessage(db_conn) << std::endl;
    }
    PQclear(res);
}