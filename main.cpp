#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <functional> // For std::function
#include <numeric>    // For std::accumulate
#include <filesystem> // Required for std::filesystem::current_path()

// IMPORTANT CHANGE: Renamed from .cpp to .h
#include "REPOSITORY_H.h" // Include your main repository header

// --- Helper Functions for CLI output ---

void printGeneralUsage() {
    std::cout << "Usage: minigit <command> [arguments...]\n\n";
    std::cout << "Available commands:\n";
    std::cout << "  init                         Initialize a new MiniGit repository.\n";
    std::cout << "  add <filename>...            Add file(s) to the staging area.\n";
    std::cout << "  commit -m \"<message>\"        Record changes to the repository.\n";
    std::cout << "  log                          Show commit history.\n";
    std::cout << "  branch <branch-name>         Create a new branch.\n";
    std::cout << "  checkout <ref>               Switch branches or restore working tree files.\n";
    std::cout << "  status                       Show the working tree status.\n";
    std::cout << "  ls-branches                  List existing branches.\n";
    std::cout << "  merge <branch-name>          Join two or more development histories together.\n";
    // Add other commands as you implement them
}

void printCommandUsage(const std::string& command) {
    if (command == "add") {
        std::cerr << "Usage: minigit add <filename>...\n";
    } else if (command == "commit") {
        std::cerr << "Usage: minigit commit -m \"<message>\"\n";
    } else if (command == "branch") {
        std::cerr << "Usage: minigit branch <branch-name>\n";
    } else if (command == "checkout") {
        std::cerr << "Usage: minigit checkout <branch-name> | <commit-hash>\n";
    } else if (command == "merge") {
        std::cerr << "Usage: minigit merge <branch-name>\n";
    } else if (command == "log" || command == "status" || command == "ls-branches") {
        // These commands don't take additional arguments
        std::cerr << "Usage: minigit " << command << "\n";
    } else {
        std::cerr << "Error: Unknown command '" << command << "'.\n\n";
        printGeneralUsage(); // Provide general usage for unknown commands
    }
}

// --- Main function for CLI parsing and execution ---

int main(int argc, char* argv[]) {
    // Check if any arguments are provided
    if (argc < 2) {
        printGeneralUsage();
        return 0; // Exit successfully if no command is given
    }

    // The first argument is the command
    std::string command = argv[1];

    // Initialize the repository object. The current working directory is passed to its constructor.
    std::filesystem::path currentPath = std::filesystem::current_path();
    Repository repo(currentPath);

    // Command parsing and execution
    if (command == "init") {
        repo.init();
    } else if (command == "add") {
        // 'add' requires at least one filename
        if (argc < 3) {
            printCommandUsage(command);
            return 1;
        }
        // Iterate through all provided filenames and add them
        for (int i = 2; i < argc; ++i) {
            repo.add(argv[i]);
        }
    } else if (command == "commit") {
        // 'commit' requires '-m' and a message
        if (argc < 4 || std::string(argv[2]) != "-m") {
            printCommandUsage(command);
            return 1;
        }
        std::string message = argv[3]; // The commit message
        repo.commit(message);
    } else if (command == "log") {
        // 'log' takes no additional arguments
        if (argc > 2) {
            std::cerr << "Error: 'log' command does not take arguments.\n";
            printCommandUsage(command);
            return 1;
        }
        repo.log();
    } else if (command == "branch") {
        // 'branch' requires a branch name
        if (argc < 3) {
            printCommandUsage(command);
            return 1;
        }
        std::string branchName = argv[2];
        repo.branch(branchName);
    } else if (command == "checkout") {
        // 'checkout' requires a reference (branch name or commit hash)
        if (argc < 3) {
            printCommandUsage(command);
            return 1;
        }
        std::string ref = argv[2];
        repo.checkout(ref);
    } else if (command == "status") {
        // 'status' takes no additional arguments
        if (argc > 2) {
            std::cerr << "Error: 'status' command does not take arguments.\n";
            printCommandUsage(command);
            return 1;
        }
        repo.status();
    } else if (command == "ls-branches") {
        // 'ls-branches' takes no additional arguments
        if (argc > 2) {
            std::cerr << "Error: 'ls-branches' command does not take arguments.\n";
            printCommandUsage(command);
            return 1;
        }
        repo.listBranches();
    } else if (command == "merge") {
        // 'merge' requires a branch name to merge from
        if (argc < 3) {
            printCommandUsage(command);
            return 1;
        }
        std::string branchToMerge = argv[2];
        repo.merge(branchToMerge);
    } else {
        // Handle unknown commands
        printCommandUsage(command);
        return 1;
    }

    return 0; // Successful execution
}