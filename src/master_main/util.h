#pragma once

#include "../graph/graph.h"

#include <filesystem>
#include <string>
#include <vector>

struct ConfigLocation {
    std::filesystem::path path;
    std::filesystem::path root;
};

std::string trim(const std::string& value);

ConfigLocation master_config_location();
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
