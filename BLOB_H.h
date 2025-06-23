#ifndef BLOB_H
#define BLOB_H

#include <string>

// This class represents a blob object, which stores the content of a file.
class Blob {
public:
    Blob() {} // Default constructor
    Blob(const std::string& content) : content(content) {
        // Blob hash will typically be the hash of its content 
        // This will be handled when adding files to the staging area and saving blobs.
    }

    std::string content;
    std::string hash; // The hash of the blob content
};

#endif // BLOB_H