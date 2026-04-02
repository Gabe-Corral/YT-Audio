#include <iostream>
#include <cstdio>
#include <array>
#include <sstream>
#include <vector>



struct SearchMetaData {
    std::string title;
    std::string desc;
    std::string url;
};

namespace search {

std::string exec(const std::string& cmd) {
    std::array<char, 4096> buffer;
    std::string result;

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";

    while (fgets(buffer.data(), buffer.size(), pipe)) {
        result += buffer.data();
    }

    pclose(pipe);
    return result;
}

std::vector<std::string> split(const std::string& s, const std::string& delim) {
    std::vector<std::string> parts;
    size_t start = 0, end;

    while ((end = s.find(delim, start)) != std::string::npos) {
        parts.push_back(s.substr(start, end - start));
        start = end + delim.length();
    }
    parts.push_back(s.substr(start));
    return parts;
}

std::vector<SearchMetaData> get_search_results(const std::string& search_query) {
    std::vector<SearchMetaData> results;

    std::string cmd =
        "yt-dlp \"ytsearch3:" + search_query +
        "\" --print \"%(title)j|||%(description)j|||%(webpage_url)j\"";

    std::string output = exec(cmd);

    std::stringstream ss(output);
    std::string line;

    while (std::getline(ss, line)) {
        if (line.empty()) continue;

        auto parts = split(line, "|||");
        if (parts.size() < 3) continue;

        std::string title = parts[0];
        std::string desc  = parts[1];
        std::string url   = parts[2];

        SearchMetaData data = {
            .title = title,
            .desc = desc,
            .url = url
        };

        results.push_back(data);
    }

    return results;
}

std::string get_stream_url(const std::string& youtube_url) {
    const std::string command =
        "yt-dlp -f bestaudio -g \"" + youtube_url + "\" 2>/dev/null";

    std::array<char, 256> buffer{};
    std::string output;

    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        throw std::runtime_error("yt-dlp not installed or missing");
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

    // const int status = pclose(pipe);
    pclose(pipe);

    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }

    if (output.empty()) {
        throw std::runtime_error("No stream URL returned");
    }

    return output;
}

}

// int main() {
//     std::string query = "ugly brunette";

//     std::vector<SearchMetaData> results = search::get_search_results(query);

//     std::cout << search::get_stream_url(results[0].url) << "\n";

//     return 0;

//     for (const auto i : results) {
//         std::cout << "TITLE: " << "\n";
//         std::cout << i.title << "\n";
//         std::cout << "DESC: " << "\n";
//         std::cout << i.desc << "\n";
//         std::cout << "URL: " << "\n";
//         std::cout << i.url << "\n";
//     }

//     return 0;
// }
