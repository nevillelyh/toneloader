#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace toneloader {

enum class Module { pedal, amp, ir };

struct Pack {
  std::filesystem::path path;
  bool archive{};
  std::vector<std::filesystem::path> models;
};

std::filesystem::path default_library();
std::filesystem::path settings_path();
std::filesystem::path configured_library();
bool save_library(const std::filesystem::path& path);
std::vector<Pack> scan_packs(const std::filesystem::path& base, Module module);
std::filesystem::path extract_model(const std::filesystem::path& archive,
                                    const std::filesystem::path& member);

}  // namespace toneloader
