#pragma once

enum class CompressionMode { Huffman, LZ4 };
enum class JobState { QUEUED, RUNNING, WRITING, COMPLETED, FAILED };