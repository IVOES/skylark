/******************************************************************************
 * This file is part of Skylark project
 * Copyright ©2022 Hua andy <hua.andy@gmail.com>

 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * at your option any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *******************************************************************************/
#include "framework.h"

#define EXEC_VERSION 7
#define SKYLARK_SQLITE_BUSY_TIMEOUT 2000
#define START_TRANSACTION(db) sqlite3_exec(db, "begin transaction;", NULL, NULL, NULL)
#define END_TRANSACTION(db)   sqlite3_exec(db, "commit transaction;", NULL, NULL, NULL)

static CRITICAL_SECTION eu_sql_cs;
static CRITICAL_SECTION_DEBUG critsect_sqlite3 =
{
    0, 0, &eu_sql_cs,
    { &critsect_sqlite3.ProcessLocksList, &critsect_sqlite3.ProcessLocksList },
      0, 0, (DWORD_PTR)0x1,
};
static CRITICAL_SECTION eu_sql_cs = { &critsect_sqlite3, -1, 0, 0, 0, 0 };

static bool
sql_format_execute(const char *fmt, char *buf, int len)
{
    bool ret = false;
    char *pname = eu_utf16_utf8(__ORIGINAL_NAME, NULL);
    char *pver = eu_utf16_utf8(__EU_INFO_RELEASE_VERSION, NULL);
    do
    {
        if (!pname)
        {
            break;
        }
        if (!pver)
        {
            break;
        }
        int m = snprintf(buf, len, fmt, pname, pver, on_about_build_id());
        ret = (m > 0 && m < len);
    } while(0);
    eu_safe_free(pname);
    eu_safe_free(pver);
    return ret;
}

static uintptr_t
init_sql_file(const char *sql_path)
{
    int  rc = 0;
    char *msg = NULL;
    int m_table = 0;
    sqlite3 *db = NULL;
    const char *test = "SELECT szName FROM skylar_ver;";
    const char *sql[] = \
    {
        "create table file_recent(szId INTEGER PRIMARY KEY, szName char, szPos BIGINT, szDate BIGINT, UNIQUE(szName));",
        "create table file_remote(szId INTEGER PRIMARY KEY, szName char, szProtocol char, szAddress char, "
        "szPort SMALLINT, szArea SMALLINT, szUser char, szPass char, szPrivate char, szPassphrase char, UNIQUE(szName));",
        "create table find_his(szId INTEGER PRIMARY KEY, szName char, UNIQUE(szName));",
        "create table replace_his(szId INTEGER PRIMARY KEY, szName char, UNIQUE(szName));",
        "create table folder_his(szId INTEGER PRIMARY KEY, szName char, UNIQUE(szName));",
        "create table skylark_session(szTabId SMALLINT, szRealPath char, szBakPath char, szMark char, szFold char, szLine BIGINT, szCp INTEGER, szBakCp INTEGER, szEol SMALLINT, "
        "szBlank SMALLINT, szHex SMALLINT, szFocus SMALLINT,szZoom SMALLINT,szStatus SMALLINT, UNIQUE(szRealPath));",
        "create table skylar_ver(szName char, szVersion char, szBUildId BIGINT);",
        "insert or ignore into skylar_ver(szName,szVersion,szBUildId) values('%s', '%s', %I64u);",
        "create trigger delete_till_30 BEFORE INSERT ON file_recent WHEN (select count(*) from file_recent)>29 "
        "BEGIN "
        "DELETE FROM file_recent WHERE file_recent.szId IN (SELECT file_recent.szId FROM file_recent ORDER BY file_recent.szId limit (select count(*) -29 from file_recent )); "
        "END;",
        "create trigger delete_combo1_30 BEFORE INSERT ON find_his WHEN (select count(*) from find_his)>29 "
        "BEGIN "
        "DELETE FROM find_his WHERE find_his.szId IN (SELECT find_his.szId FROM find_his ORDER BY find_his.szId limit (select count(*) -29 from find_his )); "
        "END;",
        "create trigger delete_combo2_30 BEFORE INSERT ON replace_his WHEN (select count(*) from replace_his)>29 "
        "BEGIN "
        "DELETE FROM replace_his WHERE replace_his.szId IN (SELECT replace_his.szId FROM replace_his ORDER BY replace_his.szId limit (select count(*) -29 from replace_his )); "
        "END;",
        "create trigger delete_combo3_30 BEFORE INSERT ON folder_his WHEN (select count(*) from folder_his)>29 "
        "BEGIN "
        "DELETE FROM folder_his WHERE folder_his.szId IN (SELECT folder_his.szId FROM folder_his ORDER BY folder_his.szId limit (select count(*) -29 from folder_his )); "
        "END;",
        NULL
    };
    if ((rc = sqlite3_open(sql_path, &db)) != SQLITE_OK)
    {
        return 0;
    }
    if ((rc = sqlite3_busy_timeout(db, SKYLARK_SQLITE_BUSY_TIMEOUT)) != SQLITE_OK)
    {
        goto mem_clean;
    }
    // 测试表是否存在
    if ((rc = sqlite3_exec(db, test, NULL, NULL, NULL)) != SQLITE_OK)
    {
        START_TRANSACTION(db);
        for (int i = 0; sql[i]; ++i)
        {
            if (EXEC_VERSION == i)
            {
                char buffer[MAX_PATH+1] = {0};
                if (sql_format_execute(sql[i], buffer, MAX_PATH))
                {
                    rc = sqlite3_exec(db, buffer, 0, 0, &msg);
                }
            }
            else
            {
                rc = sqlite3_exec(db, sql[i], 0, 0, &msg);
            }
            if (rc != SQLITE_OK)
            {
                printf("create table, sql[%d] = [%s] error: %s\n", i, sql[i], msg);
                goto mem_clean;
            }
        }
        END_TRANSACTION(db);
    }
mem_clean:
    if (msg)
    {
        sqlite3_free(msg);
    }
    if (rc != SQLITE_OK)
    {
        sqlite3_close(db);
        db = NULL;
    }
    return (uintptr_t)db;
}

/**************************************************************************************
 * 多线程并发时, 开启用户线程锁, 不使用sqlite3本身的多线程模式, 它的内部实现太繁琐
 * 所以, 尽量使用子线程调用本函数, 防止在多线程模式下一些奇怪的死锁问题
 * sql参数, 为sql查询语句
 * callback参数, 为回调函数
 * data参数, 为回调函数参数
 * 函数成功返回0, 失败返回rc错误码, 返回-1时, 数据库打开错误.
 **************************************************************************************/
int
eu_sqlite3_send(const char *sql, sql3_callback callback, void *data)
{
    int rc = SQLITE_ERROR;
    TCHAR path[MAX_PATH] = {0};
    char *sql_path = NULL;
    _sntprintf(path, MAX_PATH-1, _T("%s\\conf\\skylark_prefs.sqlite3"), eu_module_path);
    if ((sql_path = eu_utf16_utf8(path, NULL)) != NULL)
    {
        EnterCriticalSection(&eu_sql_cs);
        uintptr_t db = 0;
        if ((db = init_sql_file(sql_path)) > 0)
        {
            char *err = NULL;
            if ((rc = sqlite3_exec((sqlite3 *)db, sql, callback, data, &err)))
            {
                if (err)
                {
                    printf("%s failed, cause: %s\n", __FUNCTION__, err);
                    sqlite3_free(err);
                }
            }
            sqlite3_close((sqlite3 *)db);
        }
        free(sql_path);
        LeaveCriticalSection(&eu_sql_cs);
    }
    return rc;
}

void
eu_push_find_history(const char *key)
{
    char sql[MAX_BUFFER+1] = {0};
    _snprintf(sql, MAX_BUFFER, "insert or ignore into find_his(szName) values('%s');", key);
    if (eu_sqlite3_send(sql, NULL, NULL) != 0)
    {
        printf("eu_sqlite3_send failed in %s\n", __FUNCTION__);
    }
}

void
eu_delete_find_history(const char *key)
{
    char sql[MAX_BUFFER+1] = {0};
    _snprintf(sql, MAX_BUFFER, "delete from find_his where szName='%s';", key);
    if (eu_sqlite3_send(sql, NULL, NULL) != 0)
    {
        printf("eu_sqlite3_send failed in %s\n", __FUNCTION__);
    }
}

static unsigned __stdcall
sql_search_history(void *lp)
{
    if (eu_sqlite3_send("SELECT szName FROM find_his;", (sql3_callback)lp, (void *)IDC_WHAT_FOLDER_CBO) != 0)
    {
        printf("eu_sqlite3_send failed in %s\n", __FUNCTION__);
    }
    return 0;
}

void
eu_get_find_history(sql3_callback pfunc)
{
    CloseHandle((HANDLE) _beginthreadex(NULL, 0, sql_search_history, pfunc, 0, NULL));
}

void
eu_push_replace_history(const char *key)
{
    char sql[MAX_BUFFER+1] = {0};
    _snprintf(sql, MAX_BUFFER, "insert or ignore into replace_his(szName) values('%s');", key);
    if (eu_sqlite3_send(sql, NULL, NULL) != 0)
    {
        printf("eu_sqlite3_send failed in %s\n", __FUNCTION__);
    }
}

void
eu_delete_replace_history(const char *key)
{
    char sql[MAX_BUFFER+1] = {0};
    _snprintf(sql, MAX_BUFFER, "delete from replace_his where szName='%s';", key);
    if (eu_sqlite3_send(sql, NULL, NULL) != 0)
    {
        printf("eu_sqlite3_send failed in %s\n", __FUNCTION__);
    }
}

static unsigned __stdcall
sql_replace_history(void *lp)
{
    if (eu_sqlite3_send("SELECT szName FROM replace_his;", (sql3_callback)lp, (void *)IDC_SEARCH_RP_CBO) != 0)
    {
        printf("eu_sqlite3_send failed in %s\n", __FUNCTION__);
    }
    return 0;
}

void
eu_get_replace_history(sql3_callback pfunc)
{
    CloseHandle((HANDLE) _beginthreadex(NULL, 0, sql_replace_history, pfunc, 0, NULL));
}

void
eu_push_folder_history(const char *key)
{
    char sql[MAX_BUFFER+1] = {0};
    _snprintf(sql, MAX_BUFFER, "insert or ignore into folder_his(szName) values('%s');", key);
    if (eu_sqlite3_send(sql, NULL, NULL) != 0)
    {
        printf("eu_sqlite3_send failed in %s\n", __FUNCTION__);
    }
}

void
eu_delete_folder_history(const char *key)
{
    char sql[MAX_BUFFER+1] = {0};
    _snprintf(sql, MAX_BUFFER, "delete from folder_his where szName='%s';", key);
    if (eu_sqlite3_send(sql, NULL, NULL) != 0)
    {
        printf("eu_sqlite3_send failed in %s\n", __FUNCTION__);
    }
}

static unsigned __stdcall
sql_folder_history(void *lp)
{
    if (eu_sqlite3_send("SELECT szName FROM folder_his;", (sql3_callback)lp, (void *)IDC_SEARCH_DIR_CBO) != 0)
    {
        printf("eu_sqlite3_send failed in %s\n", __FUNCTION__);
    }
    return 0;
}

void
eu_get_folder_history(sql3_callback pfunc)
{
    CloseHandle((HANDLE) _beginthreadex(NULL, 0, sql_folder_history, pfunc, 0, NULL));
}

void
eu_update_backup_table(file_backup *pbak)
{
    char rel_path[MAX_PATH+1] = {0};
    char bak_path[MAX_PATH+1] = {0};
    char sql[MAX_BUFFER*4] = {0};
    const char *fmt = "insert into skylark_session(szTabId,szRealPath,szBakPath,szMark,szFold,szLine,szCp,szBakCp,szEol,szBlank,szHex,szFocus,szZoom,szStatus) "
                      "values(%d, '%s', '%s', '%s', '%s', %I64d, %d, %d, %d, %d, %d, %d, %d, %d) on conflict (szRealPath) do update set "
                      "szTabId=%d,szBakPath='%s',szMark='%s',szFold='%s',szLine=%I64d,szCp=%d,szBakCp=%d,szEol=%d,szBlank=%d,szHex=%d,szFocus=%d,szZoom=%d,szStatus=%d;";
    WideCharToMultiByte(CP_UTF8, 0, pbak->rel_path, -1, rel_path, MAX_PATH, NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, pbak->bak_path, -1, bak_path, MAX_PATH, NULL, NULL);
    // 文件路径存在特殊符号时进行转义
    eu_str_replace(rel_path, MAX_PATH, "'", "''");
    eu_str_replace(bak_path, MAX_PATH, "'", "''");
    _snprintf(sql, MAX_BUFFER*4-1, fmt, pbak->tab_id, rel_path, bak_path, pbak->mark_id, pbak->fold_id, pbak->postion, pbak->cp, pbak->bakcp,
                  pbak->eol, pbak->blank, pbak->hex, pbak->focus, pbak->zoom, pbak->status, pbak->tab_id,bak_path,pbak->mark_id,pbak->fold_id,pbak->postion,
                  pbak->cp, pbak->bakcp,pbak->eol, pbak->blank, pbak->hex, pbak->focus, pbak->zoom, pbak->status);
    if (eu_sqlite3_send(sql, NULL, NULL) != 0)
    {
        printf("eu_sqlite3_send failed in %s\n", __FUNCTION__);
    }
}

void
eu_clear_backup_table(void)
{
    if (eu_sqlite3_send("delete from skylark_session;", NULL, NULL) != 0)
    {
        printf("eu_sqlite3_send failed in %s\n", __FUNCTION__);
    }
}

static unsigned __stdcall
on_sql_execute_thread(void *lp)
{
    char sql[MAX_BUFFER+1] = {0};
    _snprintf(sql, MAX_BUFFER, "delete from skylark_session where szRealPath='%s';", (char *)lp);
    if (eu_sqlite3_send(sql, NULL, NULL) != 0)
    {
        printf("eu_sqlite3_send failed in %s\n", __FUNCTION__);
    }
    free(lp);
    return 0;
}

void
on_sql_delete_backup_row(eu_tabpage *pnode)
{
    if (pnode && pnode->pathfile[0])
    {
        char *path = eu_utf16_utf8(pnode->pathfile, NULL);
        if (path)
        {
            on_sql_execute_thread(path);
        }
    }
}

void
on_sql_delete_backup_row_thread(eu_tabpage *pnode)
{
    if (pnode && pnode->pathfile[0])
    {
        char *path = eu_utf16_utf8(pnode->pathfile, NULL);
        if (path)
        {
            CloseHandle((HANDLE)_beginthreadex(NULL, 0, on_sql_execute_thread, (void *)path, 0, NULL));
        }
    }
}

/**************************************************************************************
 * 导出sqlite几个常用函数到euapi
 **************************************************************************************/
int
eu_sqlite3_open(const char *filename, sqlite3 **ppdb)
{
    return sqlite3_open(filename, ppdb);
}

int
eu_sqlite3_exec(sqlite3* db,  const char *sql, sql3_callback pfunc, void *para, char **errmsg)
{
    return sqlite3_exec(db,  sql, pfunc, para, errmsg);
}

int
eu_sqlite3_get_table(sqlite3 *db,const char *psql,char ***presult,int *prow,int *pcolumn,char **pzmsg)
{
    return sqlite3_get_table(db, psql, presult, prow, pcolumn, pzmsg);
}

void
eu_sqlite3_free_table(char **result)
{
    sqlite3_free_table(result);
}

void
eu_sqlite3_free(void *point)
{
    sqlite3_free(point);
}

int
eu_sqlite3_close(sqlite3 * db)
{
    return sqlite3_close(db);
}
