#pragma once

#include "../graph/graph.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

std::string trim(const std::string& value);
std::string read_text_file(const std::filesystem::path& path);
std::map<std::string, std::string> read_config_object_file(
    const std::filesystem::path& path
);
std::vector<std::string> split_csv_list(const std::string& value);
bool parse_bool(const std::string& value);

std::filesystem::path config_root_from_file(const std::filesystem::path& path);
std::string resolve_config_path(
    const std::filesystem::path& root,
    const std::string& value
);
std::string resolve_instance_path(
    const std::filesystem::path& root,
    const std::string& value
);
std::vector<std::filesystem::path> parent_chain(std::filesystem::path start);
std::filesystem::path find_default_file(
    const std::string& argv0,
    const std::filesystem::path& relative_path
);

long long count_edges(const Graph& G);
int ceil_bound(double value);
std::string gurobi_status_name(int status);

std::string format_double(double value);
std::string csv_escape(const std::string& value);
std::string json_escape(const std::string& value);
std::string json_string(const std::string& value);
std::string json_number(double value);
std::string json_bool(bool value);

std::string generate_uuid();
std::string current_git_commit_short();
std::string compiler_name();
