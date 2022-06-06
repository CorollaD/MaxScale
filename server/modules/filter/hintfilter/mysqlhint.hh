#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-06-06
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/ccdefs.hh>
#include <maxscale/hint.h>
#include <maxscale/filter.hh>
#include <maxscale/config2.hh>

namespace std
{
template<>
struct default_delete<HINT>
{
    void operator()(HINT* pHint)
    {
        hint_free(pHint);
    }
};
}

class HintSession;

class HintInstance : public mxs::Filter
{
public:
    static HintInstance*        create(const char* zName);
    mxs::FilterSession*         newSession(MXS_SESSION* pSession, SERVICE* pService) override;
    json_t*                     diagnostics() const override;
    uint64_t                    getCapabilities() const override;
    mxs::config::Configuration& getConfiguration() override;

private:
    HintInstance(const char* zName);

    // This is mainly here to improve error reporting when an unsupported parameter is used
    mxs::config::Configuration m_config;
};

enum TOKEN_VALUE
{
    TOK_MAXSCALE = 1,
    TOK_PREPARE,
    TOK_START,
    TOK_STOP,
    TOK_EQUAL,
    TOK_STRING,
    TOK_ROUTE,
    TOK_TO,
    TOK_MASTER,
    TOK_SLAVE,
    TOK_SERVER,
    TOK_LAST,
    TOK_LINEBRK,
    TOK_END
};

// A simple C++ wrapper for a routing hint
class Hint
{
public:
    Hint(HINT* hint)
        : m_hint(hint)
    {
    }

    ~Hint()
    {
        hint_free(m_hint);
    }

    HINT* dup()
    {
        return hint_dup(m_hint);
    }

private:
    HINT* m_hint {nullptr};
};

// Class that parses text into MaxScale hints
class HintParser
{
public:
    using InputIter = mxs::Buffer::iterator;

    /**
     * Parse text into a hint
     *
     * @param begin InputIterator pointing to the start of the text
     * @param end   InputIterator pointing to the end of the text
     *
     * @return The parsed hint if a valid one was found
     */
    HINT* parse(InputIter begin, InputIter end);

private:

    InputIter m_it;
    InputIter m_end;
    InputIter m_tok_begin;
    InputIter m_tok_end;

    std::vector<std::unique_ptr<HINT>>                     m_stack;
    std::unordered_map<std::string, std::unique_ptr<HINT>> m_named_hints;

    TOKEN_VALUE next_token();
    HINT*       process_definition();
    HINT*       parse_one(InputIter begin, InputIter end);
};

class HintSession : public mxs::FilterSession
{
public:
    HintSession(const HintSession&) = delete;
    HintSession& operator=(const HintSession&) = delete;

    HintSession(MXS_SESSION* session, SERVICE* service);
    bool routeQuery(GWBUF* queue) override;
    bool clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply) override;

private:

    HintParser m_parser;

    // Contains the current COM_STMT_PREPARE ID being executed. This is used to erase the prepared statement
    // in case it fails.
    uint32_t m_current_id {0};

    // The previous PS ID, needed for direct PS execution where the COM_STMT_EXECUTE uses -1 to refer to the
    // previous COM_STMT_PREPARE.
    uint32_t m_prev_id {0};

    // A mapping of prepared statement IDs to the hints that they contain
    std::unordered_map<uint32_t, Hint> m_ps;

    HINT*    process_hints(GWBUF* data);
    uint32_t get_id(GWBUF* buffer) const;
};
