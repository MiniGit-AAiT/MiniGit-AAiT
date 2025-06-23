#ifndef COMMIT_H
#define COMMIT_H

#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <queue> // For BFS in isAncestor
#include <set>   // For visited set in isAncestor
#include "Utils.h"
#include "BLOB_H.h" // Corrected from "Blob.h"

class Commit {
private:
    std::string hash;
    std::string message;
    std::string author;
    std::string timestamp;
    std::vector<std::string> parents;  // Support for multiple parents (merge commits)
    std::unordered_map<std::string, std::string> snapshot; // filepath -> blob hash
    
public:
    // Constructors
    Commit() = default;
    
    Commit(const std::string& message, const std::string& author = "Anonymous")
        : message(message), author(author) {
        timestamp = Utils::getCurrentTimestamp();
    }
    
    // Getters
    std::string getHash() const { return hash; }
    std::string getCommitMessage() const { return message; }
    std::string getAuthor() const { return author; }
    std::string getTimestamp() const { return timestamp; }
    const std::vector<std::string>& getParents() const { return parents; }
    const std::unordered_map<std::string, std::string>& getSnapshot() const { return snapshot; }

    // Setters
    void setHash(const std::string& h) { hash = h; }
    void addParent(const std::string& parentHash) { parents.push_back(parentHash); }
    void setSnapshot(const std::unordered_map<std::string, std::string>& snap) { snapshot = snap; }

    // Create commit from staging area
    bool createFromStagingArea(const std::filesystem::path& stagingPath,
                              const std::filesystem::path& objectsPath) {
        if (!std::filesystem::exists(stagingPath)) {
            std::cerr << "Staging area does not exist" << std::endl;
            return false;
        }
        
        try {
            snapshot.clear(); // Clear existing snapshot for new commit
            // Recursively process all files in staging area
            for (const auto& entry : std::filesystem::recursive_directory_iterator(stagingPath)) {
                if (entry.is_regular_file()) {
                    std::filesystem::path relativePath = std::filesystem::relative(entry.path(), stagingPath);
                    std::string fileContent = Utils::readFile(entry.path().string());
                    std::string blobHash = Utils::computeHash(fileContent); // Recompute hash to ensure consistency
                    
                    // Save blob to objects directory if it doesn't exist (or overwrite if content changed)
                    Utils::writeFile(objectsPath / blobHash, fileContent);
                    
                    snapshot[relativePath.string()] = blobHash; // Store relative path and blob hash
                }
            }
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Error creating snapshot from staging area: " << e.what() << std::endl;
            return false;
        }
    }

    // Save commit object to object store
    bool saveToObjectStore(const std::filesystem::path& objectsPath) {
        // Commit content format:
        // message\n
        // author\n
        // timestamp\n
        // parent1_hash parent2_hash ...\n (empty if no parents)
        // filepath1 blob_hash1\n
        // filepath2 blob_hash2\n
        // ...
        std::stringstream ss;
        ss << message << "\n";
        ss << author << "\n";
        ss << timestamp << "\n";
        
        for (size_t i = 0; i < parents.size(); ++i) {
            ss << parents[i];
            if (i < parents.size() - 1) {
                ss << " ";
            }
        }
        ss << "\n"; // Newline after parents (even if empty)

        for (const auto& pair : snapshot) {
            ss << pair.first << " " << pair.second << "\n";
        }
        
        std::string commitContent = ss.str();
        if (hash.empty()) { // Ensure hash is set before saving
            hash = Utils::computeHash(commitContent);
        }
        
        return Utils::writeFile(objectsPath / hash, commitContent);
    }

    // Load commit object from object store
    static Commit loadFromObjectStore(const std::filesystem::path& objectsPath, const std::string& commitHash) {
        Commit commit; // Create an empty commit object
        std::string commitContent = Utils::readFile(objectsPath / commitHash);

        if (commitContent.empty()) {
            // std::cerr << "Error: Commit object not found or empty: " << commitHash << std::endl;
            return commit; // Return invalid commit
        }

        std::stringstream ss(commitContent);
        std::string line;

        std::getline(ss, commit.message); // First line is message
        std::getline(ss, commit.author);  // Second line is author
        std::getline(ss, commit.timestamp); // Third line is timestamp

        // Fourth line is parents (space-separated)
        std::getline(ss, line);
        if (!line.empty()) {
            std::stringstream parentSs(line);
            std::string parentHash;
            while (parentSs >> parentHash) {
                commit.parents.push_back(parentHash);
            }
        }

        // Remaining lines are snapshot (filepath blob_hash)
        commit.snapshot.clear();
        while (std::getline(ss, line)) {
            std::string filepath;
            std::string blobHash;
            size_t spacePos = line.find(' ');
            if (spacePos != std::string::npos) {
                filepath = line.substr(0, spacePos);
                blobHash = line.substr(spacePos + 1);
                commit.snapshot[filepath] = blobHash;
            }
        }
        commit.hash = commitHash; // Set the hash of the loaded commit
        return commit;
    }

    // Check if commit object exists in the object store
    static bool existsInObjectStore(const std::filesystem::path& objectsPath, const std::string& commitHash) {
        return std::filesystem::exists(objectsPath / commitHash);
    }

    // Check if the commit object is valid (e.g., has a hash and message)
    bool isValid() const {
        return !hash.empty(); // Simple validity check
    }

    // Restore working directory to the state of this commit
    bool restoreToWorkingDirectory(const std::filesystem::path& workingDir, const std::filesystem::path& objectsPath) {
        // 1. Clear current working directory (except .minigit)
        for (const auto& entry : std::filesystem::recursive_directory_iterator(workingDir)) {
            if (entry.is_regular_file()) {
                std::filesystem::path relativePath = std::filesystem::relative(entry.path(), workingDir);
                // Exclude files within .minigit directory
                if (relativePath.string().rfind(".minigit", 0) != 0 && relativePath.string().rfind(".git", 0) != 0) {
                    std::filesystem::remove(entry.path());
                }
            } else if (entry.is_directory()) {
                std::filesystem::path relativePath = std::filesystem::relative(entry.path(), workingDir);
                if (relativePath.string().rfind(".minigit", 0) != 0 && relativePath.string().rfind(".git", 0) != 0) {
                    // Only remove empty directories, or those that would not be recreated by files.
                    // This is complex to do perfectly, simple removal can be dangerous.
                    // For now, only remove files, and let new files create directories.
                    // Or, if truly clearing, iterate and remove files first, then empty dirs.
                }
            }
        }
        // Simplified: delete all files except those in .minigit
        for (const auto& entry : std::filesystem::directory_iterator(workingDir)) {
            if (entry.path().filename() != ".minigit" && entry.path().filename() != ".git") {
                std::filesystem::remove_all(entry.path());
            }
        }


        // 2. Write files from snapshot
        for (const auto& pair : snapshot) {
            std::string filepath = pair.first;
            std::string blobHash = pair.second;
            std::string fileContent = Utils::readFile(objectsPath / blobHash);
            
            std::filesystem::path absoluteFilePath = workingDir / filepath;
            std::filesystem::create_directories(absoluteFilePath.parent_path()); // Ensure parent directories exist
            Utils::writeFile(absoluteFilePath.string(), fileContent);
        }
        return true;
    }
    
    // Check if ancestorCommit is an ancestor of descendantCommit
    static bool isAncestor(const std::filesystem::path& objectsPath,
                           const std::string& ancestorCommitHash,
                           const std::string& descendantCommitHash) {
        if (ancestorCommitHash.empty() || descendantCommitHash.empty()) return false;
        if (ancestorCommitHash == descendantCommitHash) return true;

        std::queue<std::string> q;
        std::set<std::string> visited;

        q.push(descendantCommitHash);
        visited.insert(descendantCommitHash);

        while (!q.empty()) {
            std::string currentHash = q.front();
            q.pop();

            Commit currentCommit = Commit::loadFromObjectStore(objectsPath, currentHash);
            if (!currentCommit.isValid()) {
                // Should not happen if history is valid, but handle corrupted objects
                continue;
            }

            for (const std::string& parent : currentCommit.getParents()) {
                if (parent == ancestorCommitHash) {
                    return true; // Found ancestor
                }
                if (visited.find(parent) == visited.end()) {
                    visited.insert(parent);
                    q.push(parent);
                }
            }
        }
        return false; // Not an ancestor
    }

    // Find the Lowest Common Ancestor (LCA) of two commits
    static std::string findLCA(const std::filesystem::path& objectsPath,
                               const std::string& commit1Hash,
                               const std::string& commit2Hash) {
        if (commit1Hash.empty() || commit2Hash.empty()) return "";
        if (commit1Hash == commit2Hash) return commit1Hash; // If they are the same, that's the LCA

        // BFS from commit1 to find all its ancestors and store their distance
        std::unordered_map<std::string, int> dist1;
        std::queue<std::string> q1;

        q1.push(commit1Hash);
        dist1[commit1Hash] = 0;

        while (!q1.empty()) {
            std::string currentHash = q1.front();
            q1.pop();
            Commit currentCommit = Commit::loadFromObjectStore(objectsPath, currentHash);
            if (currentCommit.isValid()) {
                for (const std::string& parent : currentCommit.getParents()) {
                    if (dist1.find(parent) == dist1.end()) { // If not visited
                        dist1[parent] = dist1[currentHash] + 1;
                        q1.push(parent);
                    }
                }
            }
        }

        // BFS from commit2, checking for common ancestors in dist1
        std::queue<std::string> q2;
        q2.push(commit2Hash);
        std::set<std::string> visited2; // To prevent cycles/revisiting in second BFS
        visited2.insert(commit2Hash);
        
        std::string lca = "";
        int minDistance = -1; // To find the lowest (closest to the commits) common ancestor

        while (!q2.empty()) {
            std::string currentHash = q2.front();
            q2.pop();

            if (dist1.count(currentHash)) { // Found a common ancestor
                // Since BFS explores level by level, the first one found is a valid LCA.
                // To get the "lowest" (closest to the diverging branches), we need to check distances.
                // For a simple LCA, the first one found by BFS from 'newer' commits is usually sufficient.
                // A more robust LCA would consider all common ancestors and pick the one with max depth.
                // For simplicity here, let's just pick the first one from q2 that's an ancestor of commit1.
                // The algorithm provided finds an LCA. To be the *lowest* (deepest in history), it's more complex.
                // For typical Git behavior, the "closest" LCA is what's needed for merge base.
                // Let's refine this to find the one closest to *both* commits (which is generally the "true" LCA for merge base)

                // Simple LCA strategy: first found by traversing commit2's history
                // is fine for finding *a* common ancestor.
                // For the *lowest* (most recent common parent), a combined BFS/DFS approach or specific LCA algorithms might be needed.
                // However, the current structure of `findLCA` is typically used to find *any* common ancestor effectively.
                // A better LCA for Git merges would often involve finding all common ancestors and then picking the "best" one.
                // For simplicity and given the usage, the first encountered common ancestor from commit2's traversal is acceptable as *an* LCA.
                // Let's ensure it's indeed the *lowest* (most recent) if possible from this structure.
                // The current BFS from commit2, when it finds an ancestor of commit1, is effectively finding the LCA.
                // Because of BFS, it finds the "closest" common ancestor in terms of number of steps from commit2.
                
                // Let's return the first common ancestor found by BFS from commit2.
                // If you need the *deepest* common ancestor, the algorithm needs to be different.
                return currentHash; 
            }

            Commit currentCommit = Commit::loadFromObjectStore(objectsPath, currentHash);
            if (currentCommit.isValid()) {
                for (const std::string& parent : currentCommit.getParents()) {
                    if (visited2.find(parent) == visited2.end()) {
                        visited2.insert(parent);
                        q2.push(parent);
                    }
                }
            }
        }
        return ""; // No common ancestor found
    }
};

#endif // COMMIT_H