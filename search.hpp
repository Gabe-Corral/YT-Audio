#pragma once

#include <string>
#include <vector>

struct SearchMetaData {
    std::string title;
    std::string desc;
    std::string url;
};

namespace search {

std::string exec(const std::string& cmd);

std::vector<std::string> split(const std::string& s, const std::string& delim);

std::vector<SearchMetaData> get_search_results(const std::string& search_query);

std::string get_stream_url(const std::string& youtube_url);

}
