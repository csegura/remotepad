#pragma once
#ifdef EMBED_CLIENT

#include "embedded_client_data.h"

#include <string>
#include <string_view>

struct EmbeddedFile {
    const unsigned char* data;
    size_t size;
    const char* mime;
};

static const EmbeddedFile* findEmbeddedFile(const std::string& url) {
    static const struct { const char* path; EmbeddedFile file; } files[] = {
        {"/index.html", {index_html_data, index_html_size, "text/html"}},
        {"/client.js",  {client_js_data,  client_js_size,  "application/javascript"}},
        {"/draw.css",   {draw_css_data,   draw_css_size,   "text/css"}},
    };

    for (const auto& entry : files) {
        if (url == entry.path) return &entry.file;
    }
    return nullptr;
}

#endif
