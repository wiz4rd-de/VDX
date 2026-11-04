#include "ui_resources.h"

#include <cctype>
#include <cstring>

namespace vdx::plugin {

const EmbeddedFile* findEmbeddedFile(const std::string& requestPath) {
  std::string path = requestPath;
  const size_t cut = path.find_first_of("?#");
  if (cut != std::string::npos) path.resize(cut);
  if (path.empty() || path == "/") path = "/index.html";
  if (path[0] != '/') path.insert(path.begin(), '/');
  std::size_t count = 0;
  const EmbeddedFile* files = embeddedFiles(count);
  for (std::size_t i = 0; i < count; ++i)
    if (path == files[i].path) return &files[i];
  return nullptr;
}

const char* mimeTypeFor(const std::string& path) {
  const size_t dot = path.find_last_of('.');
  std::string ext = dot == std::string::npos ? std::string() : path.substr(dot + 1);
  for (auto& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
  struct Entry {
    const char* ext;
    const char* mime;
  };
  static const Entry kTypes[] = {
      {"html", "text/html"},       {"htm", "text/html"},        {"js", "text/javascript"},
      {"mjs", "text/javascript"},  {"css", "text/css"},         {"json", "application/json"},
      {"map", "application/json"}, {"svg", "image/svg+xml"},    {"woff2", "font/woff2"},
      {"woff", "font/woff"},       {"ttf", "font/ttf"},         {"otf", "font/otf"},
      {"wasm", "application/wasm"}, {"txt", "text/plain"},      {"ico", "image/x-icon"},
      {"webmanifest", "application/manifest+json"},
  };
  for (const Entry& e : kTypes)
    if (ext == e.ext) return e.mime;
  return "application/octet-stream";
}

}  // namespace vdx::plugin
