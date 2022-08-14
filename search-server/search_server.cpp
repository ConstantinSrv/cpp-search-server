#include "log_duration.h"
#include "search_server.h"
#include "string_processing.h"
#include <iterator>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace std;

static int ComputeAverageRating(const vector<int> &ratings)
{
    int rating_sum = accumulate(ratings.begin(), ratings.end(), 0);
    return rating_sum / static_cast<int>(ratings.size());
}

SearchServer::SearchServer(const std::string &stop_words_text)
    : SearchServer(SplitIntoWords(stop_words_text))
{
}

SearchServer::SearchServer(const std::string_view stop_words_text)
    : SearchServer(SplitIntoWords(stop_words_text))
{
}

void SearchServer::AddDocument(int document_id, const string &document, DocumentStatus status,
                               const vector<int> &ratings)
{
    if ((document_id < 0) || (documents_.count(document_id) > 0))
    {
        throw invalid_argument("Invalid document_id"s);
    }
    const auto words = SplitIntoWordsNoStop(document);
    const double inv_word_count = 1.0 / words.size();

    vector<string> data;
    for (const string_view word : words)
    {
        data.emplace_back(string(word));
    }

    documents_.emplace(document_id, DocumentData{ComputeAverageRating(ratings), status, move(data)});

    for (const string_view word : documents_[document_id].data)
    {
        word_to_document_freqs_[word][document_id] += inv_word_count;
        document_to_word_freqs_[document_id][word] += inv_word_count;
    }
    document_ids_.insert(document_id);
}

vector<Document> SearchServer::FindTopDocuments(const string_view raw_query, DocumentStatus status) const
{
    return FindTopDocuments(execution::seq,
                            raw_query, [status](int document_id, DocumentStatus document_status, int rating)
                            { return document_status == status; });
}

vector<Document> SearchServer::FindTopDocuments(const string_view raw_query) const
{
    return FindTopDocuments(raw_query, DocumentStatus::ACTUAL);
}

int SearchServer::GetDocumentCount() const
{
    return documents_.size();
}

set<int>::iterator SearchServer::begin()
{
    return document_ids_.begin();
}

set<int>::iterator SearchServer::end()
{
    return document_ids_.end();
}

const map<string_view, double, std::less<>> &SearchServer::GetWordFrequencies(int document_id) const
{
    static const map<string_view, double, std::less<>> empty_map{};
    if (document_id < 0)
    {
        throw invalid_argument("Invalid document_id"s);
    }
    if (document_to_word_freqs_.count(document_id) == 0)
    {
        return empty_map;
    }
    return document_to_word_freqs_.at(document_id);
}

void SearchServer::RemoveDocument(int document_id)
{
    for (const auto &[word, value] : document_to_word_freqs_.at(document_id))
    {
        word_to_document_freqs_.find(word)->second.erase(document_id);
    }

    document_to_word_freqs_.erase(document_id);
    documents_.erase(document_id);
    document_ids_.erase(document_id);
}

tuple<vector<string_view>, DocumentStatus> SearchServer::MatchDocument(const string_view raw_query,
                                                                       int document_id) const
{
    // LOG_DURATION_STREAM("Operation time", cout);

    if (document_id < 0)
    {
        throw(invalid_argument("ID cannot be less than 0"));
    }

    const auto query = ParseQuery(raw_query);

    vector<string_view> matched_words;

    for (const string_view word : query.minus_words)
    {
        if (word_to_document_freqs_.count(word) == 0)
        {
            continue;
        }
        if (word_to_document_freqs_.find(word)->second.count(document_id))
        {
            return {matched_words, documents_.at(document_id).status};
        }
    }
    for (const string_view word : query.plus_words)
    {
        if (word_to_document_freqs_.count(word) == 0)
        {
            continue;
        }
        if (word_to_document_freqs_.find(word)->second.count(document_id))
        {
            matched_words.push_back(word_to_document_freqs_.find(word)->first);
        }
    }
    return {matched_words, documents_.at(document_id).status};
}

bool SearchServer::IsStopWord(const std::string_view word) const
{
    return stop_words_.count(word) > 0;
}

bool SearchServer::IsValidWord(const std::string_view word)
{
    // A valid word must not contain special characters
    return none_of(word.begin(), word.end(), [](char c)
                   { return c >= '\0' && c < ' '; });
}

std::vector<std::string_view> SearchServer::SplitIntoWordsNoStop(std::string_view text) const
{
    std::vector<string_view> words;
    for (const string_view word : SplitIntoWords(text))
    {
        if (!IsValidWord(word))
        {
            throw invalid_argument("Word "s + std::string(word) + " is invalid"s);
        }
        if (!IsStopWord(word))
        {
            words.push_back(word);
        }
    }
    return words;
}

SearchServer::QueryWord SearchServer::ParseQueryWord(const std::string_view text) const
{
    if (text.empty())
    {
        throw std::invalid_argument("Query word is empty"s);
    }
    std::string_view word = text;
    bool is_minus = false;
    if (text[0] == '-')
    {
        is_minus = true;
        word = word.substr(1);
    }
    if (word.empty() || word[0] == '-' || !IsValidWord(word))
    {
        throw std::invalid_argument("Query word "s + std::string(text) + " is invalid");
    }
    return {word, is_minus, IsStopWord(word)};
}

SearchServer::Query SearchServer::ParseQuery(const std::string_view text) const
{
    Query result;
    for (const std::string_view word : SplitIntoWords(text))
    {
        const auto query_word = ParseQueryWord(word);
        if (!query_word.is_stop)
        {
            if (query_word.is_minus)
            {
                result.minus_words.insert(query_word.data);
            }
            else
            {
                result.plus_words.insert(query_word.data);
            }
        }
    }
    return result;
}

double SearchServer::ComputeWordInverseDocumentFreq(const std::string_view word) const
{
    return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.find(word)->second.size());
}

void SearchServer::RemoveDocument(std::execution::parallel_policy policy, int document_id)
{
    std::vector<std::string_view> words(document_to_word_freqs_.at(document_id).size());
    std::transform(policy, document_to_word_freqs_.at(document_id).begin(), document_to_word_freqs_.at(document_id).end(), words.begin(), [](const auto &element)
                   { return element.first; });

    for_each(policy, words.begin(), words.end(), [this, document_id](std::string_view elem)
             { word_to_document_freqs_.find(elem)->second.erase(document_id); });

    document_to_word_freqs_.erase(document_id);
    documents_.erase(document_id);
    document_ids_.erase(document_id);
}

void SearchServer::RemoveDocument(std::execution::sequenced_policy policy, int document_id)
{
    RemoveDocument(document_id);
}

std::tuple<std::vector<std::string_view>, DocumentStatus> SearchServer::MatchDocument(std::execution::sequenced_policy policy, const std::string_view raw_query,
                                                                                      int document_id) const
{
    return MatchDocument(raw_query, document_id);
}

std::tuple<std::vector<std::string_view>, DocumentStatus> SearchServer::MatchDocument(std::execution::parallel_policy policy, const std::string_view raw_query,
                                                                                      int document_id) const
{
    if (document_id < 0)
    {
        throw(invalid_argument("ID cannot be less than 0"));
    }

    const auto query = ParseQueryParallel(raw_query);
    vector<string_view> matched_words(query.plus_words.size());

    if (any_of(policy, query.minus_words.begin(), query.minus_words.end(), [this, document_id](auto &word)
               { return word_to_document_freqs_.count(word) != 0 && word_to_document_freqs_.find(word)->second.count(document_id); }))
    {
        matched_words.clear();
        return {matched_words, documents_.at(document_id).status};
    }

    auto end = copy_if(policy, query.plus_words.begin(), query.plus_words.end(), matched_words.begin(), [this, document_id, &matched_words](auto &word)
                       { return word_to_document_freqs_.count(word) != 0 && word_to_document_freqs_.find(word)->second.count(document_id); });
    matched_words.resize(end - matched_words.begin());

    sort(policy, matched_words.begin(), matched_words.end());
    end = unique(policy, matched_words.begin(), matched_words.end());
    matched_words.resize(end - matched_words.begin());

    return {matched_words, documents_.at(document_id).status};
}

SearchServer::QueryParallel SearchServer::ParseQueryParallel(const std::string_view text) const
{
    QueryParallel result;
    for (const std::string_view word : SplitIntoWords(text))
    {
        const auto query_word = ParseQueryWord(word);
        if (!query_word.is_stop)
        {
            if (query_word.is_minus)
            {
                result.minus_words.push_back(query_word.data);
            }
            else
            {
                result.plus_words.push_back(query_word.data);
            }
        }
    }
    return result;
}