#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <filesystem> // For C++17 filesystem operations
#include <chrono>   // For std::chrono
#include <iomanip>  // For std::put_time
#include <ctime>    // For std::time, std::localtime

namespace Utils {
    // Function to check if a directory exists
    bool directoryExists(const std::string& path) {
        return std::filesystem::is_directory(path);
    }

    // Function to create a directory (including parent directories if needed)
    bool createDirectory(const std::string& path) {
        std::error_code ec; // For capturing errors
        if (std::filesystem::create_directories(path, ec)) {
            return true;
        } else {
            std::cerr << "Error creating directory " << path << ": " << ec.message() << std::endl;
            return false;
        }
    }

    // Function to write content to a file
    bool writeFile(const std::string& filepath, const std::string& content) {
        std::ofstream outFile(filepath);
        if (!outFile.is_open()) {
            std::cerr << "Error: Could not open file for writing: " << filepath << std::endl;
            return false;
        }
        outFile << content;
        outFile.close();
        return true;
    }

    // Function to read content from a file
    std::string readFile(const std::string& filepath) {
        std::ifstream inFile(filepath);
        if (!inFile.is_open()) {
            // std::cerr << "Error: Could not open file for reading: " << filepath << std::endl;
            return ""; // Return empty string or handle error as appropriate
        }
        std::string content((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
        inFile.close();
        return content;
    }

    // Placeholder for a hash function. You will replace this with SHA-1 later.
    std::string computeHash(const std::string& content) {
        // A very basic non-cryptographic hash for demonstration purposes
        size_t hash = 5381; // djb2 hash initial value
        for (char c : content) {
            hash = ((hash << 5) + hash) + c; // hash * 33 + c
        }
        return std::to_string(hash) + "_temp_hash"; // Append a string to make it visually distinct
    }

    // Function to get the base name (filename only) from a path
    std::string getBaseName(const std::string& filepath) {
        return std::filesystem::path(filepath).filename().string();
    }
    
    // Function to get current timestamp in a readable format
    std::string getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&currentTime), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    // Function to check if a string starts with a given prefix
    bool startsWith(const std::string& s, const std::string& prefix) {
        return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
    }
}

#endif // UTILS_H