/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>
#include <string>

namespace maxbase
{

namespace ssl_version
{
enum Version
{
    TLS10,
    TLS11,
    TLS12,
    TLS13,
    SSL_MAX,
    TLS_MAX,
    SSL_TLS_MAX,
    SSL_UNKNOWN
};

/**
 * Returns the enum value as string.
 *
 * @param version SSL version
 * @return Version as a string
 */
const char* to_string(Version version);

Version from_string(const char* str);
}  // namespace ssl_version

// SSL configuration
struct SSLConfig
{
    SSLConfig() = default;
    SSLConfig(const std::string& key, const std::string& cert, const std::string& ca);
    bool empty() const;

    std::string key;  /**< SSL private key */
    std::string cert; /**< SSL certificate */
    std::string ca;   /**< SSL CA certificate */

    ssl_version::Version version {ssl_version::SSL_TLS_MAX}; /**< Which TLS version to use */

    bool verify_peer {false}; /**< Enable peer certificate verification */
    bool verify_host {false}; /**< Enable peer host verification */
};
}  // namespace maxbase
