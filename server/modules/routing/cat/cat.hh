/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-07-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/router.hh>

class CatSession;

/**
 * The per instance data for the router.
 */
class Cat : public mxs::Router<Cat, CatSession>
{
    Cat(const Cat&) = delete;
    Cat& operator=(const Cat&) = delete;
public:
    ~Cat();
    static Cat* create(SERVICE* pService, mxs::ConfigParameters* params);
    CatSession* newSession(MXS_SESSION* pSession, const Endpoints& endpoints);
    json_t*     diagnostics() const;
    uint64_t    getCapabilities();

private:
    friend class CatSession;

    /** Internal functions */
    Cat(SERVICE* pService);
};
