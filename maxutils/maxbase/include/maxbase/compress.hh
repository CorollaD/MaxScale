#pragma once

#include <iostream>
#include <memory>
#include <vector>
#include <atomic>
#include <future>

class ZSTD_CCtx_s;
class ZSTD_DCtx_s;

namespace maxbase
{

enum class StreamStatus {OK, IN_PROCESS, EMPTY_INPUT_STREAM, CORRUPTED, INTERNAL_ERROR};

// stream reading assumes the input streams are complete, i.e. read() will
// return 0 only if the stram has been fully read.
// StreamCompressor::compress is designed to be called threaded.
class StreamCompressor final
{
public:
    // nthreads is how many threads the compression algo uses,
    // no threads are started in the constructor.
    StreamCompressor(int level, int nthreads = -1, float nice_percent = 0);
    StreamCompressor(StreamCompressor&&) = delete;

    StreamStatus status() const;

    StreamStatus compress(std::unique_ptr<std::istream>&& in,
                          std::unique_ptr<std::ostream>&& out);

    static bool verify_integrity(const std::istream& in);

    size_t      get_last_error() const;
    std::string get_last_error_str() const;

    // Getters mainly for loggin
    int level() const;
    int ntheads() const;
    int niceness() const;

private:
    ZSTD_CCtx_s*              m_pContext;
    std::vector<char>         m_input_buffer;
    std::vector<char>         m_output_buffer;
    std::atomic<StreamStatus> m_status = StreamStatus::OK;
    size_t                    m_last_zerr = 0;
    int                       m_level;
    int                       m_nthreads;
    float                     m_nice = 0;

    void abort_compression();
};

class StreamDecompressor final
{
public:
    StreamDecompressor();

    StreamStatus status() const;

    // Call this if memeber function compress() is run in a separate thread
    void reset_status();

    StreamStatus decompress(std::unique_ptr<std::istream>&& in, std::ostream&);

    size_t      get_last_error() const;
    std::string get_last_error_str() const;

private:
    ZSTD_DCtx_s*      m_pContext;
    std::vector<char> m_input_buffer;
    std::vector<char> m_output_buffer;

    size_t m_last_zerr = 0;

    std::atomic<StreamStatus> m_status = StreamStatus::OK;
};

// IMPLEMENTATION
inline int StreamCompressor::level() const
{
    return m_level;
}

inline int StreamCompressor::ntheads() const
{
    return m_nthreads;
}

inline int StreamCompressor::niceness() const
{
    return m_nice;
}
}
