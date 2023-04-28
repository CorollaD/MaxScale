/*
 * Copyright (c) 2023 MariaDB plc
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

#define MXB_MODULE_NAME "pp_pg_query"
#include <maxscale/ccdefs.hh>

#include <maxsimd/canonical.hh>
#include <maxscale/buffer.hh>
#include <maxscale/modinfo.hh>
#include <maxscale/parser.hh>
#include "../../protocol/Postgres/pgparser.hh"
#include <pg_query.h>
extern "C"
{
#include <src/pg_query_internal.h>
#include <catalog/pg_class.h>
#include <parser/parser.h>
}

using namespace std;
using mxs::Parser;
namespace sql = mxs::sql;

// For development
#define ASSERT_ON_NOT_HANDLED
#undef ASSERT_ON_NOT_HANDLED

#if defined(ASSERT_ON_NOT_HANDLED)
#define nhy_assert() mxb_assert(!true);
#else
#define nhy_assert()
#endif

namespace
{

int32_t module_thread_init(void);
void module_thread_finish(void);

/*
 * this_thread
 */
thread_local struct
{
    vector<const char*> markers;            // For use with maxsimd::get_canonical()
    uint32_t            options {0};
    uint64_t            server_version {0};
    Parser::SqlMode     sql_mode {Parser::SqlMode::DEFAULT}; // What sql_mode is used.
    uint64_t            version {0};
} this_thread;

/*
 * PgQueryInfo
 */
class PgQueryInfo : public GWBUF::ProtocolInfo
{
public:
    PgQueryInfo(string_view sql)
        : m_canonical(make_canonical(sql))
    {
    }

    PgQueryInfo(const PgQueryInfo&) = delete;
    PgQueryInfo& operator=(const PgQueryInfo&) = delete;

    static PgQueryInfo* get(const Parser::Helper& helper,
                                     const GWBUF& query,
                                     uint32_t collect)
    {
        PgQueryInfo* pInfo = nullptr;

        if (!is_query_parsed(query, collect))
        {
            parse_query(helper, query, collect);
        }

        return static_cast<PgQueryInfo*>(query.get_protocol_info().get());
    }

    size_t size() const override
    {
        return sizeof(*this);
    }

    void analyze(string_view sql, uint32_t collect)
    {
        mxb_assert(m_canonical == make_canonical(sql));
        mxb_assert(m_collected == 0 || (~m_collected & collect) != 0);

        m_collect = collect;

        // See libpg_query/src/pg_query_parser.c:pg_query_parse

        MemoryContext context = pg_query_enter_memory_context();

        PgQueryInternalParsetreeAndError result = pg_query_raw_parse(string{sql}.c_str());

        if (result.tree)
        {
            m_result = mxs::Parser::Result::PARSED;
            analyze(*result.tree);
        }
        else
        {
            if (result.error)
            {
#if defined(SS_DEBUG)
                int priority = LOG_WARNING;
#else
                int priority = LOG_INFO;
#endif
                MXB_LOG_MESSAGE(priority, "Parse error: '%s', SQL: %.*s",
                                result.error->message, (int)sql.length(), sql.data());

                pg_query_free_error(result.error);
            }
        }
        free(result.stderr_buffer);

        pg_query_exit_memory_context(context);

        m_collected |= collect;
    }

public:
    void analyze(const List& list)
    {
        switch (list.type)
        {
        case T_List:
            {
                for (int i = 0; i < list.length; ++i)
                {
                    analyze(*static_cast<Node*>(list.elements[i].ptr_value));
                }
            }
            break;

        default:
            nhy_assert();
        }
    }

    void analyze(const Node& x)
    {
        switch (x.type)
        {
            // Specific Information.
        case T_AlterTableStmt:
            analyze(reinterpret_cast<const AlterTableStmt&>(x));
            break;

        case T_CreateRoleStmt:
            analyze(reinterpret_cast<const CreateRoleStmt&>(x));
            break;

        case T_CreateStmt:
            analyze(reinterpret_cast<const CreateStmt&>(x));
            break;

        case T_DropStmt:
            analyze(reinterpret_cast<const DropStmt&>(x));
            break;

        case T_RawStmt:
            analyze(reinterpret_cast<const RawStmt&>(x));
            break;

        case T_SelectStmt:
            analyze(reinterpret_cast<const SelectStmt&>(x));
            break;

            // Generic Information.
        case T_AlterCollationStmt:
        case T_AlterDatabaseRefreshCollStmt:
        case T_AlterDatabaseSetStmt:
        case T_AlterDatabaseStmt:
        case T_AlterDefaultPrivilegesStmt:
        case T_AlterDomainStmt:
        case T_AlterEnumStmt:
        case T_AlterEventTrigStmt:
        case T_AlterExtensionContentsStmt:
        case T_AlterExtensionStmt:
        case T_AlterFdwStmt:
        case T_AlterForeignServerStmt:
        case T_AlterFunctionStmt:
        case T_AlterObjectDependsStmt:
        case T_AlterObjectSchemaStmt:
        case T_AlterOpFamilyStmt:
        case T_AlterOperatorStmt:
        case T_AlterOwnerStmt:
        case T_AlterPolicyStmt:
        case T_AlterPublicationStmt:
        case T_AlterRoleSetStmt:
        case T_AlterRoleStmt:
        case T_AlterSeqStmt:
        case T_AlterStatsStmt:
        case T_AlterSubscriptionStmt:
        case T_AlterSystemStmt:
        case T_AlterTSConfigurationStmt:
        case T_AlterTSDictionaryStmt:
        case T_AlterTableCmd:
        case T_AlterTableMoveAllStmt:
        case T_AlterTableSpaceOptionsStmt:
        case T_AlterTypeStmt:
        case T_AlterUserMappingStmt:
            m_type_mask |= sql::TYPE_WRITE;
            m_op = sql::OP_ALTER;
            break;

        case T_CompositeTypeStmt:
            m_type_mask |= sql::TYPE_WRITE;
            m_op = sql::OP_CREATE;
            break;

        case T_CreateSchemaStmt:
        case T_CreateTableSpaceStmt:
        case T_CreateExtensionStmt:
        case T_CreateFdwStmt:
        case T_CreateForeignServerStmt:
        case T_CreateForeignTableStmt:
        case T_CreateUserMappingStmt:
        case T_CreatePolicyStmt:
        case T_CreateAmStmt:
        case T_CreateTrigStmt:
        case T_CreateEventTrigStmt:
        case T_CreatePLangStmt:
        case T_CreateSeqStmt:
        case T_CreateDomainStmt:
        case T_CreateOpClassStmt:
        case T_CreateOpClassItem:
        case T_CreateOpFamilyStmt:
        case T_CreateStatsStmt:
        case T_CreateFunctionStmt:
        case T_CreateEnumStmt:
        case T_CreateRangeStmt:
        case T_CreatedbStmt:
        case T_CreateTableAsStmt:
        case T_CreateConversionStmt:
        case T_CreateCastStmt:
        case T_CreateTransformStmt:
        case T_CreatePublicationStmt:
        case T_CreateSubscriptionStmt:
        case T_CreateReplicationSlotCmd:
            m_type_mask |= sql::TYPE_WRITE;
            m_op = sql::OP_CREATE;
            break;

        case T_DefineStmt:
            m_type_mask |= sql::TYPE_WRITE;
            m_op = sql::OP_CREATE;
            break;

        case T_DropTableSpaceStmt:
        case T_DropUserMappingStmt:
        case T_DropRoleStmt:
        case T_DropdbStmt:
        case T_DropOwnedStmt:
        case T_DropSubscriptionStmt:
        case T_DropReplicationSlotCmd:
            m_type_mask |= sql::TYPE_WRITE;
            m_op = sql::OP_DROP;
            break;

        case T_IndexStmt:
            m_type_mask |= sql::TYPE_WRITE;
            m_op = sql::OP_CREATE;
            break;

        case T_RuleStmt:
            m_type_mask |= sql::TYPE_WRITE;
            m_op = sql::OP_CREATE;
            break;

        case T_ViewStmt:
            m_type_mask |= sql::TYPE_WRITE;
            m_op = sql::OP_CREATE;
            break;

        default:
            nhy_assert();
        }
    }

    void analyze(const AlterTableStmt& x)
    {
        m_type_mask |= sql::TYPE_WRITE;
        m_op = sql::OP_ALTER_TABLE;
    }

    void analyze(const CreateRoleStmt& x)
    {
        m_type_mask |= sql::TYPE_WRITE;
        m_op = sql::OP_CREATE;
    }

    void analyze(const CreateStmt& x)
    {
        m_type_mask |= sql::TYPE_WRITE;
        m_op = sql::OP_CREATE_TABLE;

        if (x.relation->relpersistence == RELPERSISTENCE_TEMP)
        {
            m_type_mask |= sql::TYPE_CREATE_TMP_TABLE;
        }
    }

    void analyze(const DropStmt& x)
    {
        m_type_mask |= sql::TYPE_WRITE;

        if (x.removeType == OBJECT_TABLE)
        {
            m_op = sql::OP_DROP_TABLE;
        }
        else
        {
            m_op = sql::OP_DROP;
        }
    }

    void analyze(const RawStmt& x)
    {
        analyze(*x.stmt);
    }

    void analyze(const SelectStmt& x)
    {
        m_type_mask |= sql::TYPE_READ;
        m_op = sql::OP_SELECT;
    }

    Parser::Result result() const
    {
        return m_result;
    }

    string_view get_canonical() const
    {
        return m_canonical;
    }

    string_view get_created_table_name() const
    {
        MXB_ERROR("Not implemented yet: %s", __func__);
        return string_view {};
    }

    Parser::DatabaseNames get_database_names() const
    {
        MXB_ERROR("Not implemented yet: %s", __func__);
        return Parser::DatabaseNames {};
    }

    void get_field_info(const Parser::FieldInfo** ppInfos, size_t* pnInfos) const
    {
        MXB_ERROR("Not implemented yet: %s", __func__);
        *ppInfos = nullptr;
        *pnInfos = 0;
    }

    void get_function_info(const Parser::FunctionInfo** ppInfos, size_t* pnInfos) const
    {
        MXB_ERROR("Not implemented yet: %s", __func__);
        *ppInfos = nullptr;
        *pnInfos = 0;
    }

    Parser::KillInfo get_kill_info() const
    {
        MXB_ERROR("Not implemented yet: %s", __func__);
        return Parser::KillInfo {};
    }

    mxs::sql::OpCode get_operation() const
    {
        return m_op;
    }

    GWBUF* get_preparable_stmt() const
    {
        MXB_ERROR("Not implemented yet: %s", __func__);
        return nullptr;
    }

    string_view get_prepare_name() const
    {
        MXB_ERROR("Not implemented yet: %s", __func__);
        return string_view {};
    }

    Parser::TableNames get_table_names() const
    {
        MXB_ERROR("Not implemented yet: %s", __func__);
        return Parser::TableNames {};
    }

    Parser::StmtResult get_stmt_result() const
    {
        return Parser::StmtResult { m_result, m_type_mask, m_op };
    }

    uint32_t get_trx_type_mask() const
    {
        MXB_ERROR("Not implemented yet: %s", __func__);
        return 0;
    }

    uint32_t get_type_mask() const
    {
        return m_type_mask;
    }

private:
    static string make_canonical(string_view sql)
    {
        string s(sql);

        maxsimd::get_canonical(&s, &this_thread.markers);

        return s;
    }

    static bool is_query_parsed(const GWBUF& query, uint32_t collect)
    {
        bool rv = false;

        auto* pInfo = static_cast<PgQueryInfo*>(query.get_protocol_info().get());

        if (pInfo)
        {
            if ((~pInfo->m_collected & collect) != 0)
            {
                // The statement has been parsed once, but the needed information
                // was not collected at that time.
            }
            else
            {
                rv = true;
            }
        }

        return rv;
    }

    static void parse_query(const Parser::Helper& helper, const GWBUF& query, uint32_t collect)
    {
        mxb_assert(!is_query_parsed(query, collect));

        string_view sql = helper.get_sql(query);

        PgQueryInfo* pInfo = static_cast<PgQueryInfo*>(query.get_protocol_info().get());
        if (pInfo)
        {
            mxb_assert((~pInfo->m_collect & collect) != 0);
            mxb_assert((~pInfo->m_collected & collect) != 0);

            // If we get here, then the statement has been parsed once, but
            // not all needed was collected. Now we turn on all blinkenlichts to
            // ensure that a statement is parsed at most twice.
            collect = Parser::COLLECT_ALL;
        }
        else
        {
            auto sInfo = make_unique<PgQueryInfo>(sql);
            pInfo = sInfo.get();

            const_cast<GWBUF&>(query).set_protocol_info(move(sInfo));
        }

        pInfo->analyze(sql, collect);
    }

    string         m_canonical;
    Parser::Result m_result    {Parser::Result::INVALID};
    uint32_t       m_type_mask {0};
    sql::OpCode    m_op        {sql::OP_UNDEFINED};
    int32_t        m_collected {0};
    int32_t        m_collect   {0};
};

/*
 * PgQueryParser
 */
class PgQueryParser : public mxs::Parser
{
public:
    using Info = PgQueryInfo;

    PgQueryParser(const mxs::ParserPlugin* pPlugin, const Helper* pHelper)
        : m_plugin(*pPlugin)
        , m_helper(*pHelper)
    {
    }

    const mxs::ParserPlugin& plugin() const override
    {
        return m_plugin;
    }

    const mxs::Parser::Helper& helper() const override
    {
        return m_helper;
    }

    Result parse(const GWBUF& query, uint32_t collect) const override
    {
        return get_info(query, collect)->result();
    }

    string_view get_canonical(const GWBUF& query) const override
    {
        return get_info(query)->get_canonical();
    }

    string_view get_created_table_name(const GWBUF& query) const override
    {
        return get_info(query, Parser::COLLECT_TABLES)->get_created_table_name();
    }

    mxs::Parser::DatabaseNames get_database_names(const GWBUF& query) const override
    {
        return get_info(query, Parser::COLLECT_DATABASES)->get_database_names();
    }

    void get_field_info(const GWBUF& query,
                        const FieldInfo** ppInfos,
                        size_t* pnInfos) const override
    {
        get_info(query, Parser::COLLECT_FIELDS)->get_field_info(ppInfos, pnInfos);
    }

    void get_function_info(const GWBUF& query,
                           const FunctionInfo** ppInfos,
                           size_t* pnInfos) const override
    {
        get_info(query, Parser::COLLECT_FUNCTIONS)->get_function_info(ppInfos, pnInfos);
    }

    mxs::Parser::KillInfo get_kill_info(const GWBUF& query) const override
    {
        return get_info(query)->get_kill_info();
    }

    mxs::sql::OpCode get_operation(const GWBUF& query) const override
    {
        return get_info(query)->get_operation();
    }

    uint32_t get_options() const override
    {
        return this_thread.options;
    }

    GWBUF* get_preparable_stmt(const GWBUF& query) const override
    {
        return get_info(query)->get_preparable_stmt();
    }

    string_view get_prepare_name(const GWBUF& query) const override
    {
        return get_info(query)->get_prepare_name();
    }

    uint64_t get_server_version() const override
    {
        return this_thread.version;
    }

    mxs::Parser::SqlMode get_sql_mode() const override
    {
        return this_thread.sql_mode;
    }

    mxs::Parser::TableNames get_table_names(const GWBUF& query) const override
    {
        return get_info(query, Parser::COLLECT_TABLES)->get_table_names();
    }

    uint32_t get_trx_type_mask(const GWBUF& query) const override
    {
        return get_info(query)->get_trx_type_mask();
    }

    uint32_t get_type_mask(const GWBUF& query) const override
    {
        return get_info(query)->get_type_mask();
    }

    bool set_options(uint32_t options) override
    {
        bool rv = false;

        if ((options & ~Parser::OPTION_MASK) == 0)
        {
            this_thread.options = options;
            rv = true;
        }
        else
        {
            mxb_assert(!true);
        }

        return rv;
    }

    void set_sql_mode(SqlMode sql_mode) override
    {
        switch (sql_mode)
        {
        case Parser::SqlMode::DEFAULT:
            this_thread.sql_mode = sql_mode;
            break;

        case Parser::SqlMode::ORACLE:
            this_thread.sql_mode = sql_mode;
            break;

        default:
            mxb_assert(!true);
        }
    }

    void set_server_version(uint64_t version) override
    {
        this_thread.server_version = version;
    }

private:
    PgQueryInfo* get_info(const GWBUF& query, uint32_t collect_extra = 0) const
    {
        uint32_t collect = Parser::COLLECT_ESSENTIALS | collect_extra;

        return PgQueryInfo::get(m_helper, query, collect);
    }

    const mxs::ParserPlugin&   m_plugin;
    const mxs::Parser::Helper& m_helper;
};

/*
 * PgQueryParserPlugin
 */
class PgQueryParserPlugin : public mxs::ParserPlugin
{
public:
    bool setup(Parser::SqlMode sql_mode, const char* args) override
    {
        return true;
    }

    bool thread_init(void) const override
    {
        return module_thread_init() == 0;
    }

    void thread_end(void) const override
    {
        module_thread_finish();
    }

    const Parser::Helper& default_helper() const override
    {
        return PgParser::Helper::get();
    }

    bool get_current_stmt(const char** ppStmt, size_t* pLen) const override
    {
        *ppStmt = nullptr;
        *pLen = 0;
        return false;
    }

    Parser::StmtResult get_stmt_result(const GWBUF::ProtocolInfo* pInfo) const override
    {
        return static_cast<const PgQueryInfo*>(pInfo)->get_stmt_result();
    }

    string_view get_canonical(const GWBUF::ProtocolInfo* pInfo) const override
    {
        return static_cast<const PgQueryInfo*>(pInfo)->get_canonical();
    }

    unique_ptr<Parser> create_parser(const Parser::Helper* pHelper) const override
    {
        return make_unique<PgQueryParser>(this, pHelper);
    }
};

/*
 * this_unit
 */
struct
{
    bool                initialized {false};
    PgQueryParserPlugin parser_plugin;
} this_unit;

int32_t module_process_init(void)
{
    mxb_assert(!this_unit.initialized);

    this_unit.initialized = true;

    return 0;
}

void module_process_finish(void)
{
    mxb_assert(this_unit.initialized);

    this_unit.initialized = false;
}

int32_t module_thread_init(void)
{
    return 0;
}

void module_thread_finish(void)
{
}

}

extern "C"
{
// To make it easy to get hold of the plugin when linking statically to the library.
mxs::ParserPlugin* mxs_get_parser_plugin()
{
    return &this_unit.parser_plugin;
}
}

/**
 * EXPORTS
 */

extern "C"
{

MXS_MODULE* MXS_CREATE_MODULE()
{

    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXB_MODULE_NAME,
        mxs::ModuleType::PARSER,
        mxs::ModuleStatus::GA,
        MXS_PARSER_VERSION,
        "Postgres SQL parser using libpg_query.",
        "V1.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &this_unit.parser_plugin,
        module_process_init,
        module_process_finish,
        module_thread_init,
        module_thread_finish,
    };

    return &info;
}
}
