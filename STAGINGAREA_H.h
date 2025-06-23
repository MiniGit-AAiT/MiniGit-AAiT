#ifndef STAGINGAREA_H
#define STAGINGAREA_H

#include <string>
#include <unordered_map>
#include <fstream>
#include <filesystem> // Required for std::filesystem::path

#include "Utils.h" // Your comprehensive utility functions

class StagingArea {
private:
    std::filesystem::path minigitDir;
    std::filesystem::path indexPath; // Path to the index file
    std::unordered_map<std::string, std::string> stagedFiles; // filepath -> blob hash
    std::set<std::string> removedFiles; // files explicitly marked for removal

public:
    StagingArea(const std::filesystem::path& baseDir)
        : minigitDir(baseDir), indexPath(baseDir / "index") {}

    // Initialize the staging area (create index file if it doesn't exist)
    bool initialize() {
        if (!std::filesystem::exists(indexPath)) {
            // Create an empty index file
            return Utils::writeFile(indexPath.string(), "");
        }
        return true;
    }

    // Add a file to the staging area
    bool addFile(const std::filesystem::path& workingDir, const std::string& filepath) {
        std::filesystem::path absolutePath = workingDir / filepath;
        if (!std::filesystem::exists(absolutePath)) {
            std::cerr << "Error: file not found '" << filepath << "'" << std::endl;
            return false;
        }
        
        // Convert to relative path
        std::filesystem::path relativePath = std::filesystem::relative(absolutePath, workingDir);
        
        // Compute hash and add to stagedFiles
        std::string fileContent = Utils::readFile(absolutePath.string());
        std::string blobHash = Utils::computeHash(fileContent);
        stagedFiles[relativePath.string()] = blobHash; // This line and the next are correct.
        removedFiles.erase(relativePath.string()); // If a file was removed and then re-added
        saveIndex();
        return true;
    }

    // Mark a file for removal from the staging area
    bool removeFile(const std::string& filepath) {
        // Check if the file is currently staged
        bool wasStaged = stagedFiles.erase(filepath);
        removedFiles.insert(filepath); // Mark for removal
        saveIndex();
        std::cout << "Removed " << filepath << std::endl;
        return wasStaged; // Return true if it was explicitly staged
    }

    // Load the index from the file
    void loadIndex() {
        stagedFiles.clear();
        removedFiles.clear();
        if (!std::filesystem::exists(indexPath)) {
            return; // No index file, nothing to load
        }

        std::string content = Utils::readFile(indexPath.string());
        std::stringstream ss(content);
        std::string line;
        while (std::getline(ss, line)) {
            if (line.empty()) continue;
            size_t firstSpace = line.find(' ');
            if (firstSpace == std::string::npos) continue; // Malformed line

            std::string type = line.substr(0, firstSpace);
            std::string data = line.substr(firstSpace + 1);

            if (type == "staged") {
                size_t secondSpace = data.find(' ');
                if (secondSpace == std::string::npos) continue;
                std::string hash = data.substr(0, secondSpace);
                std::string filepath = data.substr(secondSpace + 1);
                stagedFiles[filepath] = hash;
            } else if (type == "removed") {
                removedFiles.insert(data);
            }
        }
    }

    // Save the current state of the staging area to the index file
    bool saveIndex() {
        std::stringstream ss;
        for (const auto& pair : stagedFiles) {
            ss << "staged " << pair.second << " " << pair.first << "\n";
        }
        for (const auto& filepath : removedFiles) {
            ss << "removed " << filepath << "\n";
        }
        return Utils::writeFile(indexPath.string(), ss.str());
    }

    // Check if the staging area is empty
    bool isEmpty() const {
        return stagedFiles.empty() && removedFiles.empty();
    }

    // Get the path to the staging area's index file
    std::filesystem::path getStagingPath() const {
        return indexPath;
    }

    // Get a const reference to staged files
    const std::unordered_map<std::string, std::string>& getStagedFiles() const {
        return stagedFiles;
    }

    // Get a const reference to removed files
    const std::set<std::string>& getRemovedFiles() const {
        return removedFiles;
    }

    // Clear the staging area (after a commit)
    void clear() {
        stagedFiles.clear();
        removedFiles.clear();
    }
    
    // Check for unstaged changes in the working directory
    // This compares working directory files against the current HEAD commit snapshot
    // and also against the staged files.
    bool hasUnstagedChanges(const std::filesystem::path& workingDir, 
                            const std::filesystem::path& objectsDir,
                            const std::unordered_map<std::string, std::string>& headSnapshot) {
        
        // 1. Check for modified/deleted files not staged
        // Iterate through headSnapshot to find deleted/modified files
        for (const auto& [filepath, headHash] : headSnapshot) {
            std::filesystem::path absolutePath = workingDir / filepath;
            bool inStaged = stagedFiles.count(filepath);
            std::string stagedHash = inStaged ? stagedFiles.at(filepath) : "";

            if (!std::filesystem::exists(absolutePath)) {
                // File deleted in working directory
                if (!removedFiles.count(filepath) && (!inStaged || stagedHash != headHash)) {
                    // Not explicitly removed, and not staged for deletion OR staged but content is different from HEAD
                    return true; // Unstaged deletion
                }
            } else {
                // File exists in working directory, check content
                std::string workingDirContent = Utils::readFile(absolutePath.string());
                std::string workingDirHash = Utils::computeHash(workingDirContent);

                if (inStaged) {
                    // File is staged, check if working directory has further modifications
                    if (stagedHash != workingDirHash) {
                        return true; // Staged but modified in WD
                    }
                } else {
                    // File not staged, check if working directory modified compared to HEAD
                    if (headHash != workingDirHash) {
                        return true; // Unstaged modification
                    }
                }
            }
        }

        // 2. Check for newly created untracked files (not in headSnapshot and not in stagedFiles)
        for (const auto& entry : std::filesystem::recursive_directory_iterator(workingDir)) {
            if (entry.is_regular_file()) {
                std::filesystem::path relativePath = std::filesystem::relative(entry.path(), workingDir);
                if (relativePath.string().rfind(".minigit", 0) != 0 && relativePath.string().rfind(".git", 0) != 0) { // Ignore .minigit and .git directories
                    if (relativePath.string() == ".gitignore") continue; // Ignore .gitignore itself

                    if (!headSnapshot.count(relativePath.string()) && !stagedFiles.count(relativePath.string())) {
                        return true; // Untracked file
                    }
                }
            }
        }

        return false; // No unstaged changes found
    }
};

#endif // STAGINGAREA_H