#include "util.h"

#include "gurobi_c++.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>

using namespace std;
namespace fs = std::filesystem;

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
        return "";
    }

    ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

static string parse_json_string_token(const string& text, size_t& pos) {
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

static map<string, string> read_flat_json_file(const fs::path& path) {
    map<string, string> values;
    string text = read_text_file(path);
    if (text.empty()) {
        return values;
    }

    size_t pos = 0;
    auto skip_ws = [&]() {
        while (pos < text.size()
            && isspace(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
    };

    skip_ws();
    if (pos >= text.size() || text[pos] != '{') {
        return values;
    }
    ++pos;

    while (pos < text.size()) {
        skip_ws();
        if (pos < text.size() && text[pos] == '}') {
            break;
        }
        if (pos >= text.size() || text[pos] != '"') {
            break;
        }

        string key = parse_json_string_token(text, pos);
        skip_ws();
        if (pos >= text.size() || text[pos] != ':') {
            break;
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

        if (!key.empty()) {
            values[key] = value;
        }

        skip_ws();
        if (pos < text.size() && text[pos] == ',') {
            ++pos;
        }
    }

    return values;
}

map<string, string> read_config_object_file(const fs::path& path) {
    return read_flat_json_file(path);
}

vector<string> split_csv_list(const string& value) {
    vector<string> items;
    string item;
    stringstream ss(value);
    while (getline(ss, item, ',')) {
        item = trim(item);
        if (!item.empty()) {
            items.push_back(item);
        }
    }
    return items;
}

bool parse_bool(const string& value) {
    string lowered;
    lowered.reserve(value.size());
    for (char ch : value) {
        lowered.push_back(static_cast<char>(
            tolower(static_cast<unsigned char>(ch))
        ));
    }
    return lowered == "1" || lowered == "true"
        || lowered == "yes" || lowered == "on";
}

fs::path config_root_from_file(const fs::path& path) {
    fs::path parent = path.parent_path();
    if (parent.filename() == "master_cp") {
        return parent.parent_path();
    }
    return parent;
}

string resolve_config_path(const fs::path& root, const string& value) {
    fs::path path(value);
    if (path.is_relative()) {
        path = root / path;
    }
    return path.lexically_normal().string();
}

string resolve_instance_path(const fs::path& root, const string& value) {
    fs::path path(value);
    if (path.is_absolute()) {
        return path.lexically_normal().string();
    }

    fs::path direct = (root / path).lexically_normal();
    if (fs::exists(direct)) {
        return direct.string();
    }

    fs::path tests_path = (root / "tests" / path).lexically_normal();
    if (fs::exists(tests_path)) {
        return tests_path.string();
    }

    return direct.string();
}

vector<fs::path> parent_chain(fs::path start) {
    vector<fs::path> paths;
    if (start.empty()) {
        return paths;
    }

    start = fs::absolute(start).lexically_normal();
    for (int depth = 0; depth < 6 && !start.empty(); ++depth) {
        paths.push_back(start);
        fs::path parent = start.parent_path();
        if (parent == start) {
            break;
        }
        start = parent;
    }
    return paths;
}

long long count_edges(const Graph& G) {
    long long degree_sum = 0;
    for (int v = 0; v < G.num_vertices(); ++v) {
        degree_sum += static_cast<long long>(G.neighbors(v).size());
    }
    return degree_sum / 2;
}

int ceil_bound(double value) {
    return static_cast<int>(ceil(value - 1e-9));
}

string gurobi_status_name(int status) {
    switch (status) {
        case GRB_OPTIMAL: return "OPTIMAL";
        case GRB_INFEASIBLE: return "INFEASIBLE";
        case GRB_INF_OR_UNBD: return "INF_OR_UNBD";
        case GRB_UNBOUNDED: return "UNBOUNDED";
        case GRB_TIME_LIMIT: return "TIME_LIMIT";
        default: return "STATUS_" + to_string(status);
    }
}

string format_double(double value) {
    ostringstream out;
    out << fixed << setprecision(6) << value;
    return out.str();
}

string csv_escape(const string& value) {
    bool needs_quotes = value.find_first_of(",\"\n\r") != string::npos;
    if (!needs_quotes) {
        return value;
    }

    string escaped = "\"";
    for (char ch : value) {
        if (ch == '"') {
            escaped += "\"\"";
        } else {
            escaped += ch;
        }
    }
    escaped += "\"";
    return escaped;
}

string json_escape(const string& value) {
    string escaped;
    escaped.reserve(value.size() + 8);
    for (char ch : value) {
        switch (ch) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    escaped += "\\u00";
                    const char* hex = "0123456789abcdef";
                    escaped.push_back(hex[(ch >> 4) & 0x0f]);
                    escaped.push_back(hex[ch & 0x0f]);
                } else {
                    escaped.push_back(ch);
                }
        }
    }
    return escaped;
}

string json_string(const string& value) {
    return "\"" + json_escape(value) + "\"";
}

string json_number(double value) {
    ostringstream out;
    out << fixed << setprecision(6) << value;
    return out.str();
}

string json_bool(bool value) {
    return value ? "true" : "false";
}

string generate_uuid() {
    random_device rd;
    mt19937_64 rng(
        (static_cast<uint64_t>(rd()) << 32)
        ^ static_cast<uint64_t>(
            chrono::high_resolution_clock::now().time_since_epoch().count()
        )
    );
    uniform_int_distribution<uint64_t> dist(0, numeric_limits<uint64_t>::max());

    uint64_t high = dist(rng);
    uint64_t low = dist(rng);
    high = (high & 0xffffffffffff0fffULL) | 0x0000000000004000ULL;
    low = (low & 0x3fffffffffffffffULL) | 0x8000000000000000ULL;

    ostringstream out;
    out << hex << setfill('0')
        << setw(8) << static_cast<unsigned int>((high >> 32) & 0xffffffffULL)
        << "-"
        << setw(4) << static_cast<unsigned int>((high >> 16) & 0xffffULL)
        << "-"
        << setw(4) << static_cast<unsigned int>(high & 0xffffULL)
        << "-"
        << setw(4) << static_cast<unsigned int>((low >> 48) & 0xffffULL)
        << "-"
        << setw(12) << (low & 0xffffffffffffULL);
    return out.str();
}

static fs::path find_repo_root() {
    for (const fs::path& base : parent_chain(fs::current_path())) {
        if (fs::exists(base / ".git")) {
            return base;
        }
    }
    return fs::current_path();
}

static string read_first_line(const fs::path& path) {
    ifstream input(path);
    string line;
    if (input && getline(input, line)) {
        return trim(line);
    }
    return "";
}

string current_git_commit_short() {
    fs::path git_dir = find_repo_root() / ".git";
    string head = read_first_line(git_dir / "HEAD");
    if (head.empty()) {
        return "unknown";
    }

    string commit = head;
    const string ref_prefix = "ref:";
    if (head.rfind(ref_prefix, 0) == 0) {
        string ref = trim(head.substr(ref_prefix.size()));
        commit = read_first_line(git_dir / ref);
    }

    if (commit.size() > 7) {
        return commit.substr(0, 7);
    }
    return commit.empty() ? "unknown" : commit;
}

string compiler_name() {
#if defined(__clang__)
    return string("clang-") + to_string(__clang_major__) + "."
        + to_string(__clang_minor__);
#elif defined(__GNUC__)
    return string("gcc-") + to_string(__GNUC__) + "."
        + to_string(__GNUC_MINOR__);
#elif defined(_MSC_VER)
    return string("msvc-") + to_string(_MSC_VER);
#else
    return "unknown";
#endif
}
