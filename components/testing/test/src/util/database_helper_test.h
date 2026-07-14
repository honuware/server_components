#pragma once

#include "sql_util/database_access/database_helper.h"

DatabaseHelper MakeTestDatabaseHelper(std::string_view databaseName);