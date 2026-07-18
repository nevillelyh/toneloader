#include "library.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <zip.h>

namespace toneloader {
namespace {

std::filesystem::path home() {
  if (const char* value = std::getenv("HOME")) return value;
  return {};
}

std::string trim(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n\"");
  if (first == std::string::npos) return {};
  const auto last = value.find_last_not_of(" \t\r\n\"");
  return value.substr(first, last - first + 1);
}

bool extension_is(const std::filesystem::path& path, std::string expected) {
  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return extension == expected;
}

bool safe_member(const std::filesystem::path& path) {
  if (path.empty()) return false;
  if (path.is_absolute()) return false;
  if (path.has_parent_path()) return false;
  const auto name = path.string();
  return name != "." && name != "..";
}

}  // namespace

std::filesystem::path default_library() { return home() / "Music" / "ToneLoader"; }

std::filesystem::path settings_path() {
  return home() / ".config" / "ToneLoader" / "settings.toml";
}

std::filesystem::path configured_library() {
  std::ifstream input(settings_path());
  std::string line;
  while (std::getline(input, line)) {
    const auto separator = line.find('=');
    if (separator == std::string::npos || trim(line.substr(0, separator)) != "library") continue;
    const auto value = trim(line.substr(separator + 1));
    if (!value.empty()) return value;
  }
  return default_library();
}

bool save_library(const std::filesystem::path& path) {
  std::error_code error;
  std::filesystem::create_directories(settings_path().parent_path(), error);
  if (error) return false;
  std::ofstream output(settings_path(), std::ios::trunc);
  output << "library = \"" << path.string() << "\"\n";
  return output.good();
}

std::vector<Pack> scan_packs(const std::filesystem::path& base, Module module) {
  const char* directory = module == Module::pedal ? "pedal" : module == Module::amp ? "NAM" : "IR";
  const std::string model_extension = module == Module::ir ? ".wav" : ".nam";
  const auto root = base / directory;
  std::vector<Pack> result;
  std::error_code error;
  for (std::filesystem::directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
    const auto& entry = *it;
    if (entry.is_regular_file(error) && extension_is(entry.path(), ".zip")) {
      Pack pack{entry.path(), true, {}};
      int zip_error = 0;
      if (zip_t* archive = zip_open(entry.path().c_str(), ZIP_RDONLY, &zip_error)) {
        const auto count = zip_get_num_entries(archive, 0);
        for (zip_int64_t i = 0; i < count; ++i) {
          const char* name = zip_get_name(archive, static_cast<zip_uint64_t>(i), ZIP_FL_ENC_GUESS);
          if (!name) continue;
          const std::filesystem::path member(name);
          if (safe_member(member) && extension_is(member, model_extension)) pack.models.push_back(member);
        }
        zip_close(archive);
      }
      std::sort(pack.models.begin(), pack.models.end());
      result.push_back(std::move(pack));
    } else if (entry.is_directory(error)) {
      Pack pack{entry.path(), false, {}};
      std::error_code child_error;
      for (std::filesystem::directory_iterator child(entry.path(), child_error), child_end;
           !child_error && child != child_end; child.increment(child_error)) {
        if (child->is_regular_file(child_error) && extension_is(child->path(), model_extension))
          pack.models.push_back(child->path());
      }
      std::sort(pack.models.begin(), pack.models.end());
      if (!pack.models.empty()) result.push_back(std::move(pack));
    }
  }
  std::sort(result.begin(), result.end(), [](const Pack& a, const Pack& b) { return a.path < b.path; });
  return result;
}

std::filesystem::path extract_model(const std::filesystem::path& archive_path,
                                    const std::filesystem::path& member) {
  if (!safe_member(member)) return {};
  std::error_code error;
  const auto size = std::filesystem::file_size(archive_path, error);
  if (error) return {};
  const auto modified = std::filesystem::last_write_time(archive_path, error).time_since_epoch().count();
  if (error) return {};
  const auto cache = home() / ".cache" / "toneloader" /
      (std::to_string(size) + "-" + std::to_string(modified)) / member.filename();
  if (std::filesystem::is_regular_file(cache, error)) return cache;

  int zip_error = 0;
  zip_t* archive = zip_open(archive_path.c_str(), ZIP_RDONLY, &zip_error);
  if (!archive) return {};
  zip_stat_t statistics{};
  zip_stat_init(&statistics);
  if (zip_stat(archive, member.c_str(), 0, &statistics) != 0 || !(statistics.valid & ZIP_STAT_SIZE)) {
    zip_close(archive);
    return {};
  }
  zip_file_t* source = zip_fopen(archive, member.c_str(), 0);
  if (!source) { zip_close(archive); return {}; }
  std::filesystem::create_directories(cache.parent_path(), error);
  if (error) { zip_fclose(source); zip_close(archive); return {}; }
  const auto temporary = cache.string() + ".tmp";
  std::vector<char> buffer(static_cast<std::size_t>(statistics.size));
  const auto read = zip_fread(source, buffer.data(), buffer.size());
  zip_fclose(source);
  zip_close(archive);
  if (read != static_cast<zip_int64_t>(buffer.size())) return {};
  std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
  output.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
  output.close();
  if (!output) { std::filesystem::remove(temporary, error); return {}; }
  std::filesystem::rename(temporary, cache, error);
  return error ? std::filesystem::path{} : cache;
}

}  // namespace toneloader
