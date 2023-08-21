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

enum class StreamStatus : char {OK, IN_PROCESS, EMPTY_INPUT_STREAM, CORRUPTED, INTERNAL_ERROR};

// stream reading assumes the input streams are complete, i.e. read() will
// return 0 only if the stream has been fully read.
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

    size_t      last_error() const;
    std::string last_error_str() const;

    // Getters mainly for loggin
    int level() const;
    int ntheads() const;
    int niceness() const;

    // For tuning and testing. These can be called at any time.
    // They will take effect on the next compression call.
    void set_level(int level);
    void set_nthread(int nthreads);
    void set_nice(float percent);
    void set_buffer_sizes(size_t input_size, size_t output_size);

private:
    ZSTD_CCtx_s*              m_pContext;
    std::vector<char>         m_input_buffer;
    std::vector<char>         m_output_buffer;
    std::atomic<StreamStatus> m_status = StreamStatus::OK;
    size_t                    m_last_zerr = 0;
    size_t                    m_input_size;
    size_t                    m_output_size;
    int                       m_level;
    int                       m_nthreads;
    float                     m_nice = 0;

    void abort_compression();
};

class StreamDecompressor final
{
public:
    StreamDecompressor(int flush_nchars = 0);

    StreamStatus status() const;

    StreamStatus decompress(std::unique_ptr<std::istream>&& in, std::ostream&);

    size_t      last_error() const;
    std::string last_error_str() const;

    // For tuning and testing. Can be called at any time.
    // The new sizes will take effect on the next compression call.
    void set_buffer_sizes(size_t input_size, size_t output_size);

private:
    ZSTD_DCtx_s*      m_pContext;
    std::vector<char> m_input_buffer;
    std::vector<char> m_output_buffer;
    size_t            m_flush_nchars;
    size_t            m_last_zerr = 0;
    size_t            m_input_size;
    size_t            m_output_size;

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
