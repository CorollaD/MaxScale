/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include <maxscale/ccdefs.hh>

#include <memory>
#include <sstream>
#include <string>

#include <maxbase/assert.h>
#include <maxbase/jansson.h>
#include <maxbase/json.hh>
#include <maxbase/alloc.h>

namespace std
{

template<>
struct default_delete<json_t>
{
    void operator()(json_t* pJson)
    {
        json_decref(pJson);
    }
};
}

namespace maxscale
{

/**
 * @brief Convenience function for dumping JSON into a string
 *
 * @param json JSON to dump
 *
 * @return The JSON in string format
 */
static inline std::string json_dump(const json_t* json, int flags = 0)
{
    return mxb::json_dump(json, flags);
}

/**
 * @brief Convert JSON to string
 *
 * @param JSON to convert
 *
 * @return The JSON value converted to a string
 */
static inline std::string json_to_string(json_t* json)
{
    std::stringstream ss;

    switch (json_typeof(json))
    {
    case JSON_STRING:
        ss << json_string_value(json);
        break;

    case JSON_INTEGER:
        ss << json_integer_value(json);
        break;

    case JSON_REAL:
        ss << json_real_value(json);
        break;

    case JSON_TRUE:
        ss << "true";
        break;

    case JSON_FALSE:
        ss << "false";
        break;

    case JSON_NULL:
        break;

    default:
        mxb_assert(false);
        break;
    }

    return ss.str();
}

/**
 * @brief Convert type of JSON object to string
 *
 * @param json  Object whose type should be returned.
 *
 * @return The type of the object expressed as a string.
 */
static inline const char* json_type_to_string(const json_t* json)
{
    return mxb::json_type_to_string(json);
}
}
