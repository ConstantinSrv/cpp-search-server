#include "search_server.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace std;

/* Подставьте вашу реализацию класса SearchServer сюда */



// -------- Начало модульных тестов поисковой системы ----------

// Тест проверяет, что поисковая система исключает стоп-слова при добавлении документов
void TestExcludeStopWordsFromAddedDocumentContent() {
    const int doc_id = 42;
    const string content = "cat in the city"s;
    const vector<int> ratings = {1, 2, 3};
    {
        SearchServer server;
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        const auto found_docs = server.FindTopDocuments("in"s);
        ASSERT_EQUAL(found_docs.size(), 1u);
        const Document& doc0 = found_docs[0];
        ASSERT_EQUAL(doc0.id, doc_id);
    }

    {
        SearchServer server;
        server.SetStopWords("in the"s);
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        ASSERT_HINT(server.FindTopDocuments("in"s).empty(), "Stop words must be excluded from documents"s);
    }
}

/*
Разместите код остальных тестов здесь
*/
void TestAddDocument() {
    const int doc_id = 42;
    const string content = "cat in the city"s;
    const vector<int> ratings = {1, 2, 3};
    {
        SearchServer server;
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        ASSERT_EQUAL(server.FindTopDocuments("cat in the city"s).size(), 1u);
    }
}
void TestExcludeDocumentsWithMinusWords() {
    const int doc_id = 42;
    const string content = "cat in the city"s;
    const vector<int> ratings = {1, 2, 3};
    {
        SearchServer server;
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        ASSERT(server.FindTopDocuments("-cat in the city"s).empty());
    }
    {
        SearchServer server;
        server.SetStopWords("in the"s);
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        ASSERT_EQUAL(server.FindTopDocuments("cat -in -the city"s).size(), 1);
    }
}

void TestMatchDocument() {
    const int doc_id = 42;
    const string content = "cat in the city"s;
    const vector<int> ratings = {1, 2, 3};
    {
        SearchServer server;
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        const vector<string> actual_words = get<0>(server.MatchDocument("cat city"s, doc_id));
        const vector<string> expected_words{"cat"s, "city"s};
        ASSERT_EQUAL(actual_words, expected_words);
    }
    {
        SearchServer server;
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        const vector<string> actual_words = get<0>(server.MatchDocument("-cat city"s, doc_id));
        ASSERT(actual_words.empty());
    }
}

void TestOrderDocumentsByRelevance() {
    const int doc_id1 = 42;
    const string content1 = "cat in the city"s;
    const int doc_id2 = 43;
    const string content2 = "cat in the"s;
    const int doc_id3 = 44;
    const string content3 = "cat in"s;
    const int doc_id4 = 45;
    const string content4 = "cat"s;
    const vector<int> ratings = {1, 2, 3};
    {
        SearchServer server;
        server.AddDocument(doc_id2, content2, DocumentStatus::ACTUAL, ratings);
        server.AddDocument(doc_id3, content3, DocumentStatus::ACTUAL, ratings);
        server.AddDocument(doc_id1, content1, DocumentStatus::ACTUAL, ratings);
        server.AddDocument(doc_id4, content4, DocumentStatus::ACTUAL, ratings);
        const vector<Document> documents = server.FindTopDocuments("cat in the city");
        ASSERT_EQUAL(documents[0].id, 42);
        ASSERT_EQUAL(documents[1].id, 43);
        ASSERT_EQUAL(documents[2].id, 44);
        ASSERT_EQUAL(documents[3].id, 45);
    }
}

void TestCalculateRatings() {
    const int doc_id = 42;
    const string content = "cat in the city"s;
    {
        const vector<int> ratings;
        SearchServer server;
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        const vector<Document> documents = server.FindTopDocuments("cat in the city");
        ASSERT_EQUAL(documents[0].rating, 0);
    }
    {
        const vector<int> ratings = {1, 2, 3};
        SearchServer server;
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        const vector<Document> documents = server.FindTopDocuments("cat in the city");
        ASSERT_EQUAL(documents[0].rating, (1 + 2 + 3) / 3);
    }
}

void TestFilterTopDocumentsByStatus() {
    const int doc_id = 42;
    const string content = "cat in the city"s;
    const vector<int> ratings = {1, 2, 3};
    {
        SearchServer server;
        server.AddDocument(doc_id, content, DocumentStatus::BANNED, ratings);
        const vector<Document> documents = server.FindTopDocuments("cat in the city", DocumentStatus::BANNED);
        ASSERT_EQUAL(documents[0].id, 42);
    }
}

void TestFilterTopDocumentsByPredicate() {
    const int doc_id = 42;
    const string content = "cat in the city"s;
    const vector<int> ratings = {1, 2, 3};
    {
        SearchServer server;
        server.AddDocument(doc_id, content, DocumentStatus::BANNED, ratings);
        const vector<Document> documents = server.FindTopDocuments("cat in the city", [](int id, DocumentStatus status, int rating){return id == 42 && status == DocumentStatus::BANNED && rating >= 2;});
        ASSERT_EQUAL(documents[0].id, 42);
    }
    {
        SearchServer server;
        server.AddDocument(doc_id, content, DocumentStatus::BANNED, ratings);
        const vector<Document> documents = server.FindTopDocuments("cat in the city", [](int id, DocumentStatus status, int rating){return id != 42 || status != DocumentStatus::BANNED || rating < 2;});
        ASSERT_EQUAL(documents.size(), 0);
    }
}

void TestCalculateRelevance() {
    const double DEVIATION = 1e-6;
    const int doc_id1 = 42;
    const string content1 = "cat in the city"s;
    const int doc_id2 = 43;
    const string content2 = "cat in the"s;
    const int doc_id3 = 44;
    const string content3 = "cat in"s;
    const int doc_id4 = 45;
    const string content4 = "cat"s;
    const vector<int> ratings = {1, 2, 3};
    SearchServer server;
    server.AddDocument(doc_id1, content1, DocumentStatus::ACTUAL, ratings);
    server.AddDocument(doc_id2, content2, DocumentStatus::ACTUAL, ratings);
    server.AddDocument(doc_id3, content3, DocumentStatus::ACTUAL, ratings);
    server.AddDocument(doc_id4, content4, DocumentStatus::ACTUAL, ratings);
    {
        const vector<Document> documents = server.FindTopDocuments("cat");
        ASSERT(abs(documents[0].relevance - (1.0 / 4 * log(4.0 /4))) < DEVIATION);
        ASSERT(abs(documents[1].relevance - (1.0 / 3 * log(4.0 /4))) < DEVIATION);
    }
    {
        const vector<Document> documents = server.FindTopDocuments("city");
        ASSERT(abs(documents[0].relevance - (1.0 / 4 * log(4.0 /1))) < DEVIATION);
    }
    {
        const vector<Document> documents = server.FindTopDocuments("dog");
        ASSERT(documents.empty());
    }
}

// Функция TestSearchServer является точкой входа для запуска тестов
void TestSearchServer() {
    RUN_TEST(TestExcludeStopWordsFromAddedDocumentContent);
    // Не забудьте вызывать остальные тесты здесь
    RUN_TEST(TestAddDocument);
    RUN_TEST(TestExcludeDocumentsWithMinusWords);
    RUN_TEST(TestMatchDocument);
    RUN_TEST(TestOrderDocumentsByRelevance);
    RUN_TEST(TestCalculateRatings);
    RUN_TEST(TestFilterTopDocumentsByStatus);
    RUN_TEST(TestFilterTopDocumentsByPredicate);
    RUN_TEST(TestCalculateRelevance);
}

// --------- Окончание модульных тестов поисковой системы -----------

int main() {
    TestSearchServer();
    // Если вы видите эту строку, значит все тесты прошли успешно
    cout << "Search server testing finished"s << endl;
}