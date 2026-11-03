// Startup-only migration within the app's data container. GPL-3.0; see LICENSE.md.
#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>
namespace IOSUserData {
namespace fs=std::filesystem;
struct Migration {unsigned moved=0,kept=0,links=0;};
inline std::string folded(std::string name) {
    for(char &c:name)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
    return name;
}
inline void mergeMissing(const fs::path &source,const fs::path &destination,Migration &stats) {
    fs::create_directories(destination);
    std::map<std::string,fs::path> existing;
    for(const auto &entry:fs::directory_iterator(destination))existing[folded(entry.path().filename().string())]=entry.path();
    std::vector<fs::path> entries;
    for(const auto &entry:fs::directory_iterator(source))entries.push_back(entry.path());
    for(const auto &from:entries) {
        auto status=fs::symlink_status(from);
        if(fs::is_symlink(status)){++stats.links;continue;}
        const auto key=folded(from.filename().string());
        auto found=existing.find(key);
        if(found==existing.end()) {
            // Both locations are in one iOS data container: rename is atomic and
            // does not copy gigabytes of art/replays during the launch deadline.
            const auto to=destination/from.filename();fs::rename(from,to);
            existing[key]=to;++stats.moved;
        } else if(fs::is_directory(status)&&fs::is_directory(fs::symlink_status(found->second))) {
            mergeMissing(from,found->second,stats);
        } else ++stats.kept; // Existing Documents data wins; retain the legacy copy.
    }
}
inline bool migrate(const fs::path &source,const fs::path &destination,Migration &stats,std::string &error) {
    try {
        if(!fs::exists(fs::symlink_status(source)))return true;
        if(source.empty()||destination.empty()||source==source.root_path()||destination==destination.root_path()
            ||!fs::is_directory(fs::symlink_status(source))||fs::is_symlink(fs::symlink_status(destination))) {
            error="invalid migration directories";return false;
        }
        const auto from=fs::weakly_canonical(source),to=fs::weakly_canonical(destination);
        const auto relative=to.lexically_relative(from);
        const auto reverse=from.lexically_relative(to);
        if((!relative.empty()&&*relative.begin()!="..")||(!reverse.empty()&&*reverse.begin()!="..")) {
            error="migration directories overlap";return false;
        }
        mergeMissing(from,to,stats);return true;
    } catch(const fs::filesystem_error &failure) {error=failure.what();return false;}
}
}
