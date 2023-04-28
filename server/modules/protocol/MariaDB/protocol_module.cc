/*
 * Copyright (c) 2019 MariaDB Corporation Ab
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
#include <maxscale/protocol/mariadb/module_names.hh>
#define MXB_MODULE_NAME MXS_MARIADB_PROTOCOL_NAME

#include "protocol_module.hh"
#include <maxbase/format.hh>
#include <maxscale/built_in_modules.hh>
#include <maxscale/cn_strings.hh>
#include <maxscale/listener.hh>
#include <maxscale/protocol/mariadb/backend_connection.hh>
#include <maxscale/protocol/mariadb/client_connection.hh>
#include <maxscale/protocol/mariadb/mariadbparser.hh>
#include "user_data.hh"

using std::string;

namespace
{
const char DEFAULT_QC_NAME[] = "qc_sqlite";

struct ThisUnit
{
    MariaDBParser* pParser = nullptr;
} this_unit;

mxs::config::Specification s_spec(MXB_MODULE_NAME, mxs::config::Specification::PROTOCOL);
}

//
// MariaDBParser
//
MariaDBParser& MariaDBParser::get()
{
    mxb_assert(this_unit.pParser);

    return *this_unit.pParser;
}

//
// MySQLProtocolModule
//
MySQLProtocolModule::MySQLProtocolModule(const std::string& name)
    : m_config(name, &s_spec)
{
}

MySQLProtocolModule* MySQLProtocolModule::create(const std::string& name, mxs::Listener*)
{
    return new MySQLProtocolModule(name);
}

mxs::config::Configuration& MySQLProtocolModule::getConfiguration()
{
    return m_config;
}

std::unique_ptr<mxs::ClientConnection>
MySQLProtocolModule::create_client_protocol(MXS_SESSION* session, mxs::Component* component)
{
    const auto& cnf = *session->service->config();
    std::unique_ptr<mxs::ClientConnection> new_client_proto;
    std::unique_ptr<MYSQL_session> mdb_session(new(std::nothrow) MYSQL_session(cnf.max_sescmd_history,
                                                                               cnf.prune_sescmd_history,
                                                                               cnf.disable_sescmd_history));
    if (mdb_session)
    {
        auto& search_sett = mdb_session->user_search_settings;
        search_sett.listener = m_user_search_settings;
        search_sett.service.allow_root_user = cnf.enable_root;

        auto def_sqlmode = session->listener_data()->m_default_sql_mode;
        mdb_session->set_autocommit(def_sqlmode != mxs::Parser::SqlMode::ORACLE);

        mdb_session->remote = session->client_remote();

        session->set_protocol_data(std::move(mdb_session));

        new_client_proto = std::make_unique<MariaDBClientConnection>(session, component);
    }
    return new_client_proto;
}

std::string MySQLProtocolModule::auth_default() const
{
    return MXS_MARIADBAUTH_AUTHENTICATOR_NAME;
}

GWBUF MySQLProtocolModule::make_error(int errnum, const std::string& sqlstate,
                                      const std::string& message) const
{
    return mariadb::create_error_packet(0, errnum, sqlstate.c_str(), message.c_str());
}

std::string_view MySQLProtocolModule::get_sql(const GWBUF& packet) const
{
    return mariadb::get_sql(packet);
}

std::string MySQLProtocolModule::describe(const GWBUF& packet, int sql_max_len) const
{
    return get_description(packet, sql_max_len);
}

//static
std::string MySQLProtocolModule::get_description(const GWBUF& packet, int body_max_len)
{
    if (packet.length() < MYSQL_HEADER_LEN + 1)
    {
        return "";
    }

    unsigned char command = packet[4];

    MariaDBParser& p = MariaDBParser::get();
    std::string type_mask = mxs::Parser::type_mask_to_string(p.get_type_mask(const_cast<GWBUF&>(packet)));

    const char* pSql;
    int len;
    std::string sql;
    if (mxs_mysql_is_ps_command(command))
    {
        sql = "ID: " + std::to_string(mxs_mysql_extract_ps_id(&packet));
        pSql = sql.c_str();
        len = sql.length();
    }
    else
    {
        std::string_view sv = mariadb::get_sql(packet);
        pSql = sv.data();
        len = sv.length();
    }

    if (len > body_max_len)
    {
        len = body_max_len;
    }

    const char* zType_mask = type_mask.empty() ? "N/A" : type_mask.c_str();
    const char* zHint = packet.hints.empty() ? "" : ", Hint:";
    const char* zHint_type = packet.hints.empty() ? "" : Hint::type_to_str(packet.hints[0].type);

    return mxb::string_printf("cmd: (0x%02x) %s, plen: %u, type: %s, stmt: %.*s%s %s",
                              command,
                              mariadb::cmd_to_string(command),
                              MYSQL_GET_PACKET_LEN(&packet),
                              zType_mask,
                              len,
                              pSql,
                              zHint,
                              zHint_type);
}

GWBUF MySQLProtocolModule::make_query(std::string_view sql) const
{
    return mariadb::create_query(sql);
}

std::string MySQLProtocolModule::name() const
{
    return MXB_MODULE_NAME;
}

std::string MySQLProtocolModule::protocol_name() const
{
    return MXS_MARIADB_PROTOCOL_NAME;
}

std::unique_ptr<mxs::UserAccountManager> MySQLProtocolModule::create_user_data_manager()
{
    return std::unique_ptr<mxs::UserAccountManager>(new MariaDBUserManager());
}

std::unique_ptr<mxs::BackendConnection>
MySQLProtocolModule::create_backend_protocol(MXS_SESSION* session, SERVER* server, mxs::Component* component)
{
    return MariaDBBackendConnection::create(session, component, *server);
}

uint64_t MySQLProtocolModule::capabilities() const
{
    return mxs::ProtocolModule::CAP_BACKEND | mxs::ProtocolModule::CAP_AUTHDATA
           | mxs::ProtocolModule::CAP_AUTH_MODULES;
}

bool MySQLProtocolModule::read_authentication_options(mxs::ConfigParameters* params)
{
    bool error = false;
    if (!params->empty())
    {
        // Read any values recognized by the protocol itself and remove them. The leftovers are given to
        // authenticators.
        const string opt_cachedir = "cache_dir";
        const string opt_inject = "inject_service_user";
        const string opt_skip_auth = "skip_authentication";
        const string opt_match_host = "match_host";
        const string opt_lower_case = "lower_case_table_names";
        const char option_is_ignored[] = "Authenticator option '%s' is no longer supported and "
                                         "its value is ignored.";

        if (params->contains(opt_cachedir))
        {
            MXB_WARNING(option_is_ignored, opt_cachedir.c_str());
            params->remove(opt_cachedir);
        }
        if (params->contains(opt_inject))
        {
            MXB_WARNING(option_is_ignored, opt_inject.c_str());
            params->remove(opt_inject);
        }
        if (params->contains(opt_skip_auth))
        {
            m_user_search_settings.check_password = !params->get_bool(opt_skip_auth);
            params->remove(opt_skip_auth);
        }
        if (params->contains(opt_match_host))
        {
            m_user_search_settings.match_host_pattern = params->get_bool(opt_match_host);
            params->remove(opt_match_host);
        }

        if (params->contains(opt_lower_case))
        {
            // To match the server, the allowed values should be 0, 1 or 2. For backwards compatibility,
            // "true" and "false" should also apply, with "true" mapping to 1 and "false" to 0.
            long lower_case_mode = -1;
            auto lower_case_mode_str = params->get_string(opt_lower_case);
            if (lower_case_mode_str == "true")
            {
                lower_case_mode = 1;
            }
            else if (lower_case_mode_str == "false")
            {
                lower_case_mode = 0;
            }
            else if (!mxb::get_long(lower_case_mode_str, 10, &lower_case_mode))
            {
                lower_case_mode = -1;
            }

            switch (lower_case_mode)
            {
            case 0:
                m_user_search_settings.db_name_cmp_mode = UserDatabase::DBNameCmpMode::CASE_SENSITIVE;
                break;

            case 1:
                m_user_search_settings.db_name_cmp_mode = UserDatabase::DBNameCmpMode::LOWER_CASE;
                break;

            case 2:
                m_user_search_settings.db_name_cmp_mode = UserDatabase::DBNameCmpMode::CASE_INSENSITIVE;
                break;

            default:
                error = true;
                MXB_ERROR("Invalid authenticator option value for '%s': '%s'. Expected 0, 1, or 2.",
                          opt_lower_case.c_str(), lower_case_mode_str.c_str());
            }
            params->remove(opt_lower_case);
        }
    }
    return !error;
}

mxs::ProtocolModule::AuthenticatorList
MySQLProtocolModule::create_authenticators(const mxs::ConfigParameters& params)
{
    // If no authenticator is set, the default authenticator will be loaded.
    auto auth_names = params.get_string(CN_AUTHENTICATOR);
    auto auth_opts = params.get_string(CN_AUTHENTICATOR_OPTIONS);

    if (auth_names.empty())
    {
        auth_names = MXS_MARIADBAUTH_AUTHENTICATOR_NAME;
    }

    // Contains protocol-level authentication options + plugin options.
    mxs::ConfigParameters auth_config;
    // Parse all options. Then read in authentication settings which affect the entire listener.
    if (!parse_auth_options(auth_opts, &auth_config) || !read_authentication_options(&auth_config))
    {
        return {};      // error
    }

    AuthenticatorList authenticators;
    auto auth_names_list = mxb::strtok(auth_names, ",");
    bool error = false;

    for (auto iter = auth_names_list.begin(); iter != auth_names_list.end() && !error; ++iter)
    {
        string auth_name = *iter;
        mxb::trim(auth_name);
        if (!auth_name.empty())
        {
            const char* auth_namez = auth_name.c_str();
            auto new_auth_module = mxs::authenticator_init(auth_name, &auth_config);
            if (new_auth_module)
            {
                // Check that the authenticator supports the protocol. Use case-insensitive comparison.
                auto supported_protocol = new_auth_module->supported_protocol();
                if (strcasecmp(MXB_MODULE_NAME, supported_protocol.c_str()) == 0)
                {
                    authenticators.push_back(move(new_auth_module));
                }
                else
                {
                    // When printing protocol name, print the name user gave in configuration file,
                    // not the effective name.
                    MXB_ERROR("Authenticator module '%s' expects to be paired with protocol '%s', "
                              "not with '%s'.",
                              auth_namez, supported_protocol.c_str(), MXB_MODULE_NAME);
                    error = true;
                }
            }
            else
            {
                MXB_ERROR("Failed to initialize authenticator module '%s'.", auth_namez);
                error = true;
            }
        }
        else
        {
            MXB_ERROR("'%s' is an invalid value for '%s'. The value should be a comma-separated "
                      "list of authenticators or a single authenticator.",
                      auth_names.c_str(), CN_AUTHENTICATOR);
            error = true;
        }
    }

    // All authenticators have been created. Any remaining settings in the config object are unrecognized.
    if (!error && !auth_config.empty())
    {
        error = true;
        for (const auto& elem : auth_config)
        {
            MXB_ERROR("Unrecognized authenticator option: '%s'", elem.first.c_str());
        }
    }

    if (!error)
    {
        // Check if any of the authenticators support anonymous users.
        for (const auto& auth_module : authenticators)
        {
            auto mariadb_auth = static_cast<mariadb::AuthenticatorModule*>(auth_module.get());
            if (mariadb_auth->capabilities() & mariadb::AuthenticatorModule::CAP_ANON_USER)
            {
                m_user_search_settings.allow_anon_user = true;
                break;
            }
        }
    }

    if (error)
    {
        authenticators.clear();
    }
    return authenticators;
}

/**
 * Parse the authenticator options string to config parameters object.
 *
 * @param opts Options string
 * @param params_out Config object to write to
 * @return True on success
 */
bool MySQLProtocolModule::parse_auth_options(const std::string& opts, mxs::ConfigParameters* params_out)
{
    bool error = false;
    auto opt_list = mxb::strtok(opts, ",");

    for (const auto& opt : opt_list)
    {
        auto equals_pos = opt.find('=');
        if (equals_pos != string::npos && equals_pos > 0 && opt.length() > equals_pos + 1)
        {
            string opt_name = opt.substr(0, equals_pos);
            mxb::trim(opt_name);
            string opt_value = opt.substr(equals_pos + 1);
            mxb::trim(opt_value);
            params_out->set(opt_name, opt_value);
        }
        else
        {
            MXB_ERROR("Invalid authenticator option setting: %s", opt.c_str());
            error = true;
            break;
        }
    }
    return !error;
}

namespace
{
int module_init()
{
    mxb_assert(!this_unit.pParser);

    int rv = 1;

    const auto& config = mxs::Config::get();

    mxs::ParserPlugin* pPlugin = mxs::ParserPlugin::load(DEFAULT_QC_NAME);

    if (pPlugin)
    {
        MXB_NOTICE("Classifier loaded.");

        if (pPlugin->setup(config.qc_sql_mode, ""))
        {
            auto& helper = MariaDBParser::Helper::get();

            this_unit.pParser = new MariaDBParser(pPlugin->create_parser(&helper));
            rv = 0;
        }
        else
        {
            mxs::ParserPlugin::unload(pPlugin);
        }
    }
    else
    {
        MXB_NOTICE("Could not load classifier.");
    }

    if (rv == 0)
    {
        rv = MariaDBClientConnection::module_init() ? 0 : 1;
    }

    return rv;
}

void module_finish()
{
    delete this_unit.pParser;
}
}

/**
 * Get MariaDBProtocol module info
 *
 * @return The module object
 */
MXS_MODULE* mariadbprotocol_info()
{
    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXB_MODULE_NAME,
        mxs::ModuleType::PROTOCOL,
        mxs::ModuleStatus::GA,
        MXS_PROTOCOL_VERSION,
        "The client to MaxScale MySQL protocol implementation",
        "V1.1.0",
        MXS_NO_MODULE_CAPABILITIES,
        &mxs::ProtocolApiGenerator<MySQLProtocolModule>::s_api,
        module_init,
        module_finish,
        nullptr,
        nullptr,
        &s_spec
    };

    return &info;
}
