#pragma once

#include <cctype>
#include <filesystem>
#include <map>
#include <sstream>
#include <string>
#include <type_traits>

class Config {
private:
    std::map<std::string, std::string> settings;

public:
    static Config from_json_file(const std::filesystem::path& path);

    void set(const std::string& key, const std::string& value) {
        settings[key] = value;
    }

    bool contains(const std::string& key) const {
        return settings.find(key) != settings.end();
    }

    template <typename T>
    T get(const std::string& key, T default_value) const {
        auto it = settings.find(key);
        if (it == settings.end()) {
            return default_value;
        }

        if constexpr (std::is_same_v<T, std::string>) {
            return it->second;
        } else if constexpr (std::is_same_v<T, bool>) {
            std::string lowered;
            lowered.reserve(it->second.size());
            for (char ch : it->second) {
                lowered.push_back(static_cast<char>(
                    std::tolower(static_cast<unsigned char>(ch))
                ));
            }
            if (lowered == "1" || lowered == "true"
                || lowered == "yes" || lowered == "on") {
                return true;
            }
            if (lowered == "0" || lowered == "false"
                || lowered == "no" || lowered == "off") {
                return false;
            }
            return default_value;
        } else {
            T result{};
            std::istringstream input(it->second);
            if (input >> result) {
                return result;
            }
            return default_value;
        }
    }
};
