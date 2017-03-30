//
// Created by andy on 3/30/17.
//

#include <boost/filesystem/path.hpp>
#include "util.h"

namespace fs = boost::filesystem;

// taken from http://stackoverflow.com/questions/10167382/boostfilesystem-get-relative-path
fs::path relative_to(boost::filesystem::path from, fs::path to) {
    // Start at the root path and while they are the same then do nothing then when they first
    // diverge take the remainder of the two path and replace the entire from path with ".."
    // segments.
    fs::path::const_iterator fromIter = from.begin();
    fs::path::const_iterator toIter = to.begin();

    // Loop through both
    while (fromIter != from.end() && toIter != to.end() && (*toIter) == (*fromIter)) {
        ++toIter;
        ++fromIter;
    }

    fs::path finalPath;
    while (fromIter != from.end()) {
        finalPath /= "..";
        ++fromIter;
    }

    while (toIter != to.end()) {
        finalPath /= *toIter;
        ++toIter;
    }

    return finalPath;
}
