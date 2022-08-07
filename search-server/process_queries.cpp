#include "process_queries.h"
#include <algorithm>
#include <execution>
#include <functional>
#include <numeric>
#include <list>
#include <vector>

std::vector<std::vector<Document>> ProcessQueries(
    const SearchServer& search_server,
    const std::vector<std::string>& queries) {
    std::vector<std::vector<Document>> result(queries.size());
    std::transform(std::execution::par, queries.begin(), queries.end(), result.begin(), [&search_server](const std::string& query){return search_server.FindTopDocuments(query);} );
    return result;
}

std::vector<Document> ProcessQueriesJoined(
    const SearchServer& search_server,
    const std::vector<std::string>& queries) {
        auto func = [](const std::vector<Document>& lhs, const std::vector<Document>& rhs) {
            std::vector<Document> result{lhs.begin(), lhs.end()};
            for (const auto& elem : rhs) {
                result.push_back(elem);
            }
            return result;
    };
    std::vector<Document> result = std::transform_reduce(std::execution::par, queries.begin(), queries.end(), std::vector<Document>{}, func, [&search_server](const std::string& query){return search_server.FindTopDocuments(query);} );
    return result;
}