#pragma once

#include <filesystem>

/*
 * Durability primitives shared by the index, the WAL and the manifest writer.
 *
 * rename(2) is atomic, but atomic is not the same as durable: the rename can be
 * sitting in the page cache when the machine loses power. Making a file durable
 * therefore takes three steps -- fsync the data, rename, then fsync the parent
 * directory so the new link itself survives.
 */

void fsync_file(const std::filesystem::path &path);
void fsync_dir(const std::filesystem::path &path);

/* fsync(tmp) -> rename(tmp, final) -> fsync(parwent). */
void atomic_replace(const std::filesystem::path &tmp, const std::filesystem::path &final_path);