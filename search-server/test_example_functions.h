#pragma once
#include "document.h"
#include "search_server.h"
#include <string>
#include <vector>


void AddDocument(SearchServer& search_server, int id, const std::string& document, DocumentStatus status,
                     const std::vector<int>& ratings);