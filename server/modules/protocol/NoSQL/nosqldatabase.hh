/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include "nosqlprotocol.hh"
#include <memory>
#include <maxscale/target.hh>
#include "nosql.hh"
#include "nosqlcommand.hh"

class Config;

namespace nosql
{

class Database
{
public:
    ~Database();

    Database(const Database&) = delete;
    Database& operator = (const Database&) = delete;

    /**
     * @return The name of the database.
     */
    const std::string& name() const
    {
        return m_name;
    }

    /**
     * @return The context.
     */
    NoSQL::Context& context()
    {
        return m_context;
    }

    const NoSQL::Context& context() const
    {
        return m_context;
    }

    /**
     * @return The config.
     */
    const Config& config() const
    {
        return m_config;
    }

    Config& config()
    {
        return m_config;
    }

    /**
     * Create a new instance.
     *
     * @param name      The database in question.
     * @param pContext  The context to be used, a reference will be stored.
     * @param pConfig   The configuration.
     *
     * @return Unique pointer to the instance.
     */
    static std::unique_ptr<Database> create(const std::string& name,
                                            NoSQL::Context* pContext,
                                            Config* pConfig);

    /**
     * Handle an OP_DELETE
     *
     * @param pRequest  The GWBUF holding data of @c req.
     * @param packet    The delete request.
     *
     * @return nullptr
     */
    GWBUF* handle_delete(GWBUF* pRequest, nosql::Delete&& req);

    /**
     * Handle an OP_INSERT
     *
     * @param pRequest  The GWBUF holding data of @c req.
     * @param req       The insert request.
     *
     * @return nullptr
     */
    GWBUF* handle_insert(GWBUF* pRequest, nosql::Insert&& req);

    /**
     * Handle an OP_QUERY.
     *
     * @param pRequest  The GWBUF holding data of @c req.
     * @param req       The query request; *must* be intended for the database this
     *                  instance represents.
     *
     * @return A GWBUF containing a NoSQL response, or nullptr. In the former case
     *         it will be returned to the client, in the latter case @c clientReply
     *         of the client protocol will eventually be called.
     */
    GWBUF* handle_query(GWBUF* pRequest, nosql::Query&& req);

    /**
     * Handle an OP_UPDATE
     *
     * @param pRequest  The GWBUF holding data of @c req.
     * @param req       The update request.
     *
     * @return nullptr
     */
    GWBUF* handle_update(GWBUF* pRequest, nosql::Update&& req);

    /**
     * Handle an OP_GET_MORE
     *
     * @param pRequest  The GWBUF holding data of @c req.
     * @param req       The get more request.
     *
     * @return nullptr
     */
    GWBUF* handle_get_more(GWBUF* pRequest, nosql::GetMore&& req);

    /**
     * Handle an OP_KILL_CURSORS
     *
     * @pRequest    The GWBUF holding data of @c req.
     * @req         The kill cursor request.
     *
     * @return nullptr
     */
    GWBUF* handle_kill_cursors(GWBUF* pRequest, nosql::KillCursors&& req);

    /**
     * Handle an OP_MSG
     *
     * @param pRequest  The GWBUF holding data of @c req.
     * @param req       The message request.
     *
     * @return A GWBUF containing a NoSQL response, or nullptr. In the former case
     *         it will be returned to the client, in the latter case @c clientReply
     *         of the client protocol will eventually be called.
     */
    GWBUF* handle_msg(GWBUF* pRequest, nosql::Msg&& req);

    /**
     * Convert a MariaDB response to a NoSQL response. Must only be called
     * if an earlier call to @c run_command returned @ nullptr and only with
     * the buffer delivered to @c clientReply of the client protocol.
     *
     * @param mariadb_response  A response as received from downstream.
     *
     * @return @c mariadb_response translated into the equivalent NoSQL response.
     */
    GWBUF* translate(mxs::Buffer&& mariadb_response);

    /**
     * @return True, if there is no pending activity, false otherwise.
     */
    bool is_ready() const
    {
        return m_state == State::READY;
    }

private:
    Database(const std::string& name,
             NoSQL::Context* pContext,
             Config* pConfig);

    bool is_busy() const
    {
        return m_state == State::BUSY;
    }

    void set_busy()
    {
        m_state = State::BUSY;
    }

    void set_ready()
    {
        m_state = State::READY;
    }

    GWBUF* execute_command(std::unique_ptr<Command> sCommand);

    using SCommand = std::unique_ptr<Command>;

    State             m_state { State::READY };
    const std::string m_name;
    NoSQL::Context&   m_context;
    Config&           m_config;
    SCommand          m_sCommand;
};
}
