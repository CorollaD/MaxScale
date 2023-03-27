/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "postgresprotocol.hh"
#include <maxscale/session.hh>

class PgProtocolData final : public mxs::ProtocolData
{
public:
    ~PgProtocolData();

    bool will_respond(const GWBUF& buffer) const override;
    bool can_recover_state() const override;
    bool is_trx_starting() const override;
    bool is_trx_active() const override;
    bool is_trx_read_only() const override;
    bool is_trx_ending() const override;
    bool is_autocommit() const override;

    size_t amend_memory_statistics(json_t* memory) const override;
    size_t static_size() const override;
    size_t varying_size() const override;

    void set_connect_params(const uint8_t* begin, const uint8_t* end);

    const std::vector<uint8_t>& connect_params() const
    {
        return m_params;
    }

    void set_in_trx(bool in_trx);
    void set_default_database(std::string_view db);

    const std::string& default_db() const;

private:
    std::string          m_database;
    std::vector<uint8_t> m_params;
    bool                 m_in_trx {false};
};
