#include "config.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

using namespace std;
namespace fs = std::filesystem;

namespace {

string trim(const string& value) {
    size_t first = 0;
    while (first < value.size()
        && isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }

    size_t last = value.size();
    while (last > first
        && isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }

    return value.substr(first, last - first);
}

string read_text_file(const fs::path& path) {
    ifstream input(path);
    if (!input) {
        throw runtime_error("Cannot open config file: " + path.string());
    }

    ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

string parse_json_string_token(const string& text, size_t& pos) {
    string value;
    if (pos >= text.size() || text[pos] != '"') {
        return value;
    }

    ++pos;
    while (pos < text.size()) {
        char ch = text[pos++];
        if (ch == '"') {
            break;
        }
        if (ch == '\\' && pos < text.size()) {
            char escaped = text[pos++];
            switch (escaped) {
                case '"': value.push_back('"'); break;
                case '\\': value.push_back('\\'); break;
                case '/': value.push_back('/'); break;
                case 'b': value.push_back('\b'); break;
                case 'f': value.push_back('\f'); break;
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                default: value.push_back(escaped); break;
            }
        } else {
            value.push_back(ch);
        }
    }

    return value;
}

} // namespace

Config config;

void load_config_json(const fs::path& path) {
    string text = read_text_file(path);
    config.clear();

    size_t pos = 0;
    auto skip_ws = [&]() {
        while (pos < text.size()
            && isspace(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
    };

    skip_ws();
    if (pos >= text.size() || text[pos] != '{') {
        throw runtime_error("Invalid JSON object config: " + path.string());
    }
    ++pos;

    while (pos < text.size()) {
        skip_ws();
        if (pos < text.size() && text[pos] == '}') {
            break;
        }
        if (pos >= text.size() || text[pos] != '"') {
            throw runtime_error("Invalid JSON config key: " + path.string());
        }

        string key = parse_json_string_token(text, pos);
        skip_ws();
        if (pos >= text.size() || text[pos] != ':') {
            throw runtime_error("Invalid JSON config separator: " + path.string());
        }
        ++pos;
        skip_ws();

        string value;
        if (pos < text.size() && text[pos] == '"') {
            value = parse_json_string_token(text, pos);
        } else {
            size_t start = pos;
            while (pos < text.size() && text[pos] != ',' && text[pos] != '}') {
                ++pos;
            }
            value = trim(text.substr(start, pos - start));
        }

        config.set(key, value);

        skip_ws();
        if (pos < text.size() && text[pos] == ',') {
            ++pos;
        }
    }
}
