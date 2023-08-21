#include <maxbase/compress.hh>
#include <maxbase/log.hh>
#include <maxbase/assert.hh>
#include <sstream>
#include <zstd.h>
#include <thread>
#include <algorithm>
#include <thread>

namespace maxbase
{
StreamCompressor::StreamCompressor(int level, int nthreads, float nice_percent)
    : m_pContext(ZSTD_createCCtx())
    , m_input_buffer(ZSTD_CStreamInSize())
    , m_output_buffer(ZSTD_CStreamOutSize())
    , m_input_size(ZSTD_CStreamInSize())
    , m_output_size(ZSTD_CStreamOutSize())
    , m_level(level)
    , m_nthreads(nthreads)
    , m_nice(nice_percent)
{
    if (m_nthreads == -1)
    {
        m_nthreads = std::thread::hardware_concurrency();
    }

    if (m_pContext == nullptr)
    {
        m_status = StreamStatus::INTERNAL_ERROR;
        MXB_SERROR("Failed to create StreamCompressor context");
    }
}

size_t StreamCompressor::last_error() const
{
    return m_last_zerr;
}

std::string StreamCompressor::last_error_str() const
{
    return ZSTD_getErrorName(m_last_zerr);
}

void StreamCompressor::set_level(int level)
{
    m_level = level;
}

void StreamCompressor::set_nthread(int nthreads)
{
    m_nthreads = nthreads;
}

void StreamCompressor::set_nice(float percent)
{
    m_nice = std::clamp(percent, 0.0f, 75.0f);
}

void StreamCompressor::set_buffer_sizes(size_t input_size, size_t output_size)
{
    m_input_size = input_size;
    m_output_size = output_size;
}

StreamStatus StreamCompressor::compress(std::unique_ptr<std::istream>&& in,
                                        std::unique_ptr<std::ostream>&& out)
{
    mxb_assert(m_pContext != nullptr);
    m_status.store(StreamStatus::IN_PROCESS, std::memory_order_relaxed);

    if (m_input_buffer.size() != m_input_size)
    {
        m_input_buffer.resize(m_input_size);
    }

    if (m_output_buffer.size() != m_output_size)
    {
        m_output_buffer.resize(m_output_size);
    }

    if (m_status.load(std::memory_order_relaxed) != StreamStatus::OK)
    {
        m_last_zerr = ZSTD_CCtx_reset(m_pContext, ZSTD_reset_session_only);

        if (ZSTD_isError(m_last_zerr))
        {
            MXB_SERROR("Failed to reset compressor context: " << last_error_str());
            m_status.store(StreamStatus::INTERNAL_ERROR, std::memory_order_relaxed);
            return m_status;
        }
    }

    auto sInput_stream = std::move(in);
    auto sOutput_stream = std::move(out);

    m_last_zerr = ZSTD_CCtx_setParameter(m_pContext, ZSTD_c_checksumFlag, 1);

    if (!ZSTD_isError(m_last_zerr))
    {
        m_last_zerr = ZSTD_CCtx_setParameter(m_pContext, ZSTD_c_compressionLevel, m_level);
    }

    if (ZSTD_isError(m_last_zerr))
    {
        MXB_SERROR("Failed to set compressor parameter: " << last_error_str());
        m_status.store(StreamStatus::INTERNAL_ERROR, std::memory_order_relaxed);
        return m_status;
    }

    if (m_nthreads > 1)
    {
        size_t const ret = ZSTD_CCtx_setParameter(m_pContext, ZSTD_c_nbWorkers, m_nthreads);

        if (ZSTD_isError(ret))
        {
            MXB_SINFO("Installed libzstd does not support multithreading."
                      " Continue single threaded execution");
        }
    }

    long to_read = m_input_buffer.size();
    auto no_input = true;

    for (;;)
    {
        sInput_stream->read(m_input_buffer.data(), to_read);
        if (sInput_stream->gcount() == 0)
        {
            break;
        }
        no_input = false;

        auto mode = sInput_stream->gcount() < to_read ? ZSTD_e_end : ZSTD_e_continue;
        ZSTD_inBuffer input{m_input_buffer.data(), size_t(sInput_stream->gcount()), 0};

        while (input.pos < input.size)
        {
            ZSTD_outBuffer output = {m_output_buffer.data(), m_output_buffer.size(), 0};
            m_last_zerr = ZSTD_compressStream2(m_pContext, &output, &input, mode);

            if (ZSTD_isError(m_last_zerr))
            {
                MXB_SERROR("Compression error: " << last_error_str());
                m_status.store(StreamStatus::INTERNAL_ERROR, std::memory_order_relaxed);
                break;
            }

            sOutput_stream->write(m_output_buffer.data(), output.pos);
        }
    }

    if (no_input)
    {
        MXB_SERROR("Empty input stream");
        m_status.store(StreamStatus::EMPTY_INPUT_STREAM, std::memory_order_relaxed);
    }
    else if (m_status.load(std::memory_order_relaxed) == StreamStatus::IN_PROCESS)
    {
        m_status.store(StreamStatus::OK, std::memory_order_relaxed);
    }

    ZSTD_freeCCtx(m_pContext);

    return m_status.load(std::memory_order_relaxed);
}

maxbase::StreamStatus maxbase::StreamCompressor::status() const
{
    return m_status.load(std::memory_order_relaxed);
}

bool StreamCompressor::verify_integrity(const std::istream& in)
{
    // TODO, what is the fastest method.
    return false;
}

StreamDecompressor::StreamDecompressor(int flush_nchars)
    : m_pContext(ZSTD_createDCtx())
    , m_flush_nchars(flush_nchars)
    , m_input_size(ZSTD_CStreamInSize())
    , m_output_size(ZSTD_CStreamOutSize())
{
    if (m_pContext == nullptr)
    {
        m_status.store(StreamStatus::INTERNAL_ERROR, std::memory_order_relaxed);
        MXB_SERROR("Could not create decompressor context.");
    }

    m_input_buffer.resize(ZSTD_CStreamInSize());
    m_output_buffer.resize(ZSTD_CStreamOutSize());
}

StreamStatus StreamDecompressor::status() const
{
    return m_status.load(std::memory_order_relaxed);
}

StreamStatus StreamDecompressor::decompress(std::unique_ptr<std::istream>&& in,
                                    std::ostream& out)
{
    mxb_assert(m_pContext != nullptr);

    m_status.store(StreamStatus::IN_PROCESS, std::memory_order_relaxed);

    if (m_input_buffer.size() != m_input_size)
    {
        m_input_buffer.resize(m_input_size);
    }

    if (m_output_buffer.size() != m_output_size)
    {
        m_output_buffer.resize(m_output_size);
    }\
    // ZSTD_DCtx_reset
    auto sInput_stream = std::move(in);

    auto to_read = m_input_buffer.size();
    auto no_input = true;
    size_t last_ret = 0;
    size_t chars_out = 0;

    for (;;)
    {
        sInput_stream->read(m_input_buffer.data(), to_read);
        if (sInput_stream->gcount() == 0)
        {
            break;
        }

        no_input = false;

        ZSTD_inBuffer input{m_input_buffer.data(), size_t(sInput_stream->gcount()), 0};
        while (input.pos < input.size)
        {
            ZSTD_outBuffer output = {m_output_buffer.data(), m_output_buffer.size(), 0};

            m_last_zerr = ZSTD_decompressStream(m_pContext, &output, &input);

            if (ZSTD_isError(m_last_zerr))
            {
                MXB_SERROR("Decompression error = " << ZSTD_getErrorName(m_last_zerr));
                m_status.store(StreamStatus::INTERNAL_ERROR, std::memory_order_relaxed);
                break;
            }

            out.write(m_output_buffer.data(), output.pos);
            chars_out += output.pos;
            if (m_flush_nchars && chars_out > m_flush_nchars)
            {
                out.flush();
                chars_out = 0;
            }
        }
    }

    out.flush();

    if (no_input)
    {
        MXB_SERROR("Empty input stream");
        m_status.store(StreamStatus::EMPTY_INPUT_STREAM, std::memory_order_relaxed);
    }
    else if (last_ret)
    {
        MXB_SERROR("Decompression error, possible truncated stream.");
        m_status.store(StreamStatus::CORRUPTED, std::memory_order_relaxed);
    }
    else if (m_status.load(std::memory_order_relaxed) == StreamStatus::IN_PROCESS)
    {
        m_status.store(StreamStatus::OK, std::memory_order_relaxed);
    }

    ZSTD_freeDCtx(m_pContext);

    std::cerr << "DECOMPRESS DONE" << std::endl;

    return m_status.load(std::memory_order_relaxed);
}

size_t StreamDecompressor::last_error() const
{
    return m_last_zerr;
}

std::string StreamDecompressor::last_error_str() const
{
    return ZSTD_getErrorName(m_last_zerr);
}

void maxbase::StreamDecompressor::set_buffer_sizes(size_t input_size, size_t output_size)
{
    m_input_size = input_size;
    m_output_size = output_size;
}
}
