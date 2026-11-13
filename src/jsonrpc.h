#pragma once

#include <cstdio>
#include <optional>
#include <string>

// Minimal JSON-RPC-over-stdio framing for LSP (Content-Length headers,
// \r\n\r\n separator). Not a general JSON-RPC library, just the two
// functions every LSP server over stdio needs.
namespace phh {

// Blocks reading stdin until one full framed message is available and
// returns its body (the JSON payload, without the Content-Length header).
// Returns std::nullopt at EOF (the client closed the pipe).
std::optional<std::string> read_message(FILE *in);

// Writes one framed message (adds the Content-Length header itself) and
// flushes, so the client sees it immediately.
void write_message(FILE *out, const std::string &body);

} // namespace phh
