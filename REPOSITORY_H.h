#ifndef REPOSITORY_H
#define REPOSITORY_H

#include <string>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <queue>
#include <set>
#include <memory> // For std::unique_ptr
#include <algorithm> // For std::set_union, etc.

#include "Utils.h" // Your comprehensive utility functions
#include "BLOB_H.h" // Corrected from "Blob.h"
#include "COMMIT_H.h" // Corrected from "Commit.h"
#include "STAGINGAREA_H.h" // Corrected from "StagingArea.h"

class Repository {
private:
    std::filesystem::path workingDir;
    std::filesystem::path minigitDir;
    std::filesystem::path objectsDir;
    std::filesystem::path refsDir; // Points to .minigit/refs/heads
    std::filesystem::path headFile;
    std::filesystem::path configFile; // Not used yet, but good to have

    std::unique_ptr<StagingArea> stagingArea;
    std::unordered_map<std::string, std::string> branches; // branch name -> commit hash
    std::string currentBranch;
    std::string headCommit; // The hash of the commit that HEAD (or the current branch) points to.
    bool detachedHEAD; // True if HEAD points directly to a commit, not a branch

public:
    // Constructor
    Repository(const std::filesystem::path& workingDirectory)
        : workingDir(workingDirectory),
          minigitDir(workingDirectory / ".minigit"),
          objectsDir(minigitDir / "objects"),
          refsDir(minigitDir / "refs" / "heads") { // Correctly...
        headFile = minigitDir / "HEAD";
        stagingArea = std::make_unique<StagingArea>(minigitDir);
        detachedHEAD = false; // Initialize to false
    }

    // Initialize the repository
    bool init() {
        if (std::filesystem::exists(minigitDir)) {
            std::cout << "MiniGit repository already initialized in " << minigitDir << std::endl;
            return false;
        }

        try {
            std::filesystem::create_directories(minigitDir);
            std::filesystem::create_directories(objectsDir);
            std::filesystem::create_directories(refsDir); // refs/heads

            // Initialize HEAD to point to the master branch
            Utils::writeFile(headFile.string(), "ref: refs/heads/master");
            currentBranch = "master";
            branches[currentBranch] = ""; // master points to no commit initially

            // Initialize staging area
            stagingArea->initialize();
            
            // Create a default .gitignore file if it doesn't exist
            std::filesystem::path gitignorePath = workingDir / ".gitignore";
            if (!std::filesystem::exists(gitignorePath)) {
                Utils::writeFile(gitignorePath.string(), ".minigit/\n");
                std::cout << "Created .gitignore with .minigit/ ignored." << std::endl;
            }

            std::cout << "Initialized empty MiniGit repository in " << minigitDir << std::endl;
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Error initializing repository: " << e.what() << std::endl;
            return false;
        }
    }

    // Add file(s) to staging area
    bool add(const std::string& filepath) {
        std::filesystem::path absolutePath = workingDir / filepath;
        if (!std::filesystem::exists(absolutePath)) {
            std::cerr << "Error: file not found '" << filepath << "'" << std::endl;
            return false;
        }
        
        // Convert to relative path
        std::filesystem::path relativePath = std::filesystem::relative(absolutePath, workingDir);
        
        // Add file content to objects and update staging area
        std::string fileContent = Utils::readFile(absolutePath.string());
        std::string blobHash = Utils::computeHash(fileContent);

        // Save blob to objects directory
        if (!Utils::writeFile(objectsDir / blobHash, fileContent)) {
            std::cerr << "Error: Could not write blob for " << filepath << std::endl;
            return false;
        }

        // Add to staging area
        if (!stagingArea->addFile(workingDir, relativePath.string())) {
            std::cerr << "Error: Could not add " << filepath << " to staging area." << std::endl;
            return false;
        }
        std::cout << "Added " << filepath << std::endl;
        return true;
    }

    // Commit changes
    bool commit(const std::string& message) {
        if (!std::filesystem::exists(minigitDir)) {
            std::cerr << "Error: Not a MiniGit repository (or any of the parent directories): " << workingDir << std::endl;
            return false;
        }
        
        // Reload index to get latest changes
        stagingArea->loadIndex();

        if (stagingArea->isEmpty()) {
            std::cout << "Nothing to commit, working tree clean." << std::endl;
            return false;
        }
        
        // Create commit object
        Commit newCommit(message);
        
        // Set parent(s)
        std::string headRefContent = Utils::readFile(headFile.string());
        if (Utils::startsWith(headRefContent, "ref: ")) {
            currentBranch = headRefContent.substr(5);
            std::filesystem::path branchPath = minigitDir / currentBranch;
            std::string branchPathStr = std::string(branchPath.c_str()); // Alternative explicit conversion
            if (std::filesystem::exists(branchPath) && !Utils::readFile(branchPathStr).empty()) {
                newCommit.addParent(Utils::readFile(branchPathStr));
            }
        } else { // Detached HEAD
            if (!headRefContent.empty()) {
                newCommit.addParent(headRefContent);
            }
        }

        // Create snapshot from staging area
        if (!newCommit.createFromStagingArea(stagingArea->getStagingPath(), objectsDir)) {
            std::cerr << "Error creating commit from staging area." << std::endl;
            return false;
        }

        // Save the commit object
        std::string commitHash = Utils::computeHash(newCommit.getCommitMessage() + newCommit.getAuthor() + newCommit.getTimestamp()); // Simplified hash
        newCommit.setHash(commitHash); // Set the computed hash for the commit object

        if (!newCommit.saveToObjectStore(objectsDir)) {
            std::cerr << "Error saving commit object." << std::endl;
            return false;
        }

        // Update branch or HEAD
        if (Utils::startsWith(headRefContent, "ref: ")) {
            Utils::writeFile(minigitDir / currentBranch, commitHash);
            std::cout << "[" << currentBranch << " " << commitHash.substr(0, 7) << "] " << message << std::endl;
        } else { // Detached HEAD
            Utils::writeFile(headFile.string(), commitHash);
            std::cout << "[HEAD detached at " << commitHash.substr(0, 7) << "] " << message << std::endl;
        }
        
        headCommit = commitHash; // Update internal headCommit
        stagingArea->clear(); // Clear staging area after commit
        stagingArea->saveIndex(); // Save empty index

        std::cout << newCommit.getSnapshot().size() << " files committed." << std::endl;
        return true;
    }

    // Show commit history
    void log() {
        if (!std::filesystem::exists(minigitDir)) {
            std::cerr << "Error: Not a MiniGit repository (or any of the parent directories): " << workingDir << std::endl;
            return;
        }

        std::string currentHash = headCommit; // Start from the current HEAD commit
        if (currentHash.empty()) {
            std::string headContent = Utils::readFile(headFile.string());
            if (Utils::startsWith(headContent, "ref: ")) {
                currentBranch = headContent.substr(5);
                std::filesystem::path branchPath = minigitDir / currentBranch;
                std::string branchPathStr = std::string(branchPath.c_str()); // Alternative explicit conversion
                if (std::filesystem::exists(branchPath)) {
                    currentHash = Utils::readFile(branchPathStr);
                }
            } else {
                currentHash = headContent; // Detached HEAD
            }
        }

        if (currentHash.empty()) {
            std::cout << "No commits yet." << std::endl;
            return;
        }

        std::set<std::string> visited; // To prevent infinite loops in case of circular references
        while (!currentHash.empty() && visited.find(currentHash) == visited.end()) {
            visited.insert(currentHash);
            Commit commit = Commit::loadFromObjectStore(objectsDir, currentHash);
            if (!commit.isValid()) {
                std::cerr << "Error: Could not load commit " << currentHash << std::endl;
                break;
            }

            std::cout << "commit " << commit.getHash() << std::endl;
            std::cout << "Author: " << commit.getAuthor() << std::endl;
            std::cout << "Date:   " << commit.getTimestamp() << std::endl;
            std::cout << "\n    " << commit.getCommitMessage() << std::endl;
            
            // Print parents (useful for merge commits)
            if (!commit.getParents().empty()) {
                std::cout << "Parents: ";
                for (const auto& parent : commit.getParents()) {
                    std::cout << parent.substr(0, 7) << " ";
                }
                std::cout << std::endl;
            }
            std::cout << std::endl;

            if (commit.getParents().empty()) {
                currentHash = ""; // No more parents, end of history
            } else {
                // For simplicity, just follow the first parent for now
                // A more advanced log might show a DAG (Directed Acyclic Graph)
                currentHash = commit.getParents()[0];
            }
        }
    }

    // Create a new branch
    bool branch(const std::string& branchName) {
        if (!std::filesystem::exists(minigitDir)) {
            std::cerr << "Error: Not a MiniGit repository (or any of the parent directories): " << workingDir << std::endl;
            return false;
        }
        if (branchName.empty() || branchName.find(' ') != std::string::npos || branchName.find('/') != std::string::npos) {
            std::cerr << "Error: Invalid branch name. Branch names cannot be empty or contain spaces/slashes." << std::endl;
            return false;
        }
        std::filesystem::path branchPath = refsDir / branchName;
        if (std::filesystem::exists(branchPath)) {
            std::cerr << "Error: A branch named '" << branchName << "' already exists." << std::endl;
            return false;
        }

        // Get the current HEAD commit hash
        std::string headRefContent = Utils::readFile(headFile.string());
        std::string currentCommitHash;

        if (Utils::startsWith(headRefContent, "ref: ")) {
            std::string currentBranchPath = minigitDir / headRefContent.substr(5);
            std::string currentBranchPathStr = std::string(currentBranchPath.c_str()); // Alternative explicit conversion
            if (std::filesystem::exists(currentBranchPath)) {
                currentCommitHash = Utils::readFile(currentBranchPathStr);
            }
        } else { // Detached HEAD
            currentCommitHash = headRefContent;
        }

        if (currentCommitHash.empty()) {
            std::cerr << "Error: Cannot create branch from an empty repository (no commits yet)." << std::endl;
            return false;
        }

        if (!Utils::writeFile(branchPath.string(), currentCommitHash)) {
            std::cerr << "Error creating branch file for '" << branchName << "'." << std::endl;
            return false;
        }
        std::cout << "Branch '" << branchName << "' created pointing to " << currentCommitHash.substr(0, 7) << std::endl;
        return true;
    }

    // List existing branches
    void listBranches() {
        if (!std::filesystem::exists(refsDir)) {
            std::cerr << "No branches found." << std::endl;
            return;
        }

        std::string currentHeadRefContent = Utils::readFile(headFile.string());
        std::string activeBranchName = "";
        if (Utils::startsWith(currentHeadRefContent, "ref: refs/heads/")) {
            activeBranchName = currentHeadRefContent.substr(strlen("ref: refs/heads/"));
        }
        
        std::cout << "Branches:" << std::endl;
        for (const auto& entry : std::filesystem::directory_iterator(refsDir)) {
            if (entry.is_regular_file()) {
                std::string branchName = entry.path().filename().string();
                std::string branchCommitHash = Utils::readFile(entry.path().string());
                std::cout << (branchName == activeBranchName ? "* " : "  ") 
                          << branchName << " (" << branchCommitHash.substr(0, 7) << ")" << std::endl;
            }
        }
        // If detached HEAD, display current commit
        if (!activeBranchName.empty() && Utils::startsWith(currentHeadRefContent, "ref: ")) {
            // Already handled by showing active branch
        } else {
            // Detached HEAD case
            std::string headCommitHash = Utils::readFile(headFile.string());
            if (!headCommitHash.empty() && !Utils::startsWith(headCommitHash, "ref: ")) {
                std::cout << "* (HEAD detached at " << headCommitHash.substr(0, 7) << ")" << std::endl;
            }
        }
    }

    // Checkout a branch or commit
    bool checkout(const std::string& ref) {
        if (!std::filesystem::exists(minigitDir)) {
            std::cerr << "Error: Not a MiniGit repository (or any of the parent directories): " << workingDir << std::endl;
            return false;
        }
        
        // Load the current head commit to compare for unstaged changes later
        std::string currentHeadCommitHash;
        std::string headRefContent = Utils::readFile(headFile.string());
        if (Utils::startsWith(headRefContent, "ref: ")) {
            std::string currentBranchPath = minigitDir / headRefContent.substr(5);
            std::string currentBranchPathStr = std::string(currentBranchPath.c_str()); // Alternative explicit conversion
            if (std::filesystem::exists(currentBranchPath)) {
                currentHeadCommitHash = Utils::readFile(currentBranchPathStr);
            }
        } else {
            currentHeadCommitHash = headRefContent; // Detached HEAD
        }

        // Check for unstaged changes before checkout
        stagingArea->loadIndex(); // Ensure staging area is up-to-date
        Commit headCommitObj;
        if (!currentHeadCommitHash.empty()) {
            headCommitObj = Commit::loadFromObjectStore(objectsDir, currentHeadCommitHash);
        }
        
        if (stagingArea->hasUnstagedChanges(workingDir, objectsDir, headCommitObj.getSnapshot())) {
            std::cerr << "Error: Your local changes to the following files would be overwritten by checkout:" << std::endl;
            // TODO: List specific files with unstaged changes for better UX
            std::cerr << "Please commit your changes or stash them before you switch branches." << std::endl;
            return false;
        }


        std::string targetCommitHash;
        std::filesystem::path targetBranchPath = refsDir / ref;

        if (std::filesystem::exists(targetBranchPath)) {
            // It's a branch name
            targetCommitHash = Utils::readFile(targetBranchPath.string());
            if (targetCommitHash.empty()) {
                std::cerr << "Error: Branch '" << ref << "' exists but points to no commit." << std::endl;
                return false;
            }
            // Update HEAD to point to the branch
            Utils::writeFile(headFile.string(), "ref: refs/heads/" + ref);
            currentBranch = ref;
            detachedHEAD = false;
            std::cout << "Switched to branch '" << ref << "'" << std::endl;

        } else if (Commit::existsInObjectStore(objectsDir, ref)) {
            // It's a commit hash
            targetCommitHash = ref;
            // Update HEAD to point directly to the commit (detached HEAD)
            Utils::writeFile(headFile.string(), ref);
            currentBranch = ""; // No current branch when detached
            detachedHEAD = true;
            std::cout << "Note: switching to 'HEAD~" << targetCommitHash.substr(0, 7) << "'." << std::endl;
            std::cout << "You are in 'detached HEAD' state." << std::endl;
            
        } else {
            std::cerr << "Error: Reference '" << ref << "' not found. Not a valid branch or commit hash." << std::endl;
            return false;
        }

        // Load target commit
        Commit targetCommit = Commit::loadFromObjectStore(objectsDir, targetCommitHash);
        if (!targetCommit.isValid()) {
            std::cerr << "Error: Could not load target commit " << targetCommitHash << std::endl;
            return false;
        }

        // Restore working directory to the state of the target commit
        if (!targetCommit.restoreToWorkingDirectory(workingDir, objectsDir)) {
            std::cerr << "Error restoring working directory to commit " << targetCommitHash << std::endl;
            return false;
        }
        
        // Clear staging area after successful checkout
        stagingArea->clear();
        stagingArea->saveIndex();
        
        headCommit = targetCommitHash; // Update internal headCommit

        return true;
    }
    
    // Show working tree status
    void status() {
        if (!std::filesystem::exists(minigitDir)) {
            std::cerr << "Error: Not a MiniGit repository (or any of the parent directories): " << workingDir << std::endl;
            return;
        }

        std::string headRefContent = Utils::readFile(headFile.string());
        std::string currentHeadCommitHash;
        std::string currentBranchDisplay = "No branch";

        if (Utils::startsWith(headRefContent, "ref: ")) {
            currentBranchDisplay = "On branch " + headRefContent.substr(strlen("ref: refs/heads/"));
            std::filesystem::path currentBranchPath = minigitDir / currentBranch;
            std::string currentBranchPathStr = std::string(currentBranchPath.c_str()); // Alternative explicit conversion
            if (std::filesystem::exists(currentBranchPath)) {
                currentHeadCommitHash = Utils::readFile(currentBranchPathStr);
            }
        } else { // Detached HEAD
            currentBranchDisplay = "HEAD detached at " + headRefContent.substr(0, 7);
            currentHeadCommitHash = headRefContent;
        }
        
        std::cout << currentBranchDisplay << std::endl;

        stagingArea->loadIndex(); // Ensure current index is loaded

        std::unordered_map<std::string, std::string> headSnapshot;
        if (!currentHeadCommitHash.empty()) {
            Commit headCommit = Commit::loadFromObjectStore(objectsDir, currentHeadCommitHash);
            if (headCommit.isValid()) {
                headSnapshot = headCommit.getSnapshot();
            }
        }

        // Get current working directory files and their hashes
        std::unordered_map<std::string, std::string> workingDirFiles;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(workingDir)) {
            if (entry.is_regular_file()) {
                std::filesystem::path relativePath = std::filesystem::relative(entry.path(), workingDir);
                if (relativePath.string().rfind(".minigit", 0) != 0 && relativePath.string().rfind(".git", 0) != 0) { // Ignore .minigit and .git directories
                    // Also ignore .gitignore itself
                    if (relativePath.string() == ".gitignore") continue;

                    std::string fileContent = Utils::readFile(entry.path().string());
                    workingDirFiles[relativePath.string()] = Utils::computeHash(fileContent);
                }
            }
        }
        
        bool changesToBeCommitted = false;
        bool changesNotStagedForCommit = false;
        bool untrackedFiles = false;

        std::cout << "\nChanges to be committed:" << std::endl;
        std::cout << "  (use \"minigit restore --staged <file>...\" to unstage)" << std::endl;
        std::cout << "  (use \"minigit rm --cached <file>...\" to unstage)" << std::endl; // More traditional git language

        // Files added to staging area
        for (const auto& [filepath, stagedHash] : stagingArea->getStagedFiles()) {
            // If the file was in HEAD and its hash changed, it's a modification
            // If it wasn't in HEAD, it's a new file (addition)
            if (headSnapshot.count(filepath) && headSnapshot.at(filepath) != stagedHash) {
                std::cout << "\tmodified: " << filepath << std::endl;
                changesToBeCommitted = true;
            } else if (!headSnapshot.count(filepath)) {
                std::cout << "\tnew file: " << filepath << std::endl;
                changesToBeCommitted = true;
            } else if (headSnapshot.count(filepath) && headSnapshot.at(filepath) == stagedHash) {
                // If file is staged and its hash matches HEAD's hash, it means it was previously modified and committed, but now staged again without change
                // or it was in HEAD, deleted from WD, then re-added back to staging matching HEAD
                // This case handles a 're-add' of an unchanged file. It should ideally not show up here unless the status is more granular.
                // For simplicity, we assume 'added' or 'modified' means change from HEAD.
                // If it reaches here, it means it's staged but hasn't changed from HEAD. This case might be tricky if not covered in UX.
                 std::cout << "\tmodified: " << filepath << " (staged, no content change from HEAD)" << std::endl;
                 changesToBeCommitted = true; // Still considered a staged change
            }
        }
        // Files marked for removal
        for (const auto& removedFilepath : stagingArea->getRemovedFiles()) {
            std::cout << "\tdeleted:  " << removedFilepath << std::endl;
            changesToBeCommitted = true;
        }
        if (!changesToBeCommitted) {
            std::cout << "  (no changes staged for commit)" << std::endl;
        }

        std::cout << "\nChanges not staged for commit:" << std::endl;
        std::cout << "  (use \"minigit add <file>...\" to update what will be committed)" << std::endl;
        std::cout << "  (use \"minigit restore <file>...\" to discard changes in working directory)" << std::endl;

        // Modified files not staged
        for (const auto& [filepath, wdHash] : workingDirFiles) {
            bool inStaged = stagingArea->getStagedFiles().count(filepath);
            bool inRemoved = stagingArea->getRemovedFiles().count(filepath);
            
            if (inStaged) {
                // File is staged, but working directory version is different
                if (stagingArea->getStagedFiles().at(filepath) != wdHash) {
                    std::cout << "\tmodified: " << filepath << std::endl;
                    changesNotStagedForCommit = true;
                }
            } else if (inRemoved) {
                // File is marked for removal, but still exists in working directory with content
                std::string headHash = headSnapshot.count(filepath) ? headSnapshot.at(filepath) : "";
                if (headHash != wdHash) { // Modified since deletion or new file
                    std::cout << "\tmodified: " << filepath << " (deleted but modified)" << std::endl;
                    changesNotStagedForCommit = true;
                }
            }
            else { // Not in staged or removed, check against HEAD
                if (headSnapshot.count(filepath)) {
                    if (headSnapshot.at(filepath) != wdHash) {
                        std::cout << "\tmodified: " << filepath << std::endl;
                        changesNotStagedForCommit = true;
                    }
                }
            }
        }

        // Deleted files not staged (present in HEAD/staged, but not in working directory, and not marked for removal)
        for (const auto& [filepath, headHash] : headSnapshot) {
            if (!workingDirFiles.count(filepath) && !stagingArea->getStagedFiles().count(filepath) && !stagingArea->getRemovedFiles().count(filepath)) {
                std::cout << "\tdeleted:  " << filepath << std::endl;
                changesNotStagedForCommit = true;
            }
        }
        for (const auto& [filepath, stagedHash] : stagingArea->getStagedFiles()) {
            if (!workingDirFiles.count(filepath) && !stagingArea->getRemovedFiles().count(filepath)) {
                // This means a staged file was deleted from WD, but not removed from staging.
                // This should be caught by "changes to be committed" as a deletion if rm was used,
                // or "changes not staged" as a deletion if simply removed from WD.
                // For now, let's consider it 'deleted' from WD, if not in removedFiles and not in WD.
                std::cout << "\tdeleted:  " << filepath << " (staged but deleted from working directory)" << std::endl;
                changesNotStagedForCommit = true;
            }
        }

        if (!changesNotStagedForCommit) {
            std::cout << "  (no changes not staged for commit)" << std::endl;
        }

        std::cout << "\nUntracked files:" << std::endl;
        std::cout << "  (use \"minigit add <file>...\" to include in what will be committed)" << std::endl;

        // Untracked files (not in HEAD, not in staging, but in working directory)
        for (const auto& [filepath, wdHash] : workingDirFiles) {
            if (!headSnapshot.count(filepath) && !stagingArea->getStagedFiles().count(filepath) && !stagingArea->getRemovedFiles().count(filepath)) {
                std::cout << "\t" << filepath << std::endl;
                untrackedFiles = true;
            }
            // Add a specific check to ignore .gitignore if it's untracked.
            if (filepath == ".gitignore") {
                untrackedFiles = false; // Don't list .gitignore as untracked
            }
        }
        if (!untrackedFiles) {
            std::cout << "  (nothing to commit, working tree clean)" << std::endl;
        }
    }

    // Merge a branch into the current branch
    bool merge(const std::string& branchToMergeName) {
        if (!std::filesystem::exists(minigitDir)) {
            std::cerr << "Error: Not a MiniGit repository (or any of the parent directories): " << workingDir << std::endl;
            return false;
        }
        
        // 0. Ensure no unstaged changes
        stagingArea->loadIndex();
        std::string currentHeadCommitHash;
        std::string headRefContent = Utils::readFile(headFile.string());
        if (Utils::startsWith(headRefContent, "ref: ")) {
            std::string currentBranchPath = minigitDir / headRefContent.substr(5);
            std::string currentBranchPathStr = std::string(currentBranchPath.c_str()); // Alternative explicit conversion
            if (std::filesystem::exists(currentBranchPath)) {
                currentHeadCommitHash = Utils::readFile(currentBranchPathStr);
            }
        } else {
            currentHeadCommitHash = headRefContent; // Detached HEAD
        }

        Commit headCommitObj;
        if (!currentHeadCommitHash.empty()) {
            headCommitObj = Commit::loadFromObjectStore(objectsDir, currentHeadCommitHash);
        }

        if (stagingArea->hasUnstagedChanges(workingDir, objectsDir, headCommitObj.getSnapshot())) {
            std::cerr << "Error: Your local changes to the following files would be overwritten by merge." << std::endl;
            std::cerr << "Please commit your changes or stash them before you merge." << std::endl;
            return false;
        }

        // 1. Get current branch and branch to merge commits
        std::string currentBranch = headRefContent.substr(strlen("ref: refs/heads/")); // Assuming not detached HEAD
        std::string currentCommitHash = Utils::readFile(minigitDir / currentBranch);
        std::string otherCommitHash = Utils::readFile(refsDir / branchToMergeName);

        if (currentCommitHash.empty()) {
            std::cerr << "Error: Current branch '" << currentBranch << "' has no commits." << std::endl;
            return false;
        }
        if (otherCommitHash.empty()) {
            std::cerr << "Error: Branch to merge '" << branchToMergeName << "' has no commits." << std::endl;
            return false;
        }
        if (currentCommitHash == otherCommitHash) {
            std::cout << "Already up-to-date." << std::endl;
            return true;
        }
        
        Commit currentCommit = Commit::loadFromObjectStore(objectsDir, currentCommitHash);
        Commit otherCommit = Commit::loadFromObjectStore(objectsDir, otherCommitHash);

        if (!currentCommit.isValid() || !otherCommit.isValid()) {
            std::cerr << "Error: Could not load one or both merge commits." << std::endl;
            return false;
        }

        // 2. Check for fast-forward merge
        // If otherCommit is an ancestor of currentCommit, then current branch already contains otherCommit's history
        if (Commit::isAncestor(objectsDir, otherCommitHash, currentCommitHash)) {
            std::cout << "Already up-to-date." << std::endl;
            return true;
        }

        // If currentCommit is an ancestor of otherCommit
        if (Commit::isAncestor(objectsDir, currentCommitHash, otherCommitHash)) {
            std::cout << "Fast-forward merge detected." << std::endl;
            // Move current branch to otherCommit
            writeBranchRef(currentBranch, otherCommitHash);
            headCommit = otherCommitHash;
            branches[currentBranch] = otherCommitHash;
            // Restore working directory to otherCommit's state
            otherCommit.restoreToWorkingDirectory(workingDir, objectsDir);
            stagingArea->clear(); // Clear staging area after fast-forward
            stagingArea->saveIndex();
            std::cout << "Updated branch '" << currentBranch << "' to " << otherCommitHash.substr(0, 7) << "." << std::endl;
            return true;
        }

        // 2. Three-way merge
        std::cout << "Performing a three-way merge..." << std::endl;

        std::string lcaHash = Commit::findLCA(objectsDir, currentCommitHash, otherCommitHash);
        if (lcaHash.empty()) {
            std::cerr << "Error: Could not find a common ancestor for merge." << std::endl;
            return false;
        }

        Commit lcaCommit = Commit::loadFromObjectStore(objectsDir, lcaHash);
        if (!lcaCommit.isValid()) {
            std::cerr << "Error: Could not load common ancestor commit " << lcaHash << std::endl;
            return false;
        }

        // Perform merge logic:
        // Get snapshots of LCA, current, and other commits
        const auto& lcaSnapshot = lcaCommit.getSnapshot();
        const auto& currentSnapshot = currentCommit.getSnapshot();
        const auto& otherSnapshot = otherCommit.getSnapshot();

        // Create a map for merged snapshot and a set for conflicts
        std::unordered_map<std::string, std::string> mergedSnapshot = currentSnapshot; // Start with current branch's snapshot
        std::vector<std::string> conflictFiles;
        
        // Track which files were processed to avoid duplicate conflict checks
        std::set<std::string> allFiles;
        for (const auto& pair : lcaSnapshot) allFiles.insert(pair.first);
        for (const auto& pair : currentSnapshot) allFiles.insert(pair.first);
        for (const auto& pair : otherSnapshot) allFiles.insert(pair.first);

        for (const std::string& filepath : allFiles) {
            bool inLCA = lcaSnapshot.count(filepath);
            bool inCurrent = currentSnapshot.count(filepath);
            bool inOther = otherSnapshot.count(filepath);

            std::string lcaHash_file = inLCA ? lcaSnapshot.at(filepath) : "";
            std::string currentHash_file = inCurrent ? currentSnapshot.at(filepath) : "";
            std::string otherHash_file = inOther ? otherSnapshot.at(filepath) : "";

            if (currentHash_file == otherHash_file) {
                // Both branches have the same version, no conflict, use it
                mergedSnapshot[filepath] = currentHash_file;
            } else if (lcaHash_file == currentHash_file) {
                // Current branch didn't change, but other branch did (or added)
                if (inOther) {
                    mergedSnapshot[filepath] = otherHash_file; // Take other's version
                } else {
                    mergedSnapshot.erase(filepath); // Deleted in other
                }
            } else if (lcaHash_file == otherHash_file) {
                // Other branch didn't change, but current branch did (or added)
                if (inCurrent) {
                    mergedSnapshot[filepath] = currentHash_file; // Take current's version
                } else {
                    mergedSnapshot.erase(filepath); // Deleted in current
                }
            } else {
                // Both changed differently, or one changed and other deleted etc. -> CONFLICT!
                conflictFiles.push_back(filepath);
                
                std::cout << "CONFLICT (content): Merge conflict in " << filepath << std::endl;
                
                // Write conflict markers to the working directory
                std::string lcaContent = lcaHash_file.empty() ? "" : Utils::readFile(objectsDir / lcaHash_file);
                std::string currentContent = currentHash_file.empty() ? "" : Utils::readFile(objectsDir / currentHash_file);
                std::string otherContent = otherHash_file.empty() ? "" : Utils::readFile(objectsDir / otherHash_file);

                std::string conflictContent = "<<<<<<< HEAD\n" +
                                              currentContent + "\n" +
                                              "=======\n" +
                                              otherContent + "\n" +
                                              ">>>>>>> " + branchToMergeName + "\n";
                Utils::writeFile(workingDir / filepath, conflictContent); // Write to working directory for user to resolve
            }
        }
        
        if (!conflictFiles.empty()) {
            std::cerr << "Automatic merge failed; fix conflicts and then commit the result." << std::endl;
            return false; // Indicate merge failed due to conflicts
        }

        // If merge successful, create new merge commit
        std::string mergeMessage = "Merge branch '" + branchToMergeName + "' into " + currentBranch;
        Commit mergeCommit(mergeMessage);
        mergeCommit.addParent(currentCommitHash); // First parent: current branch HEAD
        mergeCommit.addParent(otherCommitHash);   // Second parent: merged branch HEAD

        // Set the merged snapshot
        mergeCommit.setSnapshot(mergedSnapshot); // Set the snapshot directly

        // Generate hash and save merge commit
        std::string mergeCommitHash = Utils::computeHash(mergeCommit.getCommitMessage() + mergeCommit.getAuthor() + mergeCommit.getTimestamp());
        mergeCommit.setHash(mergeCommitHash);

        if (!mergeCommit.saveToObjectStore(objectsDir)) {
            std::cerr << "Error saving merge commit object." << std::endl;
            return false;
        }

        // Update the current branch to point to the new merge commit
        writeBranchRef(currentBranch, mergeCommitHash);
        headCommit = mergeCommitHash; // Update internal headCommit

        // Restore working directory to merged state (this should already be handled by conflict writing,
        // but for non-conflicting files, ensure they are correct.)
        mergeCommit.restoreToWorkingDirectory(workingDir, objectsDir);
        
        // Clear staging area and add all merged files to staging
        stagingArea->clear();
        for (const auto& pair : mergedSnapshot) {
            stagingArea->addFile(workingDir, pair.first); // This will re-hash and stage
        }
        stagingArea->saveIndex();

        std::cout << "Merge complete. Created merge commit " << mergeCommitHash.substr(0, 7) << std::endl;
        return true;
    }

private:
    // Helper to read current HEAD reference
    std::string readHeadRef() {
        if (!std::filesystem::exists(headFile)) {
            return "";
        }
        return Utils::readFile(headFile.string());
    }

    // Helper to write branch reference
    bool writeBranchRef(const std::string& branchName, const std::string& commitHash) {
        return Utils::writeFile(refsDir / branchName, commitHash);
    }

    // Helper to load branches from disk
    void loadBranches() {
        branches.clear();
        if (std::filesystem::exists(refsDir)) {
            for (const auto& entry : std::filesystem::directory_iterator(refsDir)) {
                if (entry.is_regular_file()) {
                    std::string branchName = entry.path().filename().string();
                    std::string commitHash = Utils::readFile(entry.path().string());
                    branches[branchName] = commitHash;
                }
            }
        }
    }

    // Helper to resolve HEAD to a commit hash
    void resolveHead() {
        std::string headContent = Utils::readFile(headFile.string());
        if (Utils::startsWith(headContent, "ref: ")) {
            currentBranch = headContent.substr(5); // e.g., "refs/heads/master"
            std::filesystem::path branchPath = minigitDir / currentBranch; // This would be .minigit/refs/heads/master
            std::string branchPathStr = std::string(branchPath.c_str()); // Alternative explicit conversion
            if (std::filesystem::exists(branchPath)) {
                headCommit = Utils::readFile(branchPathStr);
                detachedHEAD = false;
            } else {
                headCommit = ""; // Branch file not found or empty
                detachedHEAD = false; // Still on a branch, just no commit yet
            }
        } else {
            headCommit = headContent; // Detached HEAD, points directly to a commit hash
            currentBranch = "";
            detachedHEAD = true;
        }
    }
    
    // Internal method to delete a branch (used internally or could be exposed as a command)
    bool deleteBranchInternal(const std::string& branchName) {
        std::filesystem::path branchPath = refsDir / branchName;
        if (!std::filesystem::exists(branchPath)) {
            std::cerr << "Error: Branch '" << branchName << "' does not exist." << std::endl;
            return false;
        }
        if (currentBranch == branchName && !detachedHEAD) {
            std::cerr << "Error: Cannot delete branch '" << branchName << "' while on it." << std::endl;
            return false;
        }
        std::filesystem::remove(branchPath);
        branches.erase(branchName);
        std::cout << "Deleted branch '" << branchName << "'." << std::endl;
        return true;
    }
};

#endif // REPOSITORY_H