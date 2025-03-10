/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-08-18
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file qlafilter.cc - Quary Log All Filter
 *
 * QLA Filter - Query Log All. A simple query logging filter. All queries passing
 * through the filter are written to a text file.
 *
 * The filter makes no attempt to deal with query packets that do not fit
 * in a single GWBUF.
 */


#include "qlafilter.hh"

#include <cmath>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <sstream>
#include <sys/time.h>
#include <fstream>
#include <sstream>
#include <string>

#include <maxscale/config2.hh>
#include <maxbase/format.hh>
#include <maxscale/modinfo.hh>
#include <maxscale/modutil.hh>
#include <maxscale/service.hh>
#include <maxscale/modulecmd.hh>
#include <maxscale/json_api.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>

using std::string;

namespace
{

uint64_t CAPABILITIES = RCAP_TYPE_STMT_INPUT;

const char HEADER_ERROR[] = "Failed to print header to file %s. Error %i: '%s'.";

namespace cfg = mxs::config;

cfg::Specification s_spec(MXB_MODULE_NAME, cfg::Specification::FILTER);

cfg::ParamEnum<QlaInstance::DurationMultiplier> s_duration_multiplier(
    &s_spec, "duration_unit", "Duration in milliseconds (ms) or microseconds (us)",
    {
        {QlaInstance::DurationMultiplier::DURATION_IN_MILLISECONDS, "ms"},
        {QlaInstance::DurationMultiplier::DURATION_IN_MILLISECONDS, "milliseconds"},
        {QlaInstance::DurationMultiplier::DURATION_IN_MICROSECONDS, "us"},
        {QlaInstance::DurationMultiplier::DURATION_IN_MICROSECONDS, "microseconds"},
    },
    QlaInstance::DurationMultiplier::DURATION_IN_MILLISECONDS,
    cfg::Param::AT_RUNTIME);

cfg::ParamBool s_use_canonical_form(
    &s_spec, "use_canonical_form", "Write queries in canonical form", false,
    cfg::Param::AT_RUNTIME);

cfg::ParamRegex s_match(
    &s_spec, "match", "Only log queries matching this pattern", "",
    cfg::Param::AT_RUNTIME);

cfg::ParamRegex s_exclude(
    &s_spec, "exclude", "Exclude queries matching this pattern from the log", "",
    cfg::Param::AT_RUNTIME);

cfg::ParamString s_user(
    &s_spec, "user", "Log queries only from this user", "",
    cfg::Param::AT_RUNTIME);

cfg::ParamRegex s_user_match(
    &s_spec, "user_match", "Log queries only from users that match this pattern", "",
    cfg::Param::AT_RUNTIME);

cfg::ParamRegex s_user_exclude(
    &s_spec, "user_exclude", "Exclude queries from users that match this pattern", "",
    cfg::Param::AT_RUNTIME);

cfg::ParamRegex s_source_match(
    &s_spec, "source_match", "Log queries only from hosts that match this pattern", "",
    cfg::Param::AT_RUNTIME);

cfg::ParamRegex s_source_exclude(
    &s_spec, "source_exclude", "Exclude queries from hosts that match this pattern", "",
    cfg::Param::AT_RUNTIME);

cfg::ParamString s_source(
    &s_spec, "source", "Log queries only from this network address", "",
    cfg::Param::AT_RUNTIME);

cfg::ParamString s_filebase(
    &s_spec, "filebase", "The basename of the output file",
    cfg::Param::AT_RUNTIME);

cfg::ParamEnumMask<uint32_t> s_options(
    &s_spec, "options", "Regular expression options",
    {
        {0, "case"},
        {PCRE2_CASELESS, "ignorecase"},
        {PCRE2_EXTENDED, "extended"},
    },
    0,
    cfg::Param::AT_RUNTIME);

cfg::ParamEnumMask<uint32_t> s_log_type(
    &s_spec, "log_type", "The type of log file to use",
    {
        {QlaInstance::LOG_FILE_SESSION, "session"},
        {QlaInstance::LOG_FILE_UNIFIED, "unified"},
        {QlaInstance::LOG_FILE_STDOUT, "stdout"},
    },
    QlaInstance::LOG_FILE_SESSION,
    cfg::Param::AT_RUNTIME);

cfg::ParamEnumMask<int64_t> s_log_data(
    &s_spec, "log_data", "Type of data to log in the log files",
    {
        {QlaInstance::LOG_DATA_SERVICE, "service"},
        {QlaInstance::LOG_DATA_SESSION, "session"},
        {QlaInstance::LOG_DATA_DATE, "date"},
        {QlaInstance::LOG_DATA_USER, "user"},
        {QlaInstance::LOG_DATA_QUERY, "query"},
        {QlaInstance::LOG_DATA_REPLY_TIME, "reply_time"},
        {QlaInstance::LOG_DATA_TOTAL_REPLY_TIME, "total_reply_time"},
        {QlaInstance::LOG_DATA_DEFAULT_DB, "default_db"},
        {QlaInstance::LOG_DATA_NUM_ROWS, "num_rows"},
        {QlaInstance::LOG_DATA_REPLY_SIZE, "reply_size"},
        {QlaInstance::LOG_DATA_TRANSACTION, "transaction"},
        {QlaInstance::LOG_DATA_TRANSACTION_DUR, "transaction_time"},
        {QlaInstance::LOG_DATA_NUM_WARNINGS, "num_warnings"},
        {QlaInstance::LOG_DATA_ERR_MSG, "error_msg"},
        {QlaInstance::LOG_DATA_SERVER, "server"},
    },
    QlaInstance::LOG_DATA_DATE | QlaInstance::LOG_DATA_USER | QlaInstance::LOG_DATA_QUERY,
    cfg::Param::AT_RUNTIME);

cfg::ParamString s_newline_replacement(
    &s_spec, "newline_replacement", "Value used to replace newlines", " ",
    cfg::Param::AT_RUNTIME);

cfg::ParamString s_separator(
    &s_spec, "separator", "Defines the separator between elements of a log entry", ",",
    cfg::Param::AT_RUNTIME);

cfg::ParamBool s_flush(
    &s_spec, "flush", "Flush log files after every write", false,
    cfg::Param::AT_RUNTIME);

cfg::ParamBool s_append(
    &s_spec, "append", "Append new entries to log files instead of overwriting them", true,
    cfg::Param::AT_RUNTIME);

auto open_file(const std::string& filename, std::ios_base::openmode mode)
{
    return SFile(new LogFile {filename, mode});
}

void print_string_replace_newlines(const char* sql_string, size_t sql_str_len,
                                   const char* rep_newline, std::stringstream* output);

bool check_replace_file(const string& filename, SFile* psFile);
}

QlaInstance::QlaInstance(const string& name)
    : m_settings(name, this)
    , m_name(name)
{
    m_qlalog.start();
}

QlaInstance::Settings::Settings(const std::string& name, QlaInstance* instance)
    : mxs::config::Configuration(name, &s_spec)
    , m_instance(instance)
{
    add_native(&Settings::m_v, &Values::duration_multiplier, &s_duration_multiplier);
    add_native(&Settings::m_v, &Values::use_canonical_form, &s_use_canonical_form);
    add_native(&Settings::m_v, &Values::filebase, &s_filebase);
    add_native(&Settings::m_v, &Values::flush_writes, &s_flush);
    add_native(&Settings::m_v, &Values::append, &s_append);
    add_native(&Settings::m_v, &Values::query_newline, &s_newline_replacement);
    add_native(&Settings::m_v, &Values::separator, &s_separator);
    add_native(&Settings::m_v, &Values::user_name, &s_user);
    add_native(&Settings::m_v, &Values::source, &s_source);
    add_native(&Settings::m_v, &Values::match, &s_match);
    add_native(&Settings::m_v, &Values::exclude, &s_exclude);
    add_native(&Settings::m_v, &Values::options, &s_options);
    add_native(&Settings::m_v, &Values::user_match, &s_user_match);
    add_native(&Settings::m_v, &Values::user_exclude, &s_user_exclude);
    add_native(&Settings::m_v, &Values::source_match, &s_source_match);
    add_native(&Settings::m_v, &Values::source_exclude, &s_source_exclude);
    add_native(&Settings::m_v, &Values::log_file_data_flags, &s_log_data);
    add_native(&Settings::m_v, &Values::log_file_types, &s_log_type);
}

bool QlaInstance::Settings::post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params)
{
    mxb_assert(nested_params.empty());

    m_v.write_session_log = (m_v.log_file_types & LOG_FILE_SESSION);
    m_v.write_unified_log = (m_v.log_file_types & LOG_FILE_UNIFIED);
    m_v.write_stdout_log = (m_v.log_file_types & LOG_FILE_STDOUT);
    m_v.session_data_flags = m_v.log_file_data_flags & ~LOG_DATA_SESSION;
    m_v.exclude = mxs::config::RegexValue(m_v.exclude.pattern(), m_v.options);
    m_v.match = mxs::config::RegexValue(m_v.match.pattern(), m_v.options);
    m_v.user_match = mxs::config::RegexValue(m_v.user_match.pattern(), m_v.options);
    m_v.user_exclude = mxs::config::RegexValue(m_v.user_exclude.pattern(), m_v.options);
    m_v.source_match = mxs::config::RegexValue(m_v.source_match.pattern(), m_v.options);
    m_v.source_exclude = mxs::config::RegexValue(m_v.source_exclude.pattern(), m_v.options);
    return m_instance->post_configure();
}

QlaInstance::~QlaInstance()
{
    m_qlalog.stop();
}

QlaInstance::LogManager::~LogManager()
{
}

QlaFilterSession::QlaFilterSession(QlaInstance& instance, MXS_SESSION* session, SERVICE* service)
    : mxs::FilterSession(session, service)
    , m_log(instance.log())
    , m_user(session->user())
    , m_remote(session->client_remote())
    , m_service(session->service->name())
    , m_ses_id(session->id())
    , m_rotation_count(mxs_get_log_rotation_count())
{
}

QlaFilterSession::~QlaFilterSession()
{
}

bool QlaInstance::post_configure()
{
    bool ok = false;

    if (auto log = LogManager::create(m_settings.values(), m_qlalog))
    {
        m_log.assign(std::move(log));
        ok = true;
    }

    return ok;
}

// static
std::unique_ptr<QlaInstance::LogManager>
QlaInstance::LogManager::create(const QlaInstance::Settings::Values& settings, QlaLog& qlalog)
{
    std::unique_ptr<QlaInstance::LogManager> manager(new QlaInstance::LogManager(settings, qlalog));

    if (!manager->prepare())
    {
        manager.reset();
    }

    return manager;
}

QlaInstance::LogManager::LogManager(const QlaInstance::Settings::Values& settings, QlaLog& qlalog)
    : m_settings(settings)
    , m_rotation_count(mxs_get_log_rotation_count())
    , m_qlalog(qlalog)
{
    m_sUnified_file = std::make_shared<LogFile>();      // The shared_ptr is always valid
}

bool QlaInstance::LogManager::prepare()
{
    // Try to open the unified log file
    if (m_settings.write_unified_log)
    {
        m_unified_filename = m_settings.filebase + ".unified";
        // Open the file. It is only closed at program exit.
        if (!open_unified_logfile())
        {
            MXB_ERROR("Failed to open file '%s'. Error %i: '%s'.",
                      m_unified_filename.c_str(), errno, mxb_strerror(errno));
            return false;
        }
    }

    if (m_settings.write_stdout_log)
    {
        write_stdout_log_entry(generate_log_header(m_settings.log_file_data_flags));
    }

    return true;
}

QlaInstance* QlaInstance::create(const char* name)
{
    return new QlaInstance(name);
}

mxs::FilterSession* QlaInstance::newSession(MXS_SESSION* session, SERVICE* service)
{
    auto my_session = new(std::nothrow) QlaFilterSession(*this, session, service);
    if (my_session && !my_session->prepare())
    {
        delete my_session;
        my_session = nullptr;
    }

    return my_session;
}

bool QlaFilterSession::prepare()
{
    const auto& settings = m_log->settings();
    m_active = should_activate();

    bool error = false;

    if (m_active & settings.write_session_log)
    {
        // Only open the session file if the corresponding mode setting is used.
        m_filename = mxb::string_printf("%s.%" PRIu64, settings.filebase.c_str(), m_ses_id);
        m_sSession_file = m_log->open_session_log_file(m_filename);
        if (!m_sSession_file)
        {
            error = true;
        }
    }
    return !error;
}

bool QlaInstance::read_to_json(int start, int end, json_t** output) const
{
    bool rval = false;

    if (m_settings.values().write_unified_log)
    {
        rval = (*m_log)->read_to_json(start, end, output);
    }
    else
    {
        *output = mxs_json_error("Filter '%s' does not have unified log file enabled", m_name.c_str());
    }

    return rval;
}

bool QlaInstance::LogManager::read_to_json(int start, int end, json_t** output)
{
    bool rval = false;
    mxb_assert(m_sUnified_file->is_open() && !m_unified_filename.empty());
    std::ifstream file(m_unified_filename);

    if (file)
    {
        json_t* arr = json_array();
        // TODO: Add integer type to modulecmd
        int current = 0;

        /** Skip lines we don't want */
        for (std::string line; current < start && std::getline(file, line); current++)
        {
        }

        /** Read lines until either EOF or line count is reached */
        for (std::string line; std::getline(file, line) && (current < end || end == 0); current++)
        {
            json_array_append_new(arr, json_string(line.c_str()));
        }

        *output = arr;
        rval = true;
    }
    else
    {
        *output = mxs_json_error("Failed to open file '%s'", m_unified_filename.c_str());
    }

    return rval;
}

json_t* QlaInstance::diagnostics() const
{
    return json_null();
}

uint64_t QlaInstance::getCapabilities() const
{
    return CAPABILITIES;
}

json_t* QlaFilterSession::diagnostics() const
{
    json_t* rval = json_object();
    json_object_set_new(rval, "session_filename", json_string(m_filename.c_str()));
    return rval;
}

void QlaInstance::LogManager::check_reopen_file(const string& filename, uint64_t data_flags,
                                                SFile* psFile) const
{
    if (check_replace_file(filename, psFile))
    {
        // New file created, print the log header.
        string header = generate_log_header(data_flags);
        if (!write_to_logfile((*psFile)->log_stream, header))
        {
            MXB_ERROR(HEADER_ERROR, filename.c_str(), errno, mxb_strerror(errno));
        }
    }
    // Either the old file existed or file creation failed.
}

void QlaInstance::LogManager::check_reopen_session_file(const std::string& filename, SFile* psFile) const
{
    check_reopen_file(filename, m_settings.session_data_flags, psFile);
}

bool QlaInstance::LogManager::match_exclude(const char* sql, int len)
{
    return (!m_settings.match || m_settings.match.match(sql, (size_t)len))
           && (!m_settings.exclude || !m_settings.exclude.match(sql, (size_t)len));
}

/**
 * Write QLA log entry/entries to disk
 *
 * @params elems Log entry contents
 */
void QlaFilterSession::write_log_entries(const LogEventElems& elems)
{
    if (m_log->settings().write_session_log)
    {
        int global_rot_count = mxs_get_log_rotation_count();
        if (global_rot_count > m_rotation_count)
        {
            m_rotation_count = global_rot_count;
            m_log->check_reopen_session_file(m_filename, &m_sSession_file);
        }

        if (m_sSession_file)
        {
            string entry = generate_log_entry(m_log->settings().session_data_flags, elems);
            write_session_log_entry(entry);
        }
    }

    if (m_log->settings().write_unified_log || m_log->settings().write_stdout_log)
    {
        string unified_log_entry =
            generate_log_entry(m_log->settings().log_file_data_flags, elems);

        if (m_log->settings().write_unified_log)
        {
            m_log->write_unified_log_entry(unified_log_entry);
        }

        if (m_log->settings().write_stdout_log)
        {
            m_log->write_stdout_log_entry(unified_log_entry);
        }
    }
}

bool QlaFilterSession::routeQuery(GWBUF* queue)
{
    const char* query = NULL;
    int query_len = 0;

    if (m_active && modutil_extract_SQL(*queue, &query, &query_len)
        && (m_matched = m_log->match_exclude(query, query_len)))
    {
        const uint32_t data_flags = m_log->settings().log_file_data_flags;

        m_first_reply = true;
        m_qc_type_mask = 0;     // only set if needed

        m_sql = m_log->settings().use_canonical_form ?
            queue->get_canonical() : queue->get_sql();

        m_begin_time = m_pSession->worker()->epoll_tick_now();

        if (data_flags & (QlaInstance::LOG_DATA_TRANSACTION | QlaInstance::LOG_DATA_TRANSACTION_DUR))
        {
            m_qc_type_mask = qc_get_type_mask(queue);

            if (m_qc_type_mask & QUERY_TYPE_BEGIN_TRX)
            {
                m_trx_begin_time = m_begin_time;
            }
        }

        if (data_flags & QlaInstance::LOG_DATA_DATE)
        {
            using namespace std::chrono;
            auto now = wall_time::Clock::now();
            auto current_second = duration_cast<seconds>(now.time_since_epoch());
            if (current_second != m_last_wall_second)
            {
                m_last_wall_second = current_second;
                m_wall_time_str = wall_time::to_string(now, "%F %T");
            }
        }
    }
    /* Pass the query downstream */
    return mxs::FilterSession::routeQuery(queue);
}

bool QlaFilterSession::clientReply(GWBUF* queue, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    if (m_active)
    {
        if (m_first_reply)
        {
            m_first_response_time = m_pSession->worker()->epoll_tick_now();
            m_first_reply = false;
        }

        if (reply.is_complete() & m_matched)
        {
            LogEventElems elems(m_begin_time,
                                m_sql,
                                m_first_response_time,
                                m_pSession->worker()->epoll_tick_now(),
                                reply,
                                down);

            write_log_entries(elems);
        }
    }

    return mxs::FilterSession::clientReply(queue, down, reply);
}

SFile QlaInstance::LogManager::open_session_log_file(const string& filename) const
{
    return open_log_file(m_settings.session_data_flags, filename);
}

bool QlaInstance::LogManager::open_unified_logfile()
{
    m_sUnified_file = open_log_file(m_settings.log_file_data_flags, m_unified_filename);
    return m_sUnified_file->is_open();
}

/**
 * Open a log file for writing and print a header if file did not exist.
 *
 * @param   data_flags  Data save settings flags
 * @param   filename    Target file path
 * @return  A valid file on success, null otherwise.
 */
SFile QlaInstance::LogManager::open_log_file(uint64_t data_flags, const string& filename) const
{
    std::ifstream try_file(filename);
    bool file_existed = try_file.is_open();

    SFile sFile;
    if (m_settings.append == false)
    {
        // Just open the file (possibly overwriting) and then print header.
        file_existed = false;
        sFile = open_file(filename, std::ios_base::out);
    }
    else
    {
        if (try_file.is_open())
        {
            // set file_existed to false if the file is empty to generate the header
            file_existed = try_file.peek() != std::ifstream::traits_type::eof();
        }
        sFile = open_file(filename, std::ios_base::app);
    }

    if (!sFile->is_open())
    {
        MXB_ERROR("Failed to open file '%s'. Error %i: '%s'.", filename.c_str(), errno, mxb_strerror(errno));
    }
    else if (!file_existed && data_flags != 0)
    {
        string header = generate_log_header(data_flags);
        if (!write_to_logfile(sFile->log_stream, header))
        {
            MXB_ERROR(HEADER_ERROR, filename.c_str(), errno, mxb_strerror(errno));
        }
    }

    return sFile;
}

string QlaInstance::LogManager::generate_log_header(uint64_t data_flags) const
{
    // Print a header.
    const char SERVICE[] = "Service";
    const char SESSION[] = "Session";
    const char DATE[] = "Date";
    const char USERHOST[] = "User@Host";
    const char QUERY[] = "Query";
    const char REPLY_TIME[] = "Reply_time";
    const char TOTAL_REPLY_TIME[] = "Total_reply_time";
    const char DEFAULT_DB[] = "Default_db";
    const char NUM_ROWS[] = "Num_rows";
    const char REPLY_SIZE[] = "Reply_size";
    const char NUM_WARNINGS[] = "Num_warnings";
    const char ERR_MSG[] = "Error_msg";
    const char TRANSACTION[] = "Transaction";
    const char TRANSACTION_DUR[] = "Transaction_time";
    const char SERVER[] = "Server";

    std::stringstream header;
    string curr_sep;    // Use empty string as the first separator
    const string& real_sep = m_settings.separator;

    if (data_flags & LOG_DATA_SERVICE)
    {
        header << SERVICE;
        curr_sep = real_sep;
    }
    if (data_flags & LOG_DATA_SESSION)
    {
        header << curr_sep << SESSION;
        curr_sep = real_sep;
    }
    if (data_flags & LOG_DATA_DATE)
    {
        header << curr_sep << DATE;
        curr_sep = real_sep;
    }
    if (data_flags & LOG_DATA_USER)
    {
        header << curr_sep << USERHOST;
        curr_sep = real_sep;
    }
    if (data_flags & LOG_DATA_REPLY_TIME)
    {
        header << curr_sep << REPLY_TIME;
        curr_sep = real_sep;
    }
    if (data_flags & LOG_DATA_QUERY)
    {
        header << curr_sep << QUERY;
    }
    if (data_flags & LOG_DATA_DEFAULT_DB)
    {
        header << curr_sep << DEFAULT_DB;
    }
    if (data_flags & LOG_DATA_TOTAL_REPLY_TIME)
    {
        header << curr_sep << TOTAL_REPLY_TIME;
        curr_sep = real_sep;
    }
    if (data_flags & LOG_DATA_NUM_ROWS)
    {
        header << curr_sep << NUM_ROWS;
        curr_sep = real_sep;
    }
    if (data_flags & LOG_DATA_REPLY_SIZE)
    {
        header << curr_sep << REPLY_SIZE;
        curr_sep = real_sep;
    }
    if (data_flags & LOG_DATA_TRANSACTION)
    {
        header << curr_sep << TRANSACTION;
        curr_sep = real_sep;
    }
    if (data_flags & LOG_DATA_TRANSACTION_DUR)
    {
        header << curr_sep << TRANSACTION_DUR;
        curr_sep = real_sep;
    }
    if (data_flags & LOG_DATA_NUM_WARNINGS)
    {
        header << curr_sep << NUM_WARNINGS;
        curr_sep = real_sep;
    }
    if (data_flags & LOG_DATA_ERR_MSG)
    {
        header << curr_sep << ERR_MSG;
        curr_sep = real_sep;
    }
    if (data_flags & LOG_DATA_SERVER)
    {
        header << curr_sep << SERVER;
        curr_sep = real_sep;
    }
    header << '\n';
    return header.str();
}

string QlaFilterSession::generate_log_entry(uint64_t data_flags, const LogEventElems& elems)
{
    /* Printing to the file in parts would likely cause garbled printing if several threads write
     * simultaneously, so we have to first print to a string. */
    std::stringstream output;
    string curr_sep;    // Use empty string as the first separator
    const string& real_sep = m_log->settings().separator;

    if (data_flags & QlaInstance::LOG_DATA_SERVICE)
    {
        output << m_service;
        curr_sep = real_sep;
    }
    if (data_flags & QlaInstance::LOG_DATA_SESSION)
    {
        output << curr_sep << m_ses_id;
        curr_sep = real_sep;
    }
    if (data_flags & QlaInstance::LOG_DATA_DATE)
    {
        output << curr_sep << m_wall_time_str;
        curr_sep = real_sep;
    }
    if (data_flags & QlaInstance::LOG_DATA_USER)
    {
        output << curr_sep << m_user << "@" << m_remote;
        curr_sep = real_sep;
    }
    if (data_flags & QlaInstance::LOG_DATA_REPLY_TIME)
    {
        auto secs = mxb::to_secs(elems.first_response_time - elems.begin_time);
        output << curr_sep << int(m_log->settings().duration_multiplier * secs + 0.5);
        curr_sep = real_sep;
    }
    if (data_flags & QlaInstance::LOG_DATA_QUERY)
    {
        output << curr_sep;
        if (!m_log->settings().query_newline.empty())
        {
            print_string_replace_newlines(elems.sql.data(), elems.sql.length(),
                                          m_log->settings().query_newline.c_str(),
                                          &output);
        }
        else
        {
            // The newline replacement is an empty string so print the query as is
            output.write(elems.sql.data(), elems.sql.length());
        }
        curr_sep = real_sep;
    }
    if (data_flags & QlaInstance::LOG_DATA_DEFAULT_DB)
    {
        auto maria_ses = static_cast<MYSQL_session*>(m_pSession->protocol_data());
        const char* db = maria_ses->current_db.empty() ? "(none)" : maria_ses->current_db.c_str();

        output << curr_sep << db;
        curr_sep = real_sep;
    }
    if (data_flags & QlaInstance::LOG_DATA_TOTAL_REPLY_TIME)
    {
        auto secs = mxb::to_secs(elems.last_response_time - elems.begin_time);
        output << curr_sep << int(m_log->settings().duration_multiplier * secs + 0.5);
        curr_sep = real_sep;
    }
    if (data_flags & QlaInstance::LOG_DATA_NUM_ROWS)
    {
        output << curr_sep << elems.reply.rows_read();
        curr_sep = real_sep;
    }
    if (data_flags & QlaInstance::LOG_DATA_REPLY_SIZE)
    {
        output << curr_sep << elems.reply.size();
        curr_sep = real_sep;
    }
    if (data_flags & QlaInstance::LOG_DATA_TRANSACTION)
    {
        output << curr_sep;
        if (m_qc_type_mask & QUERY_TYPE_BEGIN_TRX)
        {
            output << "BEGIN";
        }
        else if (m_qc_type_mask & QUERY_TYPE_COMMIT)
        {
            output << "COMMIT";
        }
        else if (m_qc_type_mask & QUERY_TYPE_ROLLBACK)
        {
            output << "ROLLBACK";
        }
        curr_sep = real_sep;
    }
    if (data_flags & QlaInstance::LOG_DATA_TRANSACTION_DUR)
    {
        output << curr_sep;
        if (m_qc_type_mask & QUERY_TYPE_COMMIT)
        {
            auto secs = mxb::to_secs(elems.last_response_time - m_trx_begin_time);
            output << int(m_log->settings().duration_multiplier * secs + 0.5);
        }
        curr_sep = real_sep;
    }
    if (data_flags & QlaInstance::LOG_DATA_NUM_WARNINGS)
    {
        output << curr_sep << elems.reply.num_warnings();
        curr_sep = real_sep;
    }
    if (data_flags & QlaInstance::LOG_DATA_ERR_MSG)
    {
        output << curr_sep;
        if (elems.reply.error())
        {
            output << elems.reply.error().message();
        }
        curr_sep = real_sep;
    }
    if (data_flags & QlaInstance::LOG_DATA_SERVER)
    {
        output << curr_sep;
        if (!elems.down.empty())
        {
            output << elems.down.front()->target()->name();
        }
        curr_sep = real_sep;
    }
    output << "\n";
    return output.str();
}

bool QlaInstance::LogManager::write_to_logfile(std::ofstream& of, const std::string& contents) const
{
    bool error = false;

    if (!(of << contents))
    {
        error = true;
    }
    else if (!error && m_settings.flush_writes)
    {
        if (!(of.flush()))
        {
            error = true;
        }
    }

    return !error;
}

/**
 * Write an entry to the session log file.
 *
 * @param   entry  Log entry contents
 */
void QlaFilterSession::write_session_log_entry(const string& entry)
{
    mxb_assert(m_sSession_file);
    if (!m_log->write_to_logfile(m_sSession_file->log_stream, entry))
    {
        if (!m_write_error_logged)
        {
            MXB_ERROR("Failed to write to session log file '%s'. Suppressing further similar warnings.",
                      m_filename.c_str());
            m_write_error_logged = true;
        }
    }
}

bool QlaFilterSession::should_activate()
{
    const auto& settings = m_log->settings();
    bool user_ok = true;
    bool host_ok = true;

    if (!settings.source.empty())
    {
        host_ok = m_remote == settings.source;
    }
    else if (settings.source_match || settings.source_exclude)
    {
        host_ok = (!settings.source_match || settings.source_match.match(m_remote))
            && (!settings.source_exclude || !settings.source_exclude.match(m_remote));
    }

    if (!settings.user_name.empty())
    {
        user_ok = m_user == settings.user_name;
    }
    else if (settings.user_match || settings.user_exclude)
    {
        user_ok = (!settings.user_match || settings.user_match.match(m_user))
            && (!settings.user_exclude || !settings.user_exclude.match(m_user));
    }

    return host_ok && user_ok;
}

/**
 * Write an entry to the shared log file.
 *
 * @param   entry  Log entry contents
 */
void QlaInstance::LogManager::write_unified_log_entry(const string& entry)
{
    int global_rot_count = mxs_get_log_rotation_count();
    if (global_rot_count > m_rotation_count)
    {
        m_rotation_count = global_rot_count;
        std::lock_guard<std::mutex> guard(m_file_lock);
        check_reopen_file(m_unified_filename, m_settings.log_file_data_flags, &m_sUnified_file);
    }

    auto pWorker = mxs::RoutingWorker::get_current();
    auto pShared_data = m_qlalog.get_shared_data_by_index(pWorker->index());
    pShared_data->send_update({m_sUnified_file, entry, m_settings.flush_writes});
}

/**
 * Write an entry to stdout.
 *
 * @param entry Log entry contents
 */
void QlaInstance::LogManager::write_stdout_log_entry(const string& entry) const
{
    std::cout << entry;

    if (m_settings.flush_writes)
    {
        std::cout.flush();
    }
}

namespace
{

void print_string_replace_newlines(const char* sql_string,
                                   size_t sql_str_len,
                                   const char* rep_newline,
                                   std::stringstream* output)
{
    mxb_assert(output);
    size_t line_begin = 0;
    size_t search_pos = 0;
    while (search_pos < sql_str_len)
    {
        int line_end_chars = 0;
        // A newline is either \r\n, \n or \r
        if (sql_string[search_pos] == '\r')
        {
            if (search_pos + 1 < sql_str_len && sql_string[search_pos + 1] == '\n')
            {
                // Got \r\n
                line_end_chars = 2;
            }
            else
            {
                // Just \r
                line_end_chars = 1;
            }
        }
        else if (sql_string[search_pos] == '\n')
        {
            // Just \n
            line_end_chars = 1;
        }

        if (line_end_chars > 0)
        {
            // Found line ending characters, write out the line excluding line end.
            output->write(&sql_string[line_begin], search_pos - line_begin);
            *output << rep_newline;
            // Next line begins after line end chars
            line_begin = search_pos + line_end_chars;
            // For \r\n, advance search_pos
            search_pos += line_end_chars - 1;
        }

        search_pos++;
    }

    // Print anything left
    if (line_begin < sql_str_len)
    {
        output->write(&sql_string[line_begin], sql_str_len - line_begin);
    }
}

/**
 * Open a file if it doesn't exist.
 *
 * @param filename Filename
 * @param ppFile Double pointer to old file. The file can be null.
 * @return True if new file was opened successfully. False, if file already existed or if new file
 * could not be opened. If false is returned, the caller should check that the file object exists.
 */
bool check_replace_file(const string& filename, SFile* psFile)
{
    const char retry_later[] = "Logging to file is disabled. The operation will be retried later.";

    // Check if file exists and create it if not.
    std::ifstream try_file(filename);
    bool newfile = !try_file.is_open();

    if (newfile)
    {
        *psFile = open_file(filename, std::ios_base::app);
        if (!(*psFile)->log_stream.is_open())
        {
            MXB_ERROR("Could not open log file '%s'. open() failed with error code %i: '%s'. %s",
                      filename.c_str(), errno, mxb_strerror(errno), retry_later);
        }
        MXB_INFO("Log file '%s' recreated.", filename.c_str());
    }

    return newfile;
}

bool cb_log(const MODULECMD_ARG* argv, json_t** output)
{
    mxb_assert(argv->argc > 0);
    mxb_assert(argv->argv[0].type.type == MODULECMD_ARG_FILTER);

    MXS_FILTER_DEF* filter = argv[0].argv->value.filter;
    QlaInstance* instance = reinterpret_cast<QlaInstance*>(filter_def_get_instance(filter));
    int start = argv->argc > 1 ? atoi(argv->argv[1].value.string) : 0;
    int end = argv->argc > 2 ? atoi(argv->argv[2].value.string) : 0;

    return instance->read_to_json(start, end, output);
}
}

/**
 * The module entry point routine.
 *
 * @return The module object
 */
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    modulecmd_arg_type_t args[] =
    {
        {
            MODULECMD_ARG_FILTER | MODULECMD_ARG_NAME_MATCHES_DOMAIN,
            "Filter to read logs from"
        },
        {
            MODULECMD_ARG_STRING | MODULECMD_ARG_OPTIONAL,
            "Start reading from this line"
        },
        {
            MODULECMD_ARG_STRING | MODULECMD_ARG_OPTIONAL,
            "Stop reading at this line (exclusive)"
        }
    };

    modulecmd_register_command(MXB_MODULE_NAME, "log", MODULECMD_TYPE_PASSIVE,
                               cb_log, 3, args,
                               "Show unified log file as a JSON array");

    static const char description[] = "A simple query logging filter";
    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXB_MODULE_NAME,
        mxs::ModuleType::FILTER,
        mxs::ModuleStatus::GA,
        MXS_FILTER_VERSION,
        description,
        "V1.1.1",
        CAPABILITIES,
        &mxs::FilterApi<QlaInstance>::s_api,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        &s_spec
    };

    return &info;
}
