#include "jsonrpc.h"

#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace phh {

void ensure_binary_stdio() {
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif
}

std::optional<std::string> read_message(FILE *in) {
  long content_length = -1;

  // Header lines, each ending in \r\n, until the blank line.
  for (;;) {
    std::string line;
    int c;
    while ((c = std::fgetc(in)) != EOF) {
      if (c == '\n') break;
      line.push_back(static_cast<char>(c));
    }
    if (c == EOF && line.empty()) return std::nullopt; // clean EOF between messages

    // Strip a trailing \r left over from the \r\n line ending.
    if (!line.empty() && line.back() == '\r') line.pop_back();

    if (line.empty()) break; // blank line: end of headers

    const char *prefix = "Content-Length:";
    if (std::strncmp(line.c_str(), prefix, std::strlen(prefix)) == 0) {
      content_length = std::strtol(line.c_str() + std::strlen(prefix), nullptr, 10);
    }
    // Any other header (e.g. Content-Type) is read and ignored.
  }

  if (content_length < 0) return std::nullopt; // malformed stream; nothing sane to do

  std::string body(static_cast<size_t>(content_length), '\0');
  size_t read = std::fread(body.data(), 1, body.size(), in);
  if (read != body.size()) return std::nullopt; // truncated stream (client went away mid-write)
  return body;
}

void write_message(FILE *out, const std::string &body) {
  std::fprintf(out, "Content-Length: %zu\r\n\r\n", body.size());
  std::fwrite(body.data(), 1, body.size(), out);
  std::fflush(out);
}

} // namespace phh
