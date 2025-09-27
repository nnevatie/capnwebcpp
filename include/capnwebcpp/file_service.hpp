#pragma once

#include <fstream>
#include <iostream>
#include <filesystem>

namespace capnwebcpp
{

template<typename App>
void setupFileEndpoint(App& app, const std::string& path, const std::filesystem::path& root)
{
    app.get(path + "*", [root, path](auto* res, auto* req)
    {
        std::string url(req->getUrl());

        // Extract the file path from the URL
        std::string filePath = url;
        if (url.find(path) == 0)
            filePath = url.substr(path.length());

        // Remove leading slash from file path
        if (!filePath.empty() && filePath[0] == '/')
            filePath = filePath.substr(1);

        // Default to index.html for directory requests
        if (filePath.empty() || filePath.back() == '/')
            filePath += "index.html";

        // Construct full filesystem path
        std::filesystem::path fullPath = root / filePath;

        // Security check: ensure the path is within fsRoot
        std::filesystem::path canonicalBase = std::filesystem::weakly_canonical(root);
        std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(fullPath);

        std::cout << "Serving file: " << canonicalPath << std::endl;

        if (canonicalPath.string().find(canonicalBase.string()) != 0)
        {
            res->writeStatus("403 Forbidden");
            res->end("Access denied");
            return;
        }

        // Check if file exists
        if (!std::filesystem::exists(canonicalPath) || !std::filesystem::is_regular_file(canonicalPath))
        {
            res->writeStatus("404 Not Found");
            res->end("File not found");
            return;
        }

        // Read the file
        std::ifstream file(canonicalPath, std::ios::binary);
        if (!file)
        {
            res->writeStatus("500 Internal Server Error");
            res->end("Failed to read file");
            return;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();

        // Determine content type based on file extension
        std::string extension = canonicalPath.extension().string();
        std::string contentType = "application/octet-stream";

        if (extension == ".html" || extension == ".htm")
            contentType = "text/html";
        else if (extension == ".css")
            contentType = "text/css";
        else if (extension == ".js" || extension == ".mjs")
            contentType = "text/javascript";
        else if (extension == ".json")
            contentType = "application/json";
        else if (extension == ".png")
            contentType = "image/png";
        else if (extension == ".jpg" || extension == ".jpeg")
            contentType = "image/jpeg";
        else if (extension == ".gif")
            contentType = "image/gif";
        else if (extension == ".svg")
            contentType = "image/svg+xml";
        else if (extension == ".txt")
            contentType = "text/plain";

        res->writeHeader("Content-Type", contentType);
        res->end(content);
    });
}

} // namespace capnwebcpp
