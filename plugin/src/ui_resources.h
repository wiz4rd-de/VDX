// The UI bundle (ui/dist) embedded at build time by tools/embed_ui.py.
#pragma once

#include <cstddef>
#include <string>

namespace vdx::plugin {

struct EmbeddedFile {
  const char* path;  // "/index.html", "/assets/…"
  const unsigned char* data;
  std::size_t size;
};

// Defined in the generated ui_resources.cpp.
const EmbeddedFile* embeddedFiles(std::size_t& count);

// Looks up a request path ("/" = "/index.html"; query and fragment ignored).
const EmbeddedFile* findEmbeddedFile(const std::string& requestPath);

// MIME type from the file extension.
const char* mimeTypeFor(const std::string& path);

}  // namespace vdx::plugin
