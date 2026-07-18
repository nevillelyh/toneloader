#include "library.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <zip.h>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() / "toneloader-library-test";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "IR" / "MESA" / "deep");
  const char* home = std::getenv("HOME");
  const std::string original_home = home ? home : "";
  setenv("HOME", root.c_str(), 1);
  require(toneloader::configured_library() == root / "Music" / "ToneLoader",
          "missing settings did not use the default library");
  const auto configured = root / "Models";
  require(toneloader::save_library(configured), "could not save library setting");
  require(toneloader::configured_library() == configured, "saved library setting was not read");
  std::ofstream(root / "IR" / "loose.wav");
  int zip_error = 0;
  zip_t* archive = zip_open((root / "IR" / "MESA.zip").c_str(), ZIP_CREATE, &zip_error);
  require(archive, "could not create test ZIP");
  zip_source_t* valid = zip_source_buffer(archive, "wave", 4, 0);
  zip_source_t* nested = zip_source_buffer(archive, "wave", 4, 0);
  require(zip_file_add(archive, "mesa002.wav", valid, ZIP_FL_ENC_UTF_8) >= 0,
          "could not add top-level ZIP model");
  require(zip_file_add(archive, "deep/ignored.wav", nested, ZIP_FL_ENC_UTF_8) >= 0,
          "could not add nested ZIP model");
  require(zip_close(archive) == 0, "could not close test ZIP");
  std::ofstream(root / "IR" / "MESA" / "mesa001.WAV");
  std::ofstream(root / "IR" / "MESA" / "readme.txt");
  std::ofstream(root / "IR" / "MESA" / "deep" / "ignored.wav");

  const auto packs = toneloader::scan_packs(root, toneloader::Module::ir);
  require(packs.size() == 2, "expected directory and ZIP packs");
  require(!packs[0].archive && packs[0].models.size() == 1,
          "directory pack scan was incorrect");
  require(packs[1].archive && packs[1].models.size() == 1,
          "ZIP pack scan was incorrect");
  require(packs[1].models[0] == "mesa002.wav", "nested ZIP model was not rejected");
  const auto extracted = toneloader::extract_model(packs[1].path, packs[1].models[0]);
  require(!extracted.empty() && std::filesystem::file_size(extracted) == 4,
          "ZIP model extraction failed");
  require(toneloader::extract_model(packs[1].path, "../escape.wav").empty(),
          "unsafe ZIP member was accepted");

  std::filesystem::remove_all(root);
  if (home) setenv("HOME", original_home.c_str(), 1);
  else unsetenv("HOME");
}
