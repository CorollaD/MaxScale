#include <maxbase/compress.hh>
#include <zstd.h>
#include <thread>
#include <algorithm>

namespace maxbase
{
StreamCompressor::StreamCompressor(int level, int nthreads, float nice_percent)
    : m_pContext(ZSTD_createCCtx())
    , m_input_buffer(ZSTD_CStreamInSize())
    , m_output_buffer(ZSTD_CStreamOutSize())
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
        std::cout << "Could not create ZTD context." << std::endl;
    }
}

size_t StreamCompressor::get_last_error() const
{
    return m_last_zerr;
}

std::string StreamCompressor::get_last_error_str() const
{
    return ZSTD_getErrorName(m_last_zerr);
}

StreamStatus StreamCompressor::compress(std::unique_ptr<std::istream>&& in,
                                        std::unique_ptr<std::ostream>&& out)
{
    m_status.store(StreamStatus::IN_PROCESS, std::memory_order_relaxed);

    if (m_status.load(std::memory_order_relaxed) != StreamStatus::OK)
    {
        auto ret = ZSTD_CCtx_reset(m_pContext, ZSTD_reset_session_only);
        if (ZSTD_isError(ret))
        {
            m_status.store(StreamStatus::INTERNAL_ERROR, std::memory_order_relaxed);
            return m_status;
        }
    }

    auto sInput_stream = std::move(in);
    auto sOutput_stream = std::move(out);

    if (m_pContext == nullptr)
    {
        m_status.store(StreamStatus::INTERNAL_ERROR, std::memory_order_relaxed);
        return m_status;
    }

    m_last_zerr = ZSTD_CCtx_setParameter(m_pContext, ZSTD_c_checksumFlag, 1);
    if (!ZSTD_isError(m_last_zerr))
    {
        m_last_zerr = ZSTD_CCtx_setParameter(m_pContext, ZSTD_c_compressionLevel, m_level);
    }

    if (ZSTD_isError(m_last_zerr))
    {
        m_status.store(StreamStatus::INTERNAL_ERROR, std::memory_order_relaxed);
        return m_status;
    }

    if (m_nthreads > 1)
    {
        size_t const ret = ZSTD_CCtx_setParameter(m_pContext, ZSTD_c_nbWorkers, m_nthreads);
        if (ZSTD_isError(ret))
        {
            std::cout << "Note: the linked libzstd library doesn't support multithreading. "
                         "Reverting to single-thread mode. \n";
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
            auto ret = ZSTD_compressStream2(m_pContext, &output, &input, mode);
            if (ZSTD_isError(ret))
            {
                m_status.store(StreamStatus::INTERNAL_ERROR, std::memory_order_relaxed);
                m_last_zerr = ret;
                break;
            }

            sOutput_stream->write(m_output_buffer.data(), output.pos);
        }
    }

    if (no_input)
    {
        // make this an error
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
    // TODO, what is the fastest method. Is this needed?
    return false;
}

StreamDecompressor::StreamDecompressor()
    : m_pContext(ZSTD_createDCtx())
{
    if (m_pContext == nullptr)
    {
        m_status.store(StreamStatus::INTERNAL_ERROR, std::memory_order_relaxed);
        std::cout << "Could not create decompressor context." << std::endl;
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
    m_status.store(StreamStatus::IN_PROCESS, std::memory_order_relaxed);

    // ZSTD_DCtx_reset
    auto sInput_stream = std::move(in);

    auto to_read = m_input_buffer.size();
    auto no_input = true;
    size_t last_ret = 0;

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

            auto ret = ZSTD_decompressStream(m_pContext, &output, &input);

            if (ZSTD_isError(ret))
            {
                std::cout << "Decompression error = " << ZSTD_getErrorName(ret) << std::endl;
                m_status.store(StreamStatus::INTERNAL_ERROR, std::memory_order_relaxed);
                m_last_zerr = ret;
                break;
            }

            out.write(m_output_buffer.data(), output.pos);
        }
    }

    out.flush();    // TODO, should probably flush more often.

    if (no_input)
    {
        // make this an error
        m_status.store(StreamStatus::EMPTY_INPUT_STREAM, std::memory_order_relaxed);
    }
    else if (last_ret)
    {
        m_status.store(StreamStatus::CORRUPTED, std::memory_order_relaxed);
    }
    else if (m_status.load(std::memory_order_relaxed) == StreamStatus::IN_PROCESS)
    {
        m_status.store(StreamStatus::OK, std::memory_order_relaxed);
    }

    ZSTD_freeDCtx(m_pContext);

    return m_status.load(std::memory_order_relaxed);
}

size_t StreamDecompressor::get_last_error() const
{
    return m_last_zerr;
}

std::string StreamDecompressor::get_last_error_str() const
{
    return ZSTD_getErrorName(m_last_zerr);
}
}
