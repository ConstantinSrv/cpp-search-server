#pragma once
#include "concurrent_map.h"
#include "document.h"
#include "log_duration.h"
#include "string_processing.h"
#include <algorithm>
#include <cmath>
#include <execution>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

const int MAX_RESULT_DOCUMENT_COUNT = 5;

const double DEVIATION = 1e-6;

class SearchServer
{
public:
    template <typename StringContainer>
    explicit SearchServer(const StringContainer &stop_words);

    explicit SearchServer(const std::string &stop_words_text);
    explicit SearchServer(const std::string_view stop_words_text);

    void AddDocument(int document_id, const std::string &document, DocumentStatus status,
                     const std::vector<int> &ratings);

    template <typename DocumentPredicate>
    std::vector<Document> FindTopDocuments(const std::string_view raw_query,
                                           DocumentPredicate document_predicate) const;
    template <typename DocumentPredicate, typename Policy>
    std::vector<Document> FindTopDocuments(const Policy &policy, std::string_view raw_query,
                                           DocumentPredicate document_predicate) const;

    std::vector<Document> FindTopDocuments(const std::string_view raw_query, DocumentStatus status) const;
    template <typename Policy>
    std::vector<Document> FindTopDocuments(const Policy &policy, const std::string_view raw_query, DocumentStatus status) const;

    std::vector<Document> FindTopDocuments(const std::string_view raw_query) const;
    template <typename Policy>
    std::vector<Document> FindTopDocuments(const Policy &policy, const std::string_view raw_query) const;

    int GetDocumentCount() const;
    std::set<int>::iterator begin();
    std::set<int>::iterator end();
    const std::map<std::string_view, double, std::less<>> &GetWordFrequencies(int document_id) const;
    void RemoveDocument(int document_id);
    void RemoveDocument(std::execution::sequenced_policy policy, int document_id);
    void RemoveDocument(std::execution::parallel_policy policy, int document_id);

    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(const std::string_view raw_query,
                                                                            int document_id) const;
    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(std::execution::sequenced_policy policy, const std::string_view raw_query,
                                                                            int document_id) const;
    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(std::execution::parallel_policy policy, const std::string_view raw_query,
                                                                            int document_id) const;

private:
    struct DocumentData
    {
        int rating = 0;
        DocumentStatus status;
        std::vector<std::string> data;
    };
    const std::set<std::string, std::less<>> stop_words_;
    std::map<std::string_view, std::map<int, double>, std::less<>> word_to_document_freqs_;
    std::map<int, std::map<std::string_view, double, std::less<>>> document_to_word_freqs_;
    std::map<int, DocumentData> documents_;
    std::set<int> document_ids_;

    bool IsStopWord(const std::string_view word) const;

    static bool IsValidWord(const std::string_view word);

    std::vector<std::string_view> SplitIntoWordsNoStop(std::string_view text) const;

    struct QueryWord
    {
        std::string_view data;
        bool is_minus = false;
        bool is_stop = false;
    };
    QueryWord ParseQueryWord(const std::string_view text) const;

    struct Query
    {
        std::set<std::string_view> plus_words;
        std::set<std::string_view> minus_words;
    };

    struct QueryParallel
    {
        std::vector<std::string_view> plus_words;
        std::vector<std::string_view> minus_words;
    };

    Query ParseQuery(const std::string_view text) const;
    QueryParallel ParseQueryParallel(const std::string_view text) const;
    // Existence required
    double ComputeWordInverseDocumentFreq(const std::string_view word) const;

    template <typename DocumentPredicate, typename Policy>
    std::vector<Document> FindAllDocuments(const Policy &policy, const Query &query,
                                           DocumentPredicate document_predicate) const;
    template <typename DocumentPredicate>
    std::vector<Document> FindAllDocumentsParallel(const Query &query,
                                                   DocumentPredicate document_predicate) const;
};

template <typename StringContainer>
SearchServer::SearchServer(const StringContainer &stop_words)
    : stop_words_(MakeUniqueNonEmptyStrings(stop_words)) // Extract non-empty stop words
{
    if (!all_of(stop_words_.begin(), stop_words_.end(), IsValidWord))
    {
        throw std::invalid_argument("Some of stop words are invalid");
    }
}

template <typename DocumentPredicate>
std::vector<Document> SearchServer::FindTopDocuments(const std::string_view raw_query,
                                                     DocumentPredicate document_predicate) const
{
    return FindTopDocuments(std::execution::seq, raw_query, document_predicate);
}

template <typename DocumentPredicate, typename Policy>
std::vector<Document> SearchServer::FindTopDocuments(const Policy &policy, const std::string_view raw_query,
                                                     DocumentPredicate document_predicate) const
{
    const auto query = ParseQuery(raw_query);

    auto matched_documents = FindAllDocuments(policy, query, document_predicate);

    sort(policy, matched_documents.begin(), matched_documents.end(),
         [](const Document &lhs, const Document &rhs)
         {
             return lhs.relevance > rhs.relevance || (std::abs(lhs.relevance - rhs.relevance) < DEVIATION && lhs.rating > rhs.rating);
         });
    if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT)
    {
        matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
    }
    return matched_documents;
}

template <typename Policy>
std::vector<Document> SearchServer::FindTopDocuments(const Policy &policy, const std::string_view raw_query, DocumentStatus status) const
{

    return FindTopDocuments(policy,
                            raw_query, [status](int document_id, DocumentStatus document_status, int rating)
                            { return document_status == status; });
}
template <typename Policy>
std::vector<Document> SearchServer::FindTopDocuments(const Policy &policy, const std::string_view raw_query) const
{
    return FindTopDocuments(policy,
                            raw_query, [](int document_id, DocumentStatus document_status, int rating)
                            { return document_status == DocumentStatus::ACTUAL; });
}

template <typename DocumentPredicate, typename Policy>
std::vector<Document> SearchServer::FindAllDocuments(const Policy &policy, const Query &query,
                                                     DocumentPredicate document_predicate) const
{
    if constexpr (std::is_same_v<std::remove_reference_t<Policy>,
                                 std::execution::sequenced_policy>)
    {
        std::map<int, double> document_to_relevance;
        for (const std::string_view word : query.plus_words)
        {
            if (word_to_document_freqs_.count(word) == 0)
            {
                continue;
            }
            const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
            for (const auto [document_id, term_freq] : word_to_document_freqs_.find(word)->second)
            {
                const auto &document_data = documents_.at(document_id);
                if (document_predicate(document_id, document_data.status, document_data.rating))
                {
                    document_to_relevance[document_id] += term_freq * inverse_document_freq;
                }
            }
        }
        for (const std::string_view word : query.minus_words)
        {
            if (word_to_document_freqs_.count(word) == 0)
            {
                continue;
            }
            for (const auto [document_id, _] : word_to_document_freqs_.find(word)->second)
            {
                document_to_relevance.erase(document_id);
            }
        }

        std::vector<Document> matched_documents;
        for (const auto [document_id, relevance] : document_to_relevance)
        {
            matched_documents.push_back(
                {document_id, relevance, documents_.at(document_id).rating});
        }
        return matched_documents;
    }
    else
    {
        ConcurrentMap<int, double> document_to_relevance_concurent(10);
        std::vector<std::string_view> plus_words(query.plus_words.begin(), query.plus_words.end());
        std::vector<std::string_view> minus_words(query.minus_words.begin(), query.minus_words.end());

        for_each(std::execution::par, plus_words.begin(), plus_words.end(), [this, &document_to_relevance_concurent, &document_predicate](const auto word)
                 {
        if (word_to_document_freqs_.count(word) == 0)
        {
            return;
        }
        const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
        for (const auto [document_id, term_freq] : word_to_document_freqs_.find(word)->second)
        {
            const auto &document_data = documents_.at(document_id);
            if (document_predicate(document_id, document_data.status, document_data.rating))
            {
                document_to_relevance_concurent[document_id].ref_to_value += term_freq * inverse_document_freq;
            }
        } });

        for_each(std::execution::par, minus_words.begin(), minus_words.end(), [this, &document_to_relevance_concurent](const auto word)
                 {
        if (word_to_document_freqs_.count(word) == 0)
        {
            return;
        }
        for (const auto [document_id, _] : word_to_document_freqs_.find(word)->second)
        {
            document_to_relevance_concurent.erase(document_id);
        } });

        std::map<int, double> document_to_relevance = document_to_relevance_concurent.BuildOrdinaryMap();
        std::vector<Document> matched_documents(document_to_relevance.size());
        for (const auto [document_id, relevance] : document_to_relevance)
        {
            matched_documents.push_back(
                {document_id, relevance, documents_.at(document_id).rating});
        }
        return matched_documents;
    }
}
