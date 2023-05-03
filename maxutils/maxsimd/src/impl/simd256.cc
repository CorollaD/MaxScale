/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#if defined (__x86_64__)

#include "simd256.hh"

#include <maxbase/assert.h>
#include <array>
#include <iostream>
#include <iomanip>

using namespace std::string_literals;

namespace maxsimd
{
namespace simd256
{

MXS_AVX2_FUNC std::string to_string(__m256i reg)
{
    using namespace std;

    ostringstream os;
    auto regc = reinterpret_cast<unsigned char*>(&reg);
    for (int i = 0; i < SIMD_BYTES; ++i)
    {
        os << regc[i];
    }

    return os.str();
}

MXS_AVX2_FUNC std::string to_hex_string(__m256i reg)
{
    using namespace std;

    ostringstream os;
    auto regc = reinterpret_cast<unsigned char*>(&reg);
    for (int i = 0; i < SIMD_BYTES; ++i)
    {
        if (i)
        {
            os << ' ';
        }
        os << hex << setw(2) << int(regc[i]);
    }

    return os.str();
}

MXS_AVX2_FUNC __m256i make_ascii_bitmap(const std::string& chars)
{
    std::array<unsigned char, SIMD_BYTES> bitmap {};
    for (unsigned char ch : chars)
    {
        if (ch & 0b10000000 || ch == '\0')
        {
            mxb_assert(!true);
            continue;
        }
        auto index = ch & 0b00001111;
        char bit = 1 << (ch >> 4);
        bitmap[index] |= bit;           // upper 128-bit lane
        bitmap[index + 16] |= bit;      // lower 128-bit lane
    }

    return _mm256_loadu_si256((__m256i*) bitmap.data());
}
}
}
#endif
