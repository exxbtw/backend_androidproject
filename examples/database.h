#pragma once
#include <libpq-fe.h>
#include <string>
#include <vector>

extern PGconn* db_conn;
void db_connect();
void db_disconnect();

struct LocationData; 
struct CellInfoData;

void db_insert(LocationData* loc, const CellInfoData& c);