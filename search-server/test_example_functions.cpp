#include "document.h"
#include "test_example_functions.h"
#include "search_server.h"
#include <string>
#include <vector>

void AddDocument(SearchServer& search_server, int id, const std::string& document, DocumentStatus status,
                     const std::vector<int>& ratings)
{
    search_server.AddDocument(id, document, status, ratings);
}