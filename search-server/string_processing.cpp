#include "string_processing.h"
#include <algorithm>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using namespace std;

vector<string_view> SplitIntoWords(string_view text) {
    vector<string_view> words;

    text.remove_prefix(min(text.find_first_not_of(' '), text.size()));
    while(!text.empty()){
        auto word_end = text.find(' ');
        auto word = text.substr(0, word_end);
        words.push_back(word);
        text.remove_prefix(word.size());
        text.remove_prefix(min(text.find_first_not_of(' '), text.size()));
    }
    return words;
}