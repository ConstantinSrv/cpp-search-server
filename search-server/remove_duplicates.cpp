#include "remove_duplicates.h"
#include "search_server.h"
#include <iostream>
#include <map>
#include <set>

using namespace std;

void RemoveDuplicates(SearchServer& search_server) {
    vector<int> ids_to_delete;
    map<set<string>, int> words_to_ids;
    for (const int document_id : search_server) {
        set<string> words;
        for (const auto& [word, frequency] : search_server.GetWordFrequencies(document_id)) {
            words.insert(word);
        }
        if (words_to_ids.count(words) != 0) {
            if (words_to_ids.at(words) > document_id) {
                ids_to_delete.push_back(words_to_ids.at(words));
                words_to_ids[words] = document_id;
            }
            else {
                ids_to_delete.push_back(document_id);
            }
        }
        else {
            words_to_ids[words] = document_id;
        } 
        
    }
    for (const int id : ids_to_delete) {
        cout << "Found duplicate document id " << id << endl;
        search_server.RemoveDocument(id);
    }
}