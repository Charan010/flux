#pragma once

#include <string>

#include "object_store.h"

class Restorer{
public:
    explicit Restorer(const ObjectStore& objectStore);

    void restore(const std::string& manifestPath, const std::string& outputPath) const;

private:
    const ObjectStore& objectStore_;
};