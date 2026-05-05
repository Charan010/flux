#include "helper.h"
#include <string>   
#include <memory>   
#include <cstdio>

std::string get_sha256(const std::string& path) {
    std::string cmd = "sha256sum " + path + " 2>/dev/null";
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) return "ERROR";
    
    char buffer[128];
    if (fgets(buffer, 128, pipe.get()) != nullptr) {
        std::string res = buffer;
        return res.substr(0, 64); 
    }
    return "ERROR";
}