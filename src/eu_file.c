/*******************************************************************************
 * This file is part of Skylark project
 * Copyright ©2023 Hua andy <hua.andy@gmail.com>

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

#include <fcntl.h>
#include "framework.h"
#include <tlhelp32.h>

static volatile long file_close_id = 0;
static volatile long last_focus = -1;
static HANDLE file_event_final = NULL;

static bool
on_file_set_filter(const TCHAR *ext, TCHAR **pfilter)
{
    TCHAR p_all[] = {'A','l','l',' ','F','i','e','s','(','*','.','*',')','\0','*','.','*','\0','\0'};
    if (!(*pfilter = (TCHAR *)calloc(sizeof(TCHAR), MAX_BUFFER + 1)))
    {
        return false;
    }
    if (ext)
    {
        TCHAR *pext = NULL;
        TCHAR *pdesc = NULL;
        TCHAR exts[QW_SIZE+1] = {0};
        char u8_exts[QW_SIZE+1] = {0};
        _sntprintf(exts, QW_SIZE, _T("*%s;"), ext);
        WideCharToMultiByte(CP_UTF8, 0, exts, -1, u8_exts, QW_SIZE, NULL, NULL);
        for (doctype_t *mapper = eu_doc_get_ptr(); mapper && mapper->doc_type; ++mapper)
        {
            if (mapper->extname && strstr(mapper->extname, u8_exts) != NULL)
            {
                pext = eu_utf8_utf16(&mapper->extname[1], NULL);
                pdesc = eu_utf8_utf16(mapper->filedesc, NULL);
                if (_tcslen(pext) > 0 && pext[_tcslen(pext) - 1] == ';')
                {
                    pext[_tcslen(pext) - 1] = 0;
                }
                break;
            }
        }
        if (pext && pdesc)
        {
            int l = _sntprintf(*pfilter, MAX_BUFFER, _T("%s(%s)"), pdesc, pext);
            int ext_len = _tcslen(exts) > 0 ? (int)_tcslen(exts) - 1 : 0;
            exts[ext_len] = 0;   // strip ';'
            memcpy(*pfilter + l + 1, exts, ext_len * sizeof(TCHAR));
            memcpy(*pfilter + l + ext_len + 2, p_all, sizeof(p_all));
            free(pext);
            free(pdesc);
            return true;
        }
    }
    memcpy(*pfilter, p_all, sizeof(p_all));
    return true;
}

int
on_file_open_filename_dlg(HWND hwnd, TCHAR *file_name, int name_len)
{
    OPENFILENAME ofn = {sizeof(OPENFILENAME),};
    if (!file_name)
    {
        return EUE_POINT_NULL;
    }
    LOAD_I18N_RESSTR(IDS_TOOLBAR_1, lptext);
    *file_name = 0;
    ofn.hwndOwner = hwnd ? hwnd : eu_module_hwnd();
    ofn.hInstance = eu_module_handle();
    ofn.lpstrFile = file_name;
    ofn.nMaxFile = name_len;
    ofn.lpstrFilter = _T("All Files(*.*)\0*.*\0" \
                         "C/C++ Files(*.c;*.cpp;*.cxx;*.h;*.hpp;*.rc)\0*.c;*.cpp;*.cxx;*.h;*.hpp;*.rc\0" \
                         "Script Files(*.bat;*.cmd;*.sh;*.lua;*.pl;*.py)\0*.bat;*.cmd;*.sh;*.lua;*.pl;*.py\0" \
                         "HTML Files(*.htm;*.html;*.xml;*.css;*.js)\0*.htm;*.html;*.xml;*.css;*.js\0" \
                         "PE Files(*.exe;*.com;*.dll;*.sys)\0*.exe;*.com;*.dll;*.sys\0\0");
    ofn.lpstrTitle = lptext;
    ofn.Flags = OFN_ENABLESIZING | OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_HIDEREADONLY;
    return !GetOpenFileName(&ofn);
}

static int
on_file_get_filename_dlg(TCHAR *file_name, int name_len, const TCHAR *pcd)
{
    int err = SKYLARK_OK;
    TCHAR *filter = NULL;
    OPENFILENAME ofn = {sizeof(ofn),};
    if (!file_name ||!pcd)
    {
        return EUE_POINT_NULL;
    }
    LOAD_I18N_RESSTR(IDS_TOOLBAR_2, lptext);
    TCHAR *p = _tcsrchr(file_name, _T('.'));
    if (!on_file_set_filter(p, &filter) || !filter)
    {
        return EUE_EXT_FILTER_ERR;
    }
    ofn.hwndOwner = eu_module_hwnd();
    ofn.hInstance = eu_module_handle();
    ofn.lpstrFile = file_name;
    ofn.nMaxFile = name_len;
    ofn.lpstrFilter = filter;
    ofn.lpstrTitle = lptext;
    ofn.lpstrInitialDir = pcd;
    ofn.Flags = OFN_ENABLESIZING | OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT;
    err = !GetSaveFileName(&ofn);
    free(filter);
    // 重置默认工作目录
    SetCurrentDirectory(eu_module_path);
    return err;
}

static void
on_file_kill_tree(const uint32_t self)
{
    bool more;
    volatile int i = 1;
    static uint32_t edit_pid[MAX_SIZE] = {0};
    PROCESSENTRY32W pe32 = {sizeof(PROCESSENTRY32W)};
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        printf("CreateToolhelp32Snapshot (of processes) error %lu\n", GetLastError());
        return;
    }
    edit_pid[0] = self;
    more = Process32FirstW(snapshot,&pe32);
    while (more)
    {
        if (pe32.th32ParentProcessID > 4 && pe32.th32ProcessID != self && (wcscmp(__ORIGINAL_NAME, (LPCWSTR)pe32.szExeFile) == 0))
        {
            edit_pid[i++] = pe32.th32ProcessID;
            if (i >= MAX_SIZE)
            {
                break;
            }
        }
        more = Process32NextW(snapshot,&pe32);
    }
    CloseHandle(snapshot);
    if (edit_pid[1] > 0)
    {
        for (int i = 0; i < MAX_SIZE && edit_pid[i] > 0 ; ++i)
        {
            if (edit_pid[i] != edit_pid[0])
            {
                HANDLE handle = OpenProcess(PROCESS_TERMINATE, false, edit_pid[i]);
                if (handle)
                {
                    TerminateProcess(handle, (DWORD)-1);
                    CloseHandle(handle);
                    edit_pid[i] = 0;
                }
            }
        }
    }
}

void
on_file_update_time(eu_tabpage *pnode, time_t m)
{
    if (m)
    {
        pnode->st_mtime = m;
    }
    else if (pnode->pathfile[0])
    {
        struct _stat statbuf;
        _tstat(pnode->pathfile, &statbuf);
        pnode->st_mtime = statbuf.st_mtime;
    }
}

static int
on_file_refresh_recent_menu(void *data, int count, char **column, char **names)
{
    HMENU hre = (HMENU)data;
    const int index = GetMenuItemCount(hre);
    for (int i = 0; i < count && column[i]; ++i)
    {
        if (column[i][0] != 0)
        {
            TCHAR ptr_row[MAX_BUFFER] = {0};
            if (MultiByteToWideChar(CP_UTF8, 0, column[i], -1, ptr_row, MAX_BUFFER))
            {
                if (_tcsrchr(ptr_row, _T('&')))
                {
                    eu_wstr_replace(ptr_row, MAX_BUFFER, _T("&"), _T("&&"));
                }
                AppendMenu(hre, MF_POPUP | MF_STRING, IDM_HISTORY_BASE + index, ptr_row);
            }
        }
    }
    return SKYLARK_OK;
}

void
on_file_update_recent_menu(void)
{
    HWND hwnd = eu_module_hwnd();
    HMENU hroot = GetMenu(hwnd);
    if (hroot)
    {
        HMENU hre = NULL;
        HMENU hfile = GetSubMenu(hroot, 0);
        hre = hfile ? GetSubMenu(hfile, 2) : NULL;
        if (hre)
        {
            int count = GetMenuItemCount(hre);
            for (int index = 0; index < count; ++index)
            {
                DeleteMenu(hre, 0, MF_BYPOSITION);
            }
            if (on_sql_mem_post("SELECT szName FROM file_recent ORDER BY szDate DESC;", on_file_refresh_recent_menu, (void *)hre) != 0)
            {
                printf("on_sql_mem_post failed in %s\n", __FUNCTION__);
            }
            if ((count = GetMenuItemCount(hre)) == 0)
            {
                LOAD_I18N_RESSTR(IDC_MSG_HISTORY_ZERO, m_str);
                if (AppendMenu(hre, MF_POPUP | MF_STRING, IDM_HISTORY_BASE, m_str))
                {
                    util_enable_menu_item(hroot, IDM_HISTORY_BASE, false);
                }
            }
        }
    }
}

static void
on_file_push_recent(const eu_tabpage *pnode)
{
    file_recent *precent = (file_recent *)calloc(1, sizeof(file_recent));
    if (precent)
    {
        if (pnode && util_make_u8(pnode->pathfile, precent->path, MAX_BUFFER)[0])
        {   // 也支持16进制编辑器获取实时位置
            precent->postion = eu_sci_call((eu_tabpage *)pnode, SCI_GETCURRENTPOS, 0, 0);
            precent->hex = !(pnode->pmod && pnode->plugin) ? pnode->hex_mode : 0;
            on_sql_file_recent_thread(precent);
        }
        free(precent);
    }
}

void
on_file_clear_recent(void)
{
    HMENU hroot = GetMenu(eu_module_hwnd());
    if (hroot)
    {
        HMENU hre = NULL;
        HMENU hfile = GetSubMenu(hroot, 0);
        hre = hfile ? GetSubMenu(hfile, 2) : NULL;
        if (hre && (on_sql_file_recent_clear() == 0))
        {
            bool ret = false;
            int count = GetMenuItemCount(hre);
            for (int index = 0; index < count; ++index)
            {
                if (!(ret = DeleteMenu(hre, 0, MF_BYPOSITION)))
                {
                    break;
                }
            }
            if (ret)
            {
                LOAD_I18N_RESSTR(IDC_MSG_HISTORY_ZERO, m_str);
                if (AppendMenu(hre, MF_POPUP | MF_STRING, IDM_HISTORY_BASE, m_str))
                {
                    util_enable_menu_item(hroot, IDM_HISTORY_BASE, false);
                }
            }
        }
    }
}

/******************************************************************
 * _tsplitpath_s (MSVCRT for wine)
 *
 * Secure version of _tsplitpath
 */
static int
on_file_tsplitpath(const TCHAR *inpath, TCHAR *drive, size_t sz_drive, TCHAR *dir, size_t sz_dir, 
                   TCHAR *fname, size_t sz_fname, TCHAR *ext, size_t sz_ext)
{
    const TCHAR *p, *end;
    if (!inpath || (!drive && sz_drive) || (drive && !sz_drive) || (!dir && sz_dir) || (dir && !sz_dir) ||
        (!fname && sz_fname) || (fname && !sz_fname) || (!ext && sz_ext) || (ext && !sz_ext))
    {
        *_errno() = EINVAL;
        return EINVAL;
    }
    if (inpath[0] && inpath[1] == ':')
    {
        if (drive)
        {
            if (sz_drive <= 2)
            {
                goto do_error;
            }
            drive[0] = inpath[0];
            drive[1] = inpath[1];
            drive[2] = 0;
        }
        inpath += 2;
    }
    else if (drive)
    {
        drive[0] = '\0';
    }
    /* look for end of directory part */
    end = NULL;
    for (p = inpath; *p; p++)
    {
        if (*p == '/' || *p == '\\') end = p + 1;
    }
    if (end) /* got a directory */
    {
        if (dir)
        {
            if (sz_dir <= (size_t)(end - inpath))
            {
                goto do_error;
            }
            memcpy(dir, inpath, (end - inpath) * sizeof(wchar_t));
            dir[end - inpath] = 0;
        }
        inpath = end;
    }
    else if (dir)
    {
        dir[0] = 0;
    }
    /* look for extension: what's after the last dot */
    end = NULL;
    for (p = inpath; *p; p++)
    {
        if (*p == '.') end = p;
    }
    if (!end)
    {
        end = p; /* there's no extension */
    }
    if (fname)
    {
        if (sz_fname <= (size_t)(end - inpath))
        {
            goto do_error;
        }
        memcpy(fname, inpath, (end - inpath) * sizeof(wchar_t));
        fname[end - inpath] = 0;
    }
    if (ext)
    {
        if (sz_ext <= _tcslen(end))
        {
            goto do_error;
        }
        _tcscpy(ext, end);
    }
    return 0;
do_error:
    if (drive) drive[0] = '\0';
    if (dir) dir[0] = '\0';
    if (fname) fname[0] = '\0';
    if (ext) ext[0] = '\0';
    *_errno() = ERANGE;
    return ERANGE;
}

static void
on_file_splite_path(const TCHAR *full_path, TCHAR *pathname, TCHAR *filename, TCHAR *mainname, TCHAR *extname)
{
    TCHAR drv[_MAX_DRIVE];
    TCHAR ext[_MAX_EXT] = {0};
    TCHAR path[MAX_BUFFER] = {0};
    TCHAR part[MAX_PATH] = {0};
    TCHAR *ptr_part = mainname ? mainname : part;
    TCHAR *ptr_ext = extname ? extname : ext;
    on_file_tsplitpath(full_path, drv, _MAX_DRIVE, path, MAX_BUFFER, ptr_part, MAX_PATH, ptr_ext, _MAX_EXT);
    if (pathname)
    {
        *pathname = 0;
        if (_tcslen(drv) > 0 && _tcslen(path) > 0)
        {
            _sntprintf(pathname, MAX_BUFFER, _T("%s%s"), drv, path);
        }
    }
    if (filename)
    {
        *filename = 0;
        if (_tcslen(ptr_part) > 0 || _tcslen(ptr_ext) > 0)
        {
            _sntprintf(filename, MAX_PATH, _T("%s%s"), ptr_part, ptr_ext);
        }
    }
}

int
on_file_new(void)
{
    eu_tabpage *pnode = NULL;
    TCHAR filename[100] = {0};
    const uint8_t *bom_str = NULL;
    if ((pnode = (eu_tabpage *) calloc(1, sizeof(eu_tabpage))) == NULL)
    {
        return EUE_POINT_NULL;
    }
    else
    {
        pnode->is_blank = true;
        pnode->begin_pos = -1;
    }
    if (on_tabpage_generator(filename, 100)[0])
    {
        _tcscpy(pnode->pathfile, filename);
        _tcscpy(pnode->filename, filename);
    }
    if (on_tabpage_add(pnode))
    {
        eu_safe_free(pnode);
        return EUE_INSERT_TAB_FAIL;
    }
    else
    {
        on_sci_before_file(pnode);
        eu_sci_call(pnode, SCI_CLEARALL, 0, 0);
        pnode->eol = eu_get_config()->new_file_eol;
        pnode->codepage = eu_get_config()->new_file_enc;
    }
    switch (pnode->codepage)
    {
        case IDM_UNI_UTF8B:
            bom_str = (const uint8_t *) "\xEF\xBB\xBF";
            break;
        case IDM_UNI_UTF16LEB:
            bom_str = (const uint8_t *) "\xFF\xFE";
            break;
        case IDM_UNI_UTF16BEB:
            bom_str = (const uint8_t *) "\xFE\xFF";
            break;
        default:
            break;
    }
    if (bom_str)
    {
        on_encoding_set_bom(bom_str, pnode);
    }
    if (true)
    {
        on_file_update_time(pnode, time(NULL));
        on_sci_after_file(pnode);
        on_tabpage_selection(pnode, -1);
    }
    return SKYLARK_OK;
}

uint64_t
on_file_get_avail_phys(void)
{
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);
    GlobalMemoryStatusEx(&statex);
    return (statex.ullAvailPhys);
}

static bool
on_file_map_hex(eu_tabpage *pnode, HANDLE hfile, size_t nbyte)
{
    if (!pnode || pnode->phex)
    {
        return false;
    }
    // 如果是二进制文件, 使用内存映射
    pnode->phex = (PHEXVIEW) calloc(1, sizeof(HEXVIEW));
    if (!pnode->phex)
    {
        return false;
    }
    // PAGE_READWRITE, write immediately
    pnode->phex->hmap = share_create(hfile, PAGE_WRITECOPY, nbyte, NULL);
    if (pnode->phex->hmap == NULL)
    {
        printf("share_create failed, cause %lu\n", GetLastError());
        eu_safe_free(pnode->phex);
        return false;
    }
    // 映射全部文件
    pnode->phex->pbase = (uint8_t *)share_map(pnode->phex->hmap, nbyte, FILE_MAP_COPY);
    if (pnode->phex->pbase == NULL)
    {
        printf("share_map failed, cause %lu\n", GetLastError());
        share_close(pnode->phex->hmap);
        eu_safe_free(pnode->phex);
        return false;
    }
    pnode->phex->ex_style |= (pnode->file_attr & FILE_ATTRIBUTE_READONLY) ? HVS_READONLY : 0;
    eu_close_handle(pnode->phex->hmap);
    return true;
}

static int
on_file_set_codepage(eu_tabpage *pnode, const HANDLE hfile, const TCHAR *pfull)
{
    int check_len = 0;
    int bytesread = 0;
    int err = SKYLARK_OK;
    uint8_t *buf = NULL;
    check_len = eu_int_cast(pnode->raw_size > BUFF_SIZE ? BUFF_SIZE : pnode->raw_size);
    do
    {
        if (!(buf = (uint8_t *)calloc(1, check_len+1)))
        {
            err = EUE_NOT_ENOUGH_MEMORY;
            break;
        }
        if (!ReadFile(hfile, buf, check_len, &bytesread, NULL))
        {
            MSG_BOX_ERR(IDC_MSG_OPEN_FAIL, IDC_MSG_ERROR, MB_ICONERROR | MB_OK);
            err = EUE_API_READ_FILE_ERR;
            break;
        }
        if (bytesread < 1)
        {
            // 为空的新文件, 设为默认模式
            pnode->eol = eu_get_config()->new_file_eol;
            pnode->codepage = eu_get_config()->new_file_enc;
        }
        else // 如果是utf8编码, 获取换行符, 否则, 等编码转换后再获取换行符
        {
            pnode->codepage = eu_try_encoding(buf, bytesread, false, pfull);
            if (pnode->codepage < IDM_UNI_UTF16LE)
            {
                pnode->eol = on_encoding_line_mode((const char *)buf, bytesread);
            }
            if (pnode->codepage != IDM_OTHER_BIN)
            {
                on_encoding_set_bom((const uint8_t *) buf, pnode);
            }
        }
    } while(0);
    eu_safe_free(buf);
    return err;
}

static size_t
on_file_header_parser(void *hdr, size_t size, size_t nmemb, void *userdata)
{
    const size_t cb = size * nmemb;
    if (hdr && userdata)
    {
        char *p = NULL;
        char *u8_file = ((char (*)[MAX_PATH])userdata)[0];
        if (u8_file[0] && (p = strstr(hdr, u8_file)) && (p[strlen(p) - 1] == '\r' || p[strlen(p) - 1] == '\n'))
        {
            printf("we do util_split, hdr = [%s]\n", (const char*)hdr);
            util_split(hdr, userdata, ' ');
            return 0;
        }
    }
    return cb;
}

static void
on_file_attr_parser(eu_tabpage *pnode, char (*pstr)[MAX_PATH])
{
    if (pnode && pstr)
    {
        for (int i = 0; i < TWO_DISM; ++i)
        {   // 第四列, 是文件尺寸
            if (i == 4 && pstr[i][0])
            {   // 预先获取文件大小
                pnode->raw_size = _atoi64(pstr[i]);
                printf("%s, %I64u\n", __FUNCTION__, pnode->raw_size);
            }
        }
    }
}

static int
on_file_remote_lenght(eu_tabpage *pnode, const wchar_t *path)
{
    char *url = NULL;
    CURL *curl = NULL;
    CURLcode res = EUE_CURL_INIT_FAIL;
    char data[TWO_DISM][MAX_PATH] = {0};
    remotefs *pserver = on_remote_list_find(path);
    if (!pserver)
    {
        return res;
    }
    if ((url = eu_utf16_utf8(path, NULL)) == NULL)
    {
        return res;
    }
    do
    {
        char *p = NULL;
        if ((p = strrchr(url, '/')) && strlen(p) > 1)
        {
            p[1] = 0;
        }
        if (!(curl = on_remote_init_socket(url, pserver)))
        {
            break;
        }
        // INOUT, 传送一个文件名, 返回一个数组指针
        data[0][0] = ' ';
        util_make_u8(pnode->filename, &data[0][1], MAX_PATH - 2);
        eu_curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        eu_curl_easy_setopt(curl, CURLOPT_TIMEOUT, 8L);
        eu_curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "sftp");
        eu_curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_file_header_parser);
        eu_curl_easy_setopt(curl, CURLOPT_WRITEDATA, data);
        if ((res = eu_curl_easy_perform(curl)) == CURLE_OK || res == CURLE_WRITE_ERROR)
        {
            res = CURLE_OK;
            on_file_attr_parser(pnode, data);
        }
        else
        {
            const char *err_string = eu_curl_easy_strerror(res);
            printf("%s error[%d]: %s\n", __FUNCTION__, res, err_string);
            pnode->raw_size = 0;
        }
    } while(0);
    if (curl)
    {
        eu_curl_easy_cleanup(curl);
    }
    free(url);
    return res;
}

static int
on_file_preload(eu_tabpage *pnode, file_backup *pbak)
{
    int err = SKYLARK_OK;
    HANDLE hfile = NULL;
    const TCHAR *pfull = (const TCHAR *)(pbak ? pbak->rel_path : NULL);
    if (!pnode || !pbak)
    {
        return EUE_TAB_NULL;
    }
    if (pbak->status)
    {
        pfull = pbak->bak_path;
    }
    if (STR_IS_NUL(pfull))
    {
        return EUE_PATH_NULL;
    }
    if (pbak->hex)
    {
        pnode->hex_mode = true;
    }
    if (url_has_remote(pfull))
    {
        return on_file_remote_lenght(pnode, pfull);
    }
    else if ((pnode->file_attr = GetFileAttributes(pfull)) == INVALID_FILE_ATTRIBUTES)
    {
        return EUE_FILE_ATTR_ERR;
    }
    if (!share_open_file(pfull, true, OPEN_EXISTING, &hfile))
    {
        return EUE_API_OPEN_FILE_ERR;
    }
    if (!util_file_size(hfile, &pnode->raw_size))
    {
        err = EUE_FILE_SIZE_ERR;
        goto pre_clean;
    }
    if (pbak->cp)
    {   // 存在备份,不再测试编码
        pnode->eol = pbak->eol;
        pnode->nc_pos = pbak->postion;
        pnode->codepage = pbak->cp;
        if (!TAB_NOT_BIN(pnode))
        {
            pnode->hex_mode = true;
            if (!on_file_map_hex(pnode, hfile, 0))
            {
                err = EUE_MAP_HEX_ERR;
            }
            goto pre_clean;
        }
    }
    if (pnode->hex_mode)
    {
        if ((err = !on_file_map_hex(pnode, hfile, 0)) != 0)
        {
            goto pre_clean;
        }
        if (pbak->cp && (pbak->bakcp == pbak->cp || !pbak->status))
        {
            pnode->phex->hex_ascii = true;
        }
        else if (pbak->cp)
        {
            on_encoding_set_bom_from_cp(pnode);
        }
        else
        {
            pnode->phex->hex_ascii = true;
            err = on_file_set_codepage(pnode, hfile, pfull);
        }
        goto pre_clean;
    }
    if (pnode->pmod)
    {
        err = SKYLARK_OK;
        goto pre_clean;
    }
    if (pbak->cp)
    {
        on_encoding_set_bom_from_cp(pnode);
        goto pre_clean;
    }
    if ((err = on_file_set_codepage(pnode, hfile, pfull)) == SKYLARK_OK && !TAB_NOT_BIN(pnode))
    {   // 不在备份中打开, 测试是否16进制文件?
        pnode->hex_mode = true;
        if (!on_file_map_hex(pnode, hfile, 0))
        {
            err = EUE_MAP_HEX_ERR;
        }
    }
pre_clean:
    share_close(hfile);
    return err;
}

static int
on_file_load_plugins(eu_tabpage *pnode, bool route_open)
{
    int ret = np_plugins_initialize(pnode->pmod, &pnode->plugin);
    if (ret == NP_NO_ERROR && pnode->plugin)
    {
        printf("np_plugins_initialize ok!\n");
        ret = pnode->plugin->funcs.newp(&pnode->plugin->npp, NULL);
        if (ret == 0)
        {
            pnode->plugin->win.window = pnode->hwnd_sc;
            pnode->plugin->funcs.setwindow(&pnode->plugin->npp, &pnode->plugin->win);
            if (!route_open)
            {
                char u8_file[MAX_BUFFER] = {0};
                util_make_u8(pnode->bakpath[0] ? pnode->bakpath : pnode->pathfile, u8_file, MAX_BUFFER);
                pnode->plugin->stream.url = (const char *)u8_file;
                pnode->plugin->stream.end = (intptr_t)pnode->raw_size;
                ret = pnode->plugin->funcs.asfile(&pnode->plugin->npp, &pnode->plugin->stream);
                if (!ret)
                {
                    pnode->hex_mode = true;
                }
            }
            else
            {
                uint16_t type;
                pnode->plugin->stream.end = (intptr_t)pnode->raw_size;
                ret = pnode->plugin->funcs.newstream(&pnode->plugin->npp, &pnode->plugin->stream, false, &type);
                if (!ret)
                {
                    pnode->plugin->funcs.writeready(&pnode->plugin->npp, &pnode->plugin->stream);
                }
            }
        }
    }
    return ret;
}

int
on_file_load(eu_tabpage *pnode, file_backup *pbak, const bool force)
{
    size_t len = 0;
    size_t buf_len = 0;
    size_t err = SKYLARK_OK;
    bool is_utf8 = false;
    TCHAR *pfull = NULL;
    npn_stream uf_stream = {0};
    if (!pnode)
    {
        return EUE_TAB_NULL;
    }
    if (pnode->hex_mode)
    {
        return SKYLARK_OK;
    }
    if (pnode->pmod)
    {
        return on_file_load_plugins(pnode, false);
    }
    pfull = pnode->pathfile;
    if (pbak && pbak->status)
    {
        pfull = pbak->bak_path;
        is_utf8 = true;
    }
    if (STR_IS_NUL(pfull))
    {
        return EUE_PATH_NULL;
    }
    if (!force)
    {
        uf_stream.size = (size_t)pnode->raw_size;
    }
    if (!util_open_file(pfull, &uf_stream))
    {
        MSG_BOX_ERR(IDC_MSG_OPEN_FAIL, IDC_MSG_ERROR, MB_ICONERROR | MB_OK);
        return EUE_OPEN_FILE_ERR;
    }
    if (force)
    {
        // reassign variables
        pnode->raw_size = uf_stream.size;
    }
    if (is_utf8)
    {
        len = uf_stream.size;
    }
    else
    {
        len = uf_stream.size - pnode->pre_len;
    }
    if (!is_utf8 && pnode->codepage > IDM_UNI_UTF8B && pnode->codepage < IDM_OTHER_BIN)
    {
        char *pdst = NULL;
        size_t dst_len = 0;
        euconv_t evd = {0};
        evd.src_from = eu_query_encoding_name(pnode->codepage);
        evd.dst_to = "utf-8";
        err = on_encoding_do_iconv(&evd, (char *) (uf_stream.base+pnode->pre_len), &len, &pdst, &dst_len);
        if (err == (size_t) -1)
        {
            printf("on_encoding_do_iconv error in %s\n", __FUNCTION__);
            err = EUE_ICONV_FAIL;
            MSG_BOX(IDC_MSG_ICONV_FAIL2, IDC_MSG_ERROR, MB_ICONERROR | MB_OK);
        }
        if (pdst)
        {
            if (pnode->eol < 0)
            {
                pnode->eol = on_encoding_line_mode(pdst, dst_len);
            }
            eu_sci_call(pnode, SCI_ADDTEXT, dst_len, (LPARAM)(pdst));
            eu_safe_free(pdst);
        }
    }
    else if (is_utf8)
    {
        eu_sci_call(pnode, SCI_ADDTEXT, len, (LPARAM)(uf_stream.base));
    }
    else
    {
        eu_sci_call(pnode, SCI_ADDTEXT, len, (LPARAM)(uf_stream.base+pnode->pre_len));
    }
    uf_stream.close(&uf_stream);
    return (int)err;
}

/**************************************************************************************
 * 文件是否打开已经打开
 * 参数selection代表是否选中新打开的标签
 * 如果已经打开在标签上, 返回标签号, 参数selection为true, 则返回SKYLARK_OPENED(-1)
 * 否则, 返回SKYLARK_NOT_OPENED(-2)
 **************************************************************************************/
static int
on_file_open_if(const TCHAR *pfile, bool selection)
{
    int res = SKYLARK_NOT_OPENED;
    eu_tabpage *pnode = NULL;
    if (!pfile)
    {
        return EUE_PATH_NULL;
    }
    for (int index = 0, count = TabCtrl_GetItemCount(g_tabpages); index < count; ++index)
    {
        TCITEM tci = {TCIF_PARAM};
        if (!TabCtrl_GetItem(g_tabpages, index, &tci))
        {
            printf("TabCtrl_GetItem return failed on %s:%d\n", __FILE__, __LINE__);
            res = SKYLARK_TABCTRL_ERR;
            break;
        }
        if ((pnode = (eu_tabpage *) (tci.lParam)))
        {
            if (_tcscmp(pnode->pathfile, pfile) == 0)
            {
                res = selection ? on_tabpage_selection(pnode, index) : SKYLARK_OPENED;
                break;
            }
            else if (!url_has_remote(pfile) && _tcsrchr(pfile, _T('/')))
            {
                TCHAR temp[MAX_BUFFER] = {0};
                _sntprintf(temp, MAX_BUFFER, _T("%s"), pfile);
                eu_wstr_replace(temp, MAX_BUFFER, _T("/"), _T("\\"));
                if (_tcscmp(pnode->pathfile, temp) == 0)
                {
                    res = selection ? on_tabpage_selection(pnode, index) : SKYLARK_OPENED;
                    break;
                }
            }
        }
    }
    return res;
}

static void
on_file_active_other(int index)
{
    int count = TabCtrl_GetItemCount(g_tabpages);
    if (count <= 0)
    {   // 最后一个标签
        if (!eu_get_config()->m_exit)
        {
            file_backup bak = {0};
            share_send_msg(&bak);
        }
        else if (on_sql_sync_session() == SKYLARK_OK)
        {
            printf("close last tab, skylark exit ...\n");
            SendMessage(eu_module_hwnd(), WM_BACKUP_OVER, 0, 0);
        }
        return;
    }
    switch (eu_get_config()->m_tab_active)
    {   // 激活另一个标签
        case IDM_VIEW_LEFT_TAB:
            on_tabpage_select_index(index > 0 ? index - 1 : count - 1);
            break;
        case IDM_VIEW_RIGHT_TAB:
            on_tabpage_select_index(index > count - 1 ? 0 : index);
            break;
        case IDM_VIEW_FAR_LEFT_TAB:
            on_tabpage_select_index(0);
            break;
        case IDM_VIEW_FAR_RIGHT_TAB:
            on_tabpage_select_index(count - 1);
            break;
        default:
            break;
    }
}

static void
on_file_update_postion(eu_tabpage *pnode, file_backup *pbak)
{
    if (pnode && pbak)
    {
        if (pbak->x < 0 && pbak->y > 0)
        {
            pnode->nc_pos = pbak->y;
        }
        else if (pbak->hex)
        {
            if (pbak->postion > 0)
            {
                pnode->nc_pos = pbak->postion;
            }
            else
            {
                if (pbak->x >= 0)
                {
                    pnode->nc_pos = (pbak->x > 0 ? pbak->x - 1 : pbak->x) * 16;
                }
                if (pbak->y >= 0)
                {
                    pnode->nc_pos += pbak->y % 16;
                }
            }
        }
        else
        {
            sptr_t pos = eu_sci_call(pnode, SCI_POSITIONFROMLINE, pbak->x > 0 ? pbak->x - 1 : 0, 0);
            sptr_t line_end_pos = eu_sci_call(pnode, SCI_GETLINEENDPOSITION, pbak->x > 0 ? pbak->x - 1 : 0, 0);
            pos += (pbak->y > 0 ? pbak->y - 1 : 0);
            if (pos > line_end_pos)
            {
                pos = line_end_pos;
            }
            if (pos > 0)
            {
                pnode->nc_pos = pos;
            }
            else if (pbak->postion > 0)
            {
                pnode->nc_pos = pbak->postion;
            }
            else if (pnode->nc_pos < 0)
            {
                pnode->nc_pos = 0;
            }
        }
    }
}

static void
on_file_update_focus(eu_tabpage *pnode, file_backup *pbak)
{
    if (pbak && pbak->focus)
    {
        if (pbak->focus > 0)
        {
            _InterlockedExchange(&last_focus, on_tabpage_get_index(pnode));
        }
    }
    else
    {
        _InterlockedExchange(&last_focus, pnode->tab_id);
    }
}

static void
on_file_before_open(eu_tabpage *pnode)
{
    if (!pnode->hex_mode && !pnode->pmod)
    {
        on_sci_before_file(pnode);
        eu_sci_call(pnode, SCI_CLEARALL, 0, 0);
        // 把工作目录设置在进程所在地
        util_set_working_dir(eu_module_path, NULL);
    }
}

static int
on_file_after_open(eu_tabpage *pnode, file_backup *pbak)
{
    int mtab = 0;
    on_sci_after_file(pnode);
    on_file_update_focus(pnode, pbak);
    if (!pnode->plugin)
    {
        on_file_update_postion(pnode, pbak);
        on_search_add_navigate_list(pnode, pnode->nc_pos);
    }
    if ((mtab = on_tabpage_selection(pnode, -1)) < 0)
    {
        return SKYLARK_TABCTRL_ERR;
    }
    if (!pnode->hex_mode && !pnode->pmod)
    {
        if (strlen(pbak->mark_id) > 0)
        {   // 恢复书签
            on_search_update_mark(pnode, pbak->mark_id);
        }
        if (strlen(pbak->fold_id) > 0)
        {   // 恢复折叠
            on_search_update_fold(pnode, pbak->fold_id);
        }
    }
    if (pnode->nc_pos >= 0)
    {
        on_search_jmp_pos(pnode);
    }
    if (pnode->be_modify)
    {
        on_tabpage_editor_modify(pnode, "X");
    }
    if (!pnode->is_blank)
    {
        on_file_push_recent(pnode);
    }
    if (last_focus == (intptr_t)mtab)
    {
        return mtab;
    }
    return on_tabpage_selection(pnode, last_focus);
}

static time_t
on_file_max_date(file_backup *pbak)
{
    if (pbak)
    {
        struct _stat rel_t = {0};
        struct _stat bak_t = {0};
        if (STR_NOT_NUL(pbak->rel_path))
        {
            _tstat(pbak->rel_path, &rel_t);
        }
        if (STR_NOT_NUL(pbak->bak_path))
        {
            _tstat(pbak->bak_path, &bak_t);
        }
        if (rel_t.st_mtime > bak_t.st_mtime)
        {
            pbak->status = 0;
        }
        return rel_t.st_mtime > 0 ? rel_t.st_mtime : bak_t.st_mtime;
    }
    return 0;
}

static int
on_file_node_initialize(eu_tabpage **p, file_backup *pbak)
{
    if (p && pbak)
    {
        if (*p)
        {
            printf("Waning: node != NULL (should be NULL)\n");
        }
        if ((*p = (eu_tabpage *) calloc(1, sizeof(eu_tabpage))) == NULL)
        {
            return EUE_OUT_OF_MEMORY;
        }
        (*p)->eol = -1;
        (*p)->begin_pos = -1;
        (*p)->hex_mode = !!pbak->hex;
        (*p)->is_blank = pbak->blank;
        (*p)->be_modify = !!pbak->status;
        if (STR_NOT_NUL(pbak->rel_path))
        {   // 有可能是远程文件
            if (url_has_remote(pbak->rel_path))
            {
                remotefs *pserver = on_remote_list_find(pbak->rel_path);
                if (pserver)
                {
                    memcpy(&((*p)->fs_server), pserver, sizeof(remotefs));
                }
                _tsplitpath(pbak->rel_path, NULL, NULL, (*p)->filename, (*p)->extname);
                if (_tcslen((*p)->extname) > 0)
                {
                    _tcsncat((*p)->filename, (*p)->extname, MAX_PATH);
                }
                _tcsncpy((*p)->pathfile, pbak->rel_path, MAX_BUFFER);
            }
            else
            {
                on_file_splite_path(pbak->rel_path, (*p)->pathname, (*p)->filename, NULL, (*p)->extname);
                _tcsncpy((*p)->pathfile, pbak->rel_path, MAX_BUFFER);
            }
        }
        if (STR_NOT_NUL(pbak->bak_path))
        {
            _tcsncpy((*p)->bakpath, pbak->bak_path, MAX_BUFFER);
        }
        if (!(*p)->is_blank)
        {
            (*p)->zoom_level = pbak->zoom;
            on_file_update_time((*p), on_file_max_date(pbak));
        }
        if ((*p)->extname[0])
        {
            np_plugins_lookup(NPP_PDFVIEW, (*p)->extname, &(*p)->pmod);
        }
        return SKYLARK_OK;
    }
    return EUE_POINT_NULL;
}

/**************************************************************************************
 * 文件打开函数, 参数selection只影响返回值, 一般设为true
 * 成功打开文件, 确保返回的是当前标签的序号
 * 如果selection为false, 且文件已经打开的情况下, 返回值是SKYLARK_OPENED(-1)
 **************************************************************************************/
int
on_file_only_open(file_backup *pbak, const bool selection)
{
    eu_tabpage *pnode = NULL;
    int res = on_file_open_if(pbak->rel_path, selection);
    if (res < SKYLARK_NOT_OPENED  || res == SKYLARK_OPENED)
    {
        return res;
    }
    if (res >= SKYLARK_OK && (pnode = on_tabpage_get_ptr(res)) != NULL)
    {   // 如果文件已经打开, 根据新参数更新插入符位置
        on_file_update_postion(pnode, pbak);
        if (pnode->nc_pos >= 0)
        {
            printf("we jump to %zd\n", pnode->nc_pos);
            on_search_jmp_pos(pnode);
        }
        if (!pnode->hex_mode)
        {
            res = pbak && pbak->hex ? hexview_switch_mode(pnode) : res;
        }
        else if (pbak && !pbak->hex && !eu_get_config()->m_instance)
        {
            res = hexview_switch_mode(pnode);
        }
        return res;
    }
    if ((res = on_file_node_initialize(&pnode, pbak)) != SKYLARK_OK)
    {
        return res;
    }
    if ((res = on_file_preload(pnode, pbak)) != SKYLARK_OK)
    {
        eu_safe_free(pnode);
        printf("on_file_preload failed, err = %d\n", res);
        return res;
    }
    if ((res = on_tabpage_add(pnode)) != SKYLARK_OK)
    {
        eu_safe_free(pnode);
        printf("on_tabpage_add failed, err = %d\n", res);
        return res;
    }
    on_file_before_open(pnode);
    if (on_file_load(pnode, pbak, false))
    {
        int index = on_tabpage_remove(&pnode);
        on_file_active_other(index);
        return EUE_WRITE_TAB_FAIL;
    }
    res = on_file_after_open(pnode, pbak);
    return res;
}

static int
on_file_open_bakcup(file_backup *pbak)
{
    if (!pbak || (STR_IS_NUL(pbak->rel_path)))
    {
        return on_file_new();
    }
    if (_tcslen(pbak->rel_path) > 0 && pbak->rel_path[_tcslen(pbak->rel_path) - 1] == _T('*'))
    {
        HANDLE hfile = NULL;
        WIN32_FIND_DATA st_file = {0};
        TCHAR base_path[MAX_BUFFER] = {0};
        _tcsncpy(base_path, pbak->rel_path, MAX_BUFFER);
        if (*base_path != 0)
        {
            if (_tcsrchr(base_path, _T('\\')))
            {
                _tcsrchr(base_path, _T('\\'))[0] = 0;
            }
        }
        if ((hfile = FindFirstFile(pbak->rel_path, &st_file)) == INVALID_HANDLE_VALUE)
        {
            MSG_BOX(IDC_MSG_OPEN_ERR1, IDC_MSG_ERROR, MB_ICONERROR|MB_OK);
            return EUE_FILE_ATTR_ERR;
        }
        do
        {
            if (_tcscmp(st_file.cFileName, _T(".")) == 0 || _tcscmp(st_file.cFileName, _T("..")) == 0)
            {
                continue;
            }
            if (!(st_file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                file_backup bak = {0};
                _sntprintf(bak.rel_path, MAX_BUFFER, _T("%s\\%s"), base_path, st_file.cFileName);
                on_file_only_open(&bak, true);
            }
        } while (FindNextFile(hfile, &st_file));
        FindClose(hfile);
        return SKYLARK_OK;
    }
    return (on_file_only_open(pbak, true) >= 0 ? SKYLARK_OK : SKYLARK_NOT_OPENED);
}

/**************************************************************************************
 * 文件打开函数, 不带参数. 从资源管理器接受文件
 * 成功打开文件, 返回SKYLARK_OK
 * 否则, 返回非零值
 **************************************************************************************/
int
on_file_open(void)
{
    int err = SKYLARK_OK;
    TCHAR *file_list = (TCHAR *) calloc(sizeof(TCHAR), BUFF_32K);
    if (file_list == NULL)
    {
        printf("memory allocation failed\n");
        return EUE_OUT_OF_MEMORY;
    }
    if ((err = on_file_open_filename_dlg(NULL, file_list, BUFF_32K)) != SKYLARK_OK)
    {
        goto mem_clean;
    }
    if (!(GetFileAttributes(file_list) & FILE_ATTRIBUTE_DIRECTORY))
    {
        file_backup bak = {0};
        _tcsncpy(bak.rel_path, file_list, MAX_BUFFER);
        err = (on_file_only_open(&bak, true) >= 0 ? SKYLARK_OK : SKYLARK_NOT_OPENED);
    }
    else
    {
        TCHAR pathname[MAX_BUFFER] = {0};
        _tcsncpy(pathname, file_list, MAX_BUFFER - 1);
        int path_len = (int) _tcslen(pathname);
        TCHAR *p = file_list + path_len + 1;
        while (*p)
        {
            file_backup bak = {0};
            _sntprintf(bak.rel_path, MAX_BUFFER, _T("%s\\%s"), pathname, p);
            err = (on_file_only_open(&bak, true) >= 0 ? SKYLARK_OK : SKYLARK_NOT_OPENED);
            if (err != SKYLARK_OK)
            {
                goto mem_clean;
            }
            p += _tcslen(p) + 1;
        }
    }
mem_clean:
    if (file_list)
    {
        free(file_list);
    }
    return err;
}

/**************************************************************************************
 * 拖动标签时打开新窗口的函数, 接受参数是当前拖动的标签序号
 * 无论是否打开文件, 它总是弹出一个新窗口, 所以返回值总是SKYLARK_OK
 **************************************************************************************/
int
on_file_out_open(const int index, uint32_t *pid)
{
    eu_tabpage *p = on_tabpage_get_ptr(index);
    if (p && (!p->is_blank  || TAB_NOT_NUL(p)))
    {
        TCHAR process[MAX_BUFFER] = {0};
        if (GetModuleFileName(NULL , process , MAX_BUFFER) > 0)
        {
            int err = SKYLARK_NOT_OPENED;
            if (!eu_get_config()->m_session)
            {
                if (!p->is_blank)
                {
                    sptr_t pos = eu_sci_call(p, SCI_GETCURRENTPOS, 0, 0);
                    sptr_t lineno = eu_sci_call(p, SCI_LINEFROMPOSITION, pos, 0);
                    sptr_t row = eu_sci_call(p, SCI_POSITIONFROMLINE, lineno, 0);
                    _sntprintf(process, MAX_BUFFER, _T("%s%s\"%s\" -n%zd -c%zd"), process, _T(" -noremote "), p->pathfile, lineno+1, pos-row+1);
                    if (!p->plugin && p->hex_mode)
                    {
                        _tcsncat(process, _T(" -hex"), MAX_BUFFER);
                    }
                    err = on_file_close(p, FILE_ONLY_CLOSE);
                }
            }
            else
            {
                _sntprintf(process, MAX_BUFFER, _T("%s%s"), process, _T(" -noremote "));
                err = on_file_close(p, FILE_REMOTE_CLOSE);
            }
            if (err == SKYLARK_OK)
            {
                CloseHandle(eu_new_process(process, NULL, NULL, 0, pid));
            }
        }
    }
    return SKYLARK_OK;
}

/**************************************************************************************
 * 多标签打开文件时调用的第一个函数
 * hwnd参数可省略, 成功打开文件返回SKYLARK_OK
 **************************************************************************************/
int
on_file_redirect(HWND hwnd, file_backup *pbak)
{
    UNREFERENCED_PARAMETER(hwnd);
    int err = SKYLARK_NOT_OPENED;
    if (pbak && pbak->rel_path)
    {
        if (pbak->status || (_tcslen(pbak->rel_path) > 0 && !url_has_remote(pbak->rel_path)))
        {
            err = on_file_open_bakcup(pbak);
        }
        else if (url_has_remote(pbak->rel_path))
        {
            err = (on_file_open_remote(NULL, pbak, true) >= 0 ? SKYLARK_OK : SKYLARK_NOT_OPENED);
        }
    }
    if (err != SKYLARK_OK && TabCtrl_GetItemCount(g_tabpages) < 1)
    {   // 打开文件失败且标签小于1,则建立一个空白标签页
        err = on_file_new();
    }
    return err;
}

int
on_file_drop(HDROP hdrop)
{
    file_backup bak = {0};
    int count = DragQueryFile(hdrop, 0xFFFFFFFF, NULL, 0);
    for (int index = 0; index < count; ++index)
    {
        memset(bak.rel_path, 0, sizeof(bak.rel_path));
        DragQueryFile(hdrop, index, bak.rel_path, MAX_BUFFER);
        uint32_t attr = GetFileAttributes(bak.rel_path);
        if (!(attr & FILE_ATTRIBUTE_DIRECTORY))
        {
            on_file_only_open(&bak, true);
        }
        else
        {
            _tcsncat(bak.rel_path, _T("\\*"), MAX_BUFFER);
            on_file_open_bakcup(&bak);
            break;
        }
    }
    DragFinish(hdrop);
    return SKYLARK_OK;
}

static size_t
on_file_read_remote(void *buffer, size_t size, size_t nmemb, void *stream)
{
    eu_tabpage *pnode = (eu_tabpage *) stream;
    char *data = (char *) buffer;
    size_t len;
    size_t offset = 0;
    len = size * nmemb;
    if (!pnode->bytes_remaining)
    {
        if (pnode->hex_mode || !pnode->pmod)
        {
            pnode->codepage = eu_try_encoding(buffer, len, false, pnode->filename);
            if (pnode->codepage < IDM_UNI_UTF16LE)
            {
                pnode->eol = on_encoding_line_mode(buffer, len);
            }
            if (pnode->codepage != IDM_OTHER_BIN)
            {
                on_encoding_set_bom((const uint8_t *) buffer, pnode);
            }
            offset = pnode->pre_len;
        }
        else if (on_file_load_plugins(pnode, true))
        {
            printf("on_file_load_plugins failed\n");
            return 0;
        }
    }
    else
    {
        offset = 0;
    }
    if (pnode->codepage > IDM_UNI_UTF8B && pnode->codepage < IDM_OTHER_BIN)
    {
        char *pdst = NULL;
        char *psrc = data + offset;
        size_t src_len = len - offset;
        size_t dst_len = 0;
        euconv_t evd = {0};
        evd.src_from = eu_query_encoding_name(pnode->codepage);
        evd.dst_to = "utf-8";
        size_t res = on_encoding_do_iconv(&evd, psrc, &src_len, &pdst, &dst_len);
        if (res == (size_t) -1)
        {
            return 0;
        }
        if (pnode->eol < 0)
        {
            pnode->eol = on_encoding_line_mode(pdst, dst_len);
        }
        eu_sci_call(pnode, SCI_ADDTEXT, dst_len, (LPARAM)(pdst));
        free(pdst);
    }
    else
    {
        if (pnode->hex_mode || !pnode->pmod)
        {
            eu_sci_call(pnode, SCI_ADDTEXT, len - offset, (LPARAM)(data + offset));
        }
        else if (pnode->plugin && pnode->plugin->funcs.write)
        {
            pnode->plugin->funcs.write(&pnode->plugin->npp, &pnode->plugin->stream, eu_uint_cast(pnode->bytes_remaining), eu_uint_cast(len), buffer);
        }
    }
    pnode->bytes_remaining += len - offset;
    return len;
}

static int
on_file_remote_thread(eu_tabpage *p, file_backup *pbak, remotefs *premote)
{
    int ret = EUE_CURL_INIT_FAIL;
    if (pbak && STR_NOT_NUL(pbak->rel_path))
    {
        if (p->fs_server.networkaddr[0] == 0 && premote)
        {
            memcpy(&(p->fs_server), premote, sizeof(remotefs));
        }
        if ((p->reserved0 = (intptr_t)eu_utf16_utf8(pbak->rel_path, NULL)) != 0)
        {
            CURL *curl = NULL;
            if ((curl = on_remote_init_socket((const char *)p->reserved0, &p->fs_server)))
            {
                eu_curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_file_read_remote);
                eu_curl_easy_setopt(curl, CURLOPT_WRITEDATA, p);
            #if APP_DEBUG
                eu_curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
            #endif
                eu_curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 6L);
                eu_curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
                eu_curl_easy_setopt(curl, CURLOPT_FILETIME, 1L);
                ret = eu_curl_easy_perform(curl);
                if (!ret)
                {
                    ret = eu_curl_easy_getinfo(curl, CURLINFO_FILETIME_T, &p->st_mtime);
                    printf("pnode->st_mtime = %lld\n", p->st_mtime);
                }
                eu_curl_easy_cleanup(curl);
            }
            free((void *)p->reserved0);
            p->reserved0 = 0;
        }
    }
    return ret;
}

/**************************************************************************************
 * 打开远程文件的函数
 * premote为远程服务器信息的结构体变量
 * pbak包含文件路径的一个结构体变量
 * selection只影响返回值, 一般设为true
 * 函数成功, 返回值为当前打开标签的序号, 失败则为负数
 **************************************************************************************/
int
on_file_open_remote(remotefs *premote, file_backup *pbak, const bool selection)
{
    eu_tabpage *pnode = NULL;
    int result = on_file_open_if(pbak->rel_path, selection);
    if (result < SKYLARK_NOT_OPENED  || result == SKYLARK_OPENED)
    {
        return result;
    }
    if (result >= SKYLARK_OK && (pnode = on_tabpage_get_ptr(result)) != NULL)
    {   // 如果文件已经打开, 根据新参数更新插入符位置
        on_file_update_postion(pnode, pbak);
        if (pnode->nc_pos >= 0)
        {
            on_search_jmp_pos(pnode);
        }
        return result;
    }
    if ((result = on_file_node_initialize(&pnode, pbak)) != SKYLARK_OK)
    {
        return result;
    }
    if ((result = on_file_preload(pnode, pbak)) != SKYLARK_OK)
    {
        eu_safe_free(pnode);
        printf("on_file_preload failed, err = %d\n", result);
        return result;
    }
    if (on_tabpage_add(pnode))
    {
        eu_safe_free(pnode);
        return EUE_INSERT_TAB_FAIL;
    }
    on_file_before_open(pnode);
    on_tabpage_selection(pnode, pnode->tab_id);
    if (on_file_remote_thread(pnode, pbak, premote) != CURLE_OK)
    {
        on_sql_delete_backup_row(pnode);
        int index = on_tabpage_remove(&pnode);
        on_file_active_other(index);
        MSG_BOX(IDC_MSG_ATTACH_FAIL, IDC_MSG_ERROR, MB_ICONERROR | MB_OK);
        return EUE_CURL_NETWORK_ERR;
    }
    result = on_file_after_open(pnode, pbak);
    if (!pnode->hex_mode && pnode->pmod && pnode->plugin && pnode->plugin->funcs.write)
    {
        pnode->raw_size = pnode->bytes_remaining;
        pnode->plugin->funcs.destroystream(&pnode->plugin->npp, NULL, 0);
        on_file_update_focus(pnode, NULL);
        pnode->hex_mode = true;
        result = on_tabpage_selection(pnode, last_focus);
    }
    else if (!TAB_NOT_BIN(pnode) || pnode->hex_mode)
    {
        if (pnode->hex_mode)
        {
            pnode->hex_mode ^= true;
        }
        result = hexview_switch_mode(pnode);
    }
    return result;
}

static int
on_file_do_write(eu_tabpage *pnode, TCHAR *pathfilename, bool isbak, bool save_as)
{
    int ret = SKYLARK_OK;
    FILE *fp = NULL;
    if (!pnode->hex_mode)
    {
        pnode->bytes_remaining = (size_t) eu_sci_call(pnode, SCI_GETLENGTH, 0, 0);
        if ((pnode->write_buffer = (uint8_t *)(on_sci_range_text(pnode, 0, pnode->bytes_remaining))) == NULL)
        {
            ret = EUE_POINT_NULL;
            goto FILE_FINAL;
        }
        if (!save_as && eu_get_config()->m_limit && eu_get_config()->m_limit <= eu_int_cast(pnode->bytes_remaining/1024/1024))
        {   // 非二进制文件, 不产生备份, 直接写入原文件
            isbak = false;
            pathfilename = (pnode->pathfile[0]?pnode->pathfile:pathfilename);
        }
    }
    else if (!TAB_NOT_BIN(pnode))
    {
        // 原生的16进制视图
        // 是否自动cache
        bool is_cache = isbak && (!eu_get_config()->m_limit || eu_get_config()->m_limit > eu_int_cast(pnode->phex->total_items/1024/1024));
        // 既不另存为,又不产生缓存, 则直接写入源文件
        ret = hexview_save_data(pnode, save_as || is_cache ? pathfilename : NULL);
        goto FILE_FINAL;
    }
    else
    {   // 以流打开的16进制视图
        bool is_cache = isbak && (!eu_get_config()->m_limit || eu_get_config()->m_limit > eu_int_cast(pnode->phex->total_items/1024/1024));
        // hex_ascii为真,是原始的二进制,非utf8转码后的二进制
        if (pnode->phex->hex_ascii && pnode->phex->hmap)
        {
            printf("do not convert this code\n");
            //isbak = true;
            ret = hexview_save_data(pnode, save_as || is_cache ? pathfilename : NULL);
            goto FILE_FINAL;
        }
        // 转码utf8后生成的二进制编码, 保存时需要转换回去
        pnode->write_buffer = hexview_strdup_data(pnode, &pnode->bytes_remaining);
        if (pnode->write_buffer == NULL)
        {   // 文件过大,没有足够内存
            ret = EUE_POINT_NULL;
            goto FILE_FINAL;
        }
        if (!is_cache)
        {
            printf("convert this code\n");
            isbak = false;
        }
        pathfilename = save_as || is_cache ? pathfilename : pnode->pathfile;
    }
    if (!isbak && pnode->codepage > IDM_UNI_UTF8B && pnode->codepage < IDM_OTHER_BIN)
    {
        char *pdst = NULL;
        char *pbuf = (char *)pnode->write_buffer;
        size_t dst_len = 0;
        size_t src_len = pnode->bytes_remaining;
        euconv_t evd = {0};
        evd.src_from = "utf-8";
        evd.dst_to = eu_query_encoding_name(pnode->codepage);
        printf("convert(%s) to (%s)\n", evd.src_from, evd.dst_to);
        size_t res = on_encoding_do_iconv(&evd, pbuf, &src_len, &pdst, &dst_len);
        if (res != (size_t) -1)
        {  // 释放之前的内存, 指向新的内存块
            free(pnode->write_buffer);
            pnode->write_buffer = (uint8_t *)pdst;
            pnode->bytes_remaining = dst_len;
        }
        else
        {
            ret = EUE_ICONV_FAIL;
            MSG_BOX(IDC_MSG_ICONV_FAIL1, IDC_MSG_ERROR, MB_ICONERROR | MB_OK);
            goto FILE_FINAL;
        }
    }
    if ((fp = _tfopen(pathfilename, _T("wb"))) == NULL)
    {
        ret = EUE_OPEN_FILE_ERR;
        MSG_BOX_ERR(IDC_MSG_OPEN_FAIL, IDC_MSG_ERROR, MB_ICONERROR | MB_OK);
        goto FILE_FINAL;
    }   // 文件备份时, 不转换回原编码
    if (!isbak && pnode->pre_len > 0)
    {
        if (fwrite(pnode->pre_context, 1, pnode->pre_len, fp) < pnode->pre_len)
        {
            ret = EUE_WRITE_FILE_ERR;
            MSG_BOX(IDC_MSG_WRITE_FAIL, IDC_MSG_ERROR, MB_ICONERROR | MB_OK);
            goto FILE_FINAL;
        }
    }
    if (fwrite(pnode->write_buffer, pnode->bytes_remaining, 1, fp) == -1)
    {
        ret = EUE_WRITE_FILE_ERR;
        MSG_BOX(IDC_MSG_WRITE_FAIL, IDC_MSG_ERROR, MB_ICONERROR | MB_OK);
    }
FILE_FINAL:
    eu_safe_free(pnode->write_buffer);
    if (fp)
    {
        fclose(fp);
    }
    if (!save_as)
    {
        on_file_update_time(pnode, 0);
    }
    return ret;
}

static size_t
on_file_write_remote(void *buffer, size_t size, size_t nmemb, void *stream)
{
    size_t len;
    eu_tabpage *pnode = (eu_tabpage *) stream;
    if (pnode->bytes_written >= pnode->bytes_remaining)
    {
        return 0;
    }
    if (pnode->needpre)
    {
        memcpy(buffer, pnode->pre_context, pnode->pre_len);
        len = pnode->pre_len;
        pnode->needpre = false;
        return len;
    }
    len = size * nmemb;
    if (pnode->bytes_written + len > pnode->bytes_remaining)
    {
        len = pnode->bytes_remaining - pnode->bytes_written;
    }
    memcpy(buffer, pnode->write_buffer + pnode->bytes_written, len);
    pnode->bytes_written += len;
    return len;
}

static int
on_file_write_backup(const eu_tabpage *pnode)
{
    int ret = 0;
    if (pnode && !util_availed_char(pnode->fs_server.networkaddr[0]))
    {
        size_t len = _tcslen(pnode->pathfile);
        TCHAR *pbakup = len > 0 ? (TCHAR *)calloc(sizeof(TCHAR), len + 2) : NULL;
        if (pbakup)
        {
            _sntprintf(pbakup, len+1, _T("%s~"), pnode->pathfile);
            if (!CopyFile(pnode->pathfile, pbakup, false))
            {
                MSG_BOX(IDC_MSG_COPY_FAIL, IDC_MSG_ERROR, MB_ICONERROR|MB_OK);
                ret = EUE_COPY_FILE_ERR;
            }
            free(pbakup);
        }
    }
    return ret;
}

int
on_file_stream_upload(eu_tabpage *pnode, wchar_t *pmsg)
{
    int err = EUE_CURL_NETWORK_ERR;
    if (pnode)
    {
        char *cnv = NULL;
        CURL *curl = NULL;
        pnode->bytes_written = 0;
        pnode->needpre = false;
        if (pnode->pre_len > 0)
        {
            pnode->needpre = true;
        }
        do
        {
            if ((cnv = eu_utf16_utf8(pnode->pathfile, NULL)) == NULL)
            {
                err = EUE_API_CONV_FAIL;
                break;
            }
            if (!(curl = on_remote_init_socket(cnv, &pnode->fs_server)))
            {
                printf("on_remote_init_socket return false\n");
                err = EUE_CURL_INIT_FAIL;
                break;
            }
            eu_curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
            eu_curl_easy_setopt(curl, CURLOPT_READFUNCTION, on_file_write_remote);
            eu_curl_easy_setopt(curl, CURLOPT_READDATA, pnode);
        #if defined(APP_DEBUG) && (APP_DEBUG > 0)
            eu_curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        #endif
            // 设置链接超时, 不要设置上传超时, 防止文件被截断
            eu_curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 6L);
            err = eu_curl_easy_perform(curl);
            if (err && pmsg)
            {
                util_make_u16(eu_curl_easy_strerror(err), pmsg, PERROR_LEN);
            }
            eu_curl_easy_cleanup(curl);
        } while(0);
        eu_safe_free(cnv);
    }
    return err;
}

int
on_file_save(eu_tabpage *pnode, const bool save_as)
{
    char *ptext = NULL;
    int err = SKYLARK_OK;
    npn_nmhdr nphdr = {0};
    if (!pnode)
    {
        return EUE_TAB_NULL;
    }
    if (!on_sci_doc_modified(pnode) && !save_as)
    {
        err = EUE_UNEXPECTED_SAVE;
        goto SAVE_FINAL;
    }  // 插件打开的文件, 保存
    if (pnode->pmod && pnode->plugin && !save_as)
    {
        nphdr.pnode = (void *)pnode;
        if (eu_get_config()->m_write_copy && on_file_write_backup(pnode))
        {
            err = EUE_WRITE_FILE_ERR;
            goto SAVE_FINAL;
        }
        npn_send_notify(pnode->hwnd_sc, NPP_DOC_MODIFY, OPERATE_SAVE, &nphdr);
        goto SAVE_FINAL;
    }  // 编辑器文件另存为
    if (pnode->is_blank || save_as)
    {
        TCHAR pcd[MAX_PATH] = {0};
        TCHAR full_path[MAX_BUFFER] = {0};
        _tcsncpy(full_path, pnode->filename, MAX_BUFFER);
        if (pnode->is_blank || url_has_remote(pnode->pathfile))
        {
            GetEnvironmentVariable(_T("USERPROFILE"), pcd, MAX_PATH - 1);
        }
        if (on_file_get_filename_dlg(full_path, _countof(full_path), STR_NOT_NUL(pcd) ? pcd : pnode->pathname))
        {
            err = EUE_LOCAL_FILE_ERR;
            goto SAVE_FINAL;
        }
        _tsplitpath(full_path, NULL, NULL, pnode->filename, pnode->extname);
        if (_tcslen(pnode->extname) > 0)
        {
            _tcsncat(pnode->filename, pnode->extname, MAX_BUFFER);
        }
        if (!pnode->pmod)
        {   // 非插件另存为
            if (on_file_do_write(pnode, full_path, false, true))
            {
                err = EUE_WRITE_FILE_ERR;
                goto SAVE_FINAL;
            }
            _tcsncpy(pnode->pathfile, full_path, MAX_BUFFER);
            // 有可能是远程服务器文件, 清除网址
            pnode->fs_server.networkaddr[0] = 0;
            on_file_update_time(pnode, 0);
            util_set_title(pnode);
            pnode->doc_ptr = on_doc_get_type(pnode->filename);
            on_sci_before_file(pnode);
            on_sci_after_file(pnode);
            if (pnode->is_blank)
            {
                pnode->is_blank = false;
            }
        }
        else if (pnode->plugin)
        {   // 插件另存为
            np_plugins_savefileas(&pnode->plugin->funcs, &pnode->plugin->npp, full_path);
            nphdr.pnode = (void *)pnode;
            npn_send_notify(pnode->hwnd_sc, NPP_DOC_MODIFY, OPERATE_SAVEAS, &nphdr);
        }
    }
    else if (util_availed_char(pnode->fs_server.networkaddr[0]))
    {   // sftp文件保存
        size_t buf_len = 0;
        wchar_t msg[PERROR_LEN+1] = {0};
        if (TAB_NOT_BIN(pnode))
        {
            if (pnode->hex_mode)
            {
                ptext = (char *)hexview_strdup_data(pnode, &buf_len);
                if (ptext == NULL)
                {
                    err = EUE_POINT_NULL;
                    goto SAVE_FINAL;
                }
            }
            else if (!(ptext = util_strdup_content(pnode, &buf_len)))
            {
                err = EUE_POINT_NULL;
                goto SAVE_FINAL;
            }
            if (pnode->codepage > IDM_UNI_UTF8B && pnode->codepage < IDM_OTHER_BIN)
            {
                char *psrc = ptext;
                size_t src_len = buf_len;
                size_t dst_len = 0;
                euconv_t evd = {0};
                evd.src_from = "utf-8";
                evd.dst_to = eu_query_encoding_name(pnode->codepage);
                size_t res = on_encoding_do_iconv(&evd, psrc, &src_len, (char **)&pnode->write_buffer, &pnode->bytes_remaining);
                if (res == (size_t) -1)
                {
                    MSG_BOX(IDC_MSG_ICONV_FAIL1, IDC_MSG_ERROR, MB_ICONERROR | MB_OK);
                    err = EUE_ICONV_FAIL;
                    goto SAVE_FINAL;
                }
            }
            else
            {
                pnode->bytes_remaining = buf_len;
                pnode->write_buffer = (uint8_t *)ptext;
            }
        }
        else
        {
            pnode->write_buffer = hexview_strdup_data(pnode, &pnode->bytes_remaining);
            if (pnode->write_buffer == NULL)
            {
                err = EUE_POINT_NULL;
                goto SAVE_FINAL;
            }
        }
        if ((err = on_file_stream_upload(pnode, msg)) != SKYLARK_OK)
        {
            print_err_msg(IDC_MSG_ATTACH_ERRORS, msg);
            pnode->st_mtime = 0;
            goto SAVE_FINAL;
        }
        on_file_update_time(pnode, time(NULL));
    }
    else
    {
        if (eu_get_config()->m_write_copy && on_file_write_backup(pnode))
        {
            err = EUE_WRITE_FILE_ERR;
            goto SAVE_FINAL;
        }
        if (on_file_do_write(pnode, pnode->pathfile, false, false))
        {
            err = EUE_WRITE_FILE_ERR;
        }
    }
SAVE_FINAL:
    // 防止重复释放内存
    uint8_t *dup = (uint8_t *)pnode->write_buffer;
    if (dup && dup != (uint8_t *)ptext)
    {
        eu_safe_free(pnode->write_buffer);
    }
    if (ptext && dup != (uint8_t *)ptext)
    {
        free(ptext);
    }
    if (!err)
    {
        if (!pnode->pmod)
        {   // 发送SCI_SETSAVEPOINT消息
            eu_sci_call(pnode, SCI_SETSAVEPOINT, 0, 0);
            if (!(pnode->is_blank || save_as))
            {
                if (pnode->doc_ptr && pnode->doc_ptr->fn_reload_symlist)
                {
                    pnode->doc_ptr->fn_reload_symlist(pnode);
                }
            }
        }
        on_sci_refresh_ui(pnode);
    }
    return err;
}

int
on_file_save_as(eu_tabpage *pnode)
{
    return !pnode ? EUE_TAB_NULL : on_file_save(pnode, true);
}

int
on_file_all_save(void)
{
    int count = TabCtrl_GetItemCount(g_tabpages);
    for (int index = 0; index < count; ++index)
    {
        TCITEM tci = {TCIF_PARAM};
        TabCtrl_GetItem(g_tabpages, index, &tci);
        eu_tabpage *pnode = (eu_tabpage *) (tci.lParam);
        if (pnode)
        {
            on_file_save(pnode, false);
        }
    }
    return SKYLARK_OK;
}

static void
on_file_guid(TCHAR *buf, int len)
{
    GUID  guid;
    if (S_OK == CoCreateGuid(&guid))
    {
        _sntprintf(buf,
                   len,
                   _T("%08x%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x"),
                   guid.Data1, guid.Data2, guid.Data3,
                   guid.Data4[0], guid.Data4[1],
                   guid.Data4[2], guid.Data4[3],
                   guid.Data4[4], guid.Data4[5],
                   guid.Data4[6], guid.Data4[7]);
    }
    else
    {
        eu_rand_str(buf, 32);
    }
}

static void
on_file_npp_write(eu_tabpage *pnode, const wchar_t *cache_path, bool isbak)
{
#define TMP_SUFFX (L".$bak~")
    bool is_cache = isbak && (!eu_get_config()->m_limit || eu_get_config()->m_limit > eu_int_cast(pnode->raw_size/1024/1024));
    if (is_cache)
    {   // 保存本地文件, 并移动到cache_path
        int ret = 0;
        wchar_t *tmp_path = NULL;
        wchar_t pathfile[MAX_BUFFER] = {0};
        if (!np_plugins_getvalue(&pnode->plugin->funcs, &pnode->plugin->npp, NV_TABTITLE, (void **)&tmp_path) && tmp_path)
        {
            _snwprintf(pathfile, MAX_BUFFER, L"%s", tmp_path);
            wcsncat(tmp_path, TMP_SUFFX, MAX_BUFFER);
            ret = CopyFileW(pathfile, tmp_path, false);
        }
        if (ret)
        {
            np_plugins_savefile(&pnode->plugin->funcs, &pnode->plugin->npp);
            ret = MoveFileW(pathfile, cache_path);
        }
        if (ret)
        {
            ret = MoveFileExW(tmp_path, pathfile, MOVEFILE_REPLACE_EXISTING|MOVEFILE_COPY_ALLOWED);
        }
        eu_safe_free(tmp_path);
    }
    else if (!url_has_remote(pnode->pathfile))
    {   // no cache, 则自动保存
        np_plugins_savefile(&pnode->plugin->funcs, &pnode->plugin->npp);
    }
    else
    {   // 网络流文件的修改丢弃
        on_sql_delete_backup_row(pnode);
    }
#undef TMP_SUFFX
}

static void
on_file_save_backup(eu_tabpage *pnode, const CLOSE_MODE mode)
{
    TCHAR buf[QW_SIZE] = {0};
    file_backup filebak = {0};
    filebak.cp = pnode->codepage;
    filebak.bakcp = !TAB_NOT_BIN(pnode) ? IDM_OTHER_BIN : IDM_UNI_UTF8;
    if (!file_click_close(mode))
    {
        if (!pnode->is_blank || TAB_NOT_NUL(pnode))
        {
            if (pnode->be_modify)
            {
                on_file_guid(buf, QW_SIZE - 1);
                if (!pnode->pmod)
                {
                    _sntprintf(filebak.bak_path, MAX_BUFFER, _T("%s\\cache\\%s"), eu_config_path, buf);
                    on_file_do_write(pnode, filebak.bak_path, true, false);
                }
                else
                {
                    _sntprintf(filebak.bak_path, MAX_BUFFER, _T("%s\\cache\\%s%s"), eu_config_path, buf, pnode->extname);
                    on_file_npp_write(pnode, filebak.bak_path, true);
                }
                filebak.status = 1;
                if (pnode->hex_mode && pnode->phex && pnode->phex->hex_ascii)
                {
                    filebak.bakcp = pnode->codepage;
                }
            }
            _tcscpy(filebak.rel_path, pnode->pathfile);
            filebak.tab_id = pnode->tab_id;
            filebak.eol = pnode->eol;
            filebak.blank = pnode->is_blank;
            filebak.hex = !(pnode->pmod && pnode->plugin) ? pnode->hex_mode : 0;
            filebak.focus = pnode->last_focus;
            filebak.zoom = pnode->zoom_level > SELECTION_ZOOM_LEVEEL ? pnode->zoom_level : 0;
            on_search_page_mark(pnode, filebak.mark_id, MAX_BUFFER-1);
            on_search_fold_kept(pnode, filebak.fold_id, MAX_BUFFER-1);
            filebak.postion = eu_sci_call(pnode, SCI_GETCURRENTPOS, 0, 0);
            if (mode == FILE_REMOTE_CLOSE)
            {
                filebak.sync = (int)(url_has_remote(filebak.rel_path) ? 1 :
                               (TabCtrl_GetItemCount(g_tabpages) <= 1 && eu_get_config()->m_exit) ? 1 : 0);

                eu_update_backup_table(&filebak, DB_FILE);
                on_sql_delete_backup_row(pnode);
            }
            else
            {
                filebak.sync = 1;
                eu_update_backup_table(&filebak, DB_MEM);
            }
        }
    }
    else
    {
        on_sql_delete_backup_row(pnode);
    }
}

int
on_file_close(eu_tabpage *pnode, CLOSE_MODE mode)
{
    EU_VERIFY(g_tabpages != NULL);
    int index = -1;
    int ifocus = TabCtrl_GetCurSel(g_tabpages);
    eu_tabpage *p = on_tabpage_get_ptr(ifocus);
    if (!pnode)
    {
        return EUE_TAB_NULL;
    }
    if (eu_get_config()->m_session)
    {
        on_file_save_backup(pnode, mode);
    }
    else
    {
        on_sql_delete_backup_row(pnode);
    }
    if (!file_click_close(mode) && eu_get_config()->m_session)
    {
        // do nothing
    }
    else if (on_sci_doc_modified(pnode))
    {
        TCHAR msg[100 + MAX_BUFFER];
        if (pnode->pathfile[0] == 0)
        {
            LOAD_I18N_RESSTR(IDC_MSG_SAVE_STR1, save_str);
            _sntprintf(msg, _countof(msg) - 1, save_str);
        }
        else
        {
            LOAD_I18N_RESSTR(IDC_MSG_SAVE_STR2, save_str);
            _sntprintf(msg, _countof(msg) - 1, save_str, pnode->pathfile);
        }
        LOAD_APP_RESSTR(IDS_APP_TITLE, title);
        /* 如果需要确认, 选中该标签 */
        on_tabpage_selection(pnode, -1);
        int decision = eu_msgbox(eu_module_hwnd(), msg, title, MB_YESNOCANCEL);
        if (decision == IDCANCEL)
        {
            printf("abort closing file\n");
            return SKYLARK_OPENED;
        }
        else if (decision == IDYES)
        {
            on_file_save(pnode, false);
        }
    }
    /* 清理该文件的位置导航信息 */
    on_search_clean_navigate_this(pnode);
    /* 排序最近关闭文件的列表 */
    if (file_click_close(mode) && !pnode->is_blank)
    {
        on_file_push_recent(pnode);
    }
    /* 关闭标签后需要激活其他标签 */
    if ((index = on_tabpage_remove(&pnode)) >= 0 && (mode == FILE_REMOTE_CLOSE || mode == FILE_ONLY_CLOSE))
    {
        if (index == ifocus || mode == FILE_REMOTE_CLOSE)
        {
            on_file_active_other(index);
        }
        else if (p)
        {
            on_tabpage_selection(p, -1);
        }
    }
    return SKYLARK_OK;
}

int
on_file_all_close(void)
{
    int this_index = 0;
    int count = TabCtrl_GetItemCount(g_tabpages);
    for (int index = 0; index < count; ++index)
    {
        eu_tabpage *p = on_tabpage_get_ptr(this_index);
        if (p && on_file_close(p, FILE_ALL_CLOSE))
        {
            ++this_index;
        }
    }
    if (!this_index)
    {   // 最后一个标签
        if (!eu_get_config()->m_exit)
        {
            file_backup bak = {0};
            share_send_msg(&bak);
        }
        else
        {
            eu_close_edit();
        }
    }
    else
    {   // 如果有标签关闭失败
        on_tabpage_select_index(0);
    }
    return SKYLARK_OK;
}

int
on_file_left_close(void)
{
    int first = 0;
    int this_index = TabCtrl_GetCurSel(g_tabpages);
    eu_tabpage *pnode = on_tabpage_get_ptr(this_index);
    for (int index = 0; index < this_index; ++index)
    {
        eu_tabpage *p = on_tabpage_get_ptr(first);
        if (p)
        {
            if (on_file_close(p, FILE_EXCLUDE_CLOSE))
            {
                ++first;
            }
        }
    }
    on_tabpage_selection(pnode, -1);
    return SKYLARK_OK;
}

int
on_file_right_close(void)
{
    int count = TabCtrl_GetItemCount(g_tabpages);
    const int this_index = TabCtrl_GetCurSel(g_tabpages);
    int right = count - 1;
    for (; this_index < right; --right)
    {
        eu_tabpage *p = on_tabpage_get_ptr(right);
        if (p)
        {
            if (on_file_close(p, FILE_EXCLUDE_CLOSE))
            {
                continue;
            }
        }
    }
    if (count > this_index + 1)
    {
        on_tabpage_selection(on_tabpage_get_ptr(this_index), -1);
    }
    return SKYLARK_OK;
}

int
on_file_exclude_close(eu_tabpage *pnode)
{
    int this_index = 0;
    cvector_vector_type(int) v = NULL;
    if ((on_tabpage_sel_number(&v, false)) > 0)
    {
        eu_tabpage *p = NULL;
        for (int index = 0, count = TabCtrl_GetItemCount(g_tabpages); index < count; ++index)
        {
            if (eu_cvector_at(v, index) >= 0)
            {
                ++this_index;
                continue;
            }
            if ((p = on_tabpage_get_ptr(this_index)) && on_file_close(p, FILE_EXCLUDE_CLOSE))
            {
                ++this_index;
            }
        }
        on_tabpage_selection(pnode, -1);
    }
    cvector_freep(&v);
    return SKYLARK_OK;
}

static unsigned __stdcall
on_file_check_save(void *lp)
{
    int err = 0;
    int at_focus = -1;
    int count = TabCtrl_GetItemCount(g_tabpages);
    if (eu_get_config()->m_session)
    {
        at_focus = TabCtrl_GetCurSel(g_tabpages);
    }
    for (int index = 0; index < count; ++index)
    {
        TCITEM tci = {TCIF_PARAM};
        TabCtrl_GetItem(g_tabpages, err, &tci);
        eu_tabpage *pnode = (eu_tabpage *) (tci.lParam);
        if (pnode)
        {
            pnode->tab_id = index;
            pnode->zoom_level = pnode->zoom_level > SELECTION_ZOOM_LEVEEL ? (int) eu_sci_call(pnode, SCI_GETZOOM, 0, 0) : 0;
            if (at_focus >= 0)
            {
                pnode->last_focus = pnode->tab_id == at_focus;
            }
            if (on_file_close(pnode, FILE_SHUTDOWN))
            {
                ++err;
                continue;
            }
        }
    }
    if (on_sql_sync_session())
    {
        printf("on_sql_sync_session return false in %s\n", __FUNCTION__);
    }
    if (!err)
    {
        PostMessage(eu_module_hwnd(), WM_BACKUP_OVER, 0, 0);
    }
    _InterlockedExchange(&file_close_id, 0);
    return err;
}

static void
on_file_unsave_close(void)
{
    if (!file_close_id)
    {
        CloseHandle((HANDLE) _beginthreadex(NULL, 0, on_file_check_save, NULL, 0, (DWORD *)&file_close_id));
        if (!file_event_final)
        {
            share_envent_create_file_sem(&file_event_final);
        }
        else
        {
            SetEvent(file_event_final);
        }
    }
}

void
on_file_finish_wait(void)
{
    if (file_event_final)
    {   // we destroy windows, that it should wait for file close thread exit
        share_envent_wait_file_close_sem(&file_event_final);
    }
}

void
on_file_edit_exit(HWND hwnd)
{
    if (eu_get_config()->m_session)
    {
        on_file_unsave_close();
        if (hwnd)
        {
            ShowWindow(hwnd, SW_HIDE);
        }
        if (eu_get_search_hwnd())
        {
            ShowWindow(eu_get_search_hwnd(), SW_HIDE);
        }
    }
    else
    {
        on_file_check_save(NULL);
    }
}

void
on_file_edit_restart(HWND hwnd, const bool admin)
{
    HANDLE handle = NULL;
    TCHAR process[MAX_BUFFER] = {0};
    if (GetModuleFileName(NULL , process , MAX_BUFFER) > 0)
    {
        int len = 0;
        uint32_t pid = GetCurrentProcessId();
        _tcsncat(process, _T(" -restart "), MAX_BUFFER);
        len = (int)_tcslen(process);
        _sntprintf(process + len, MAX_BUFFER - len, _T("%lu"), pid);
        on_file_kill_tree(pid);
        if (!admin || on_reg_admin())
        {
            on_file_edit_exit(hwnd);
            CloseHandle(eu_new_process(process, NULL, NULL, 0, NULL));
        }
        else
        {
            if (on_reg_admin_execute(process))
            {
                on_file_edit_exit(hwnd);
            }
        }
    }
}

void
on_file_backup_menu(void)
{
    eu_get_config()->m_write_copy ^= true;
}

void
on_file_session_menu(void)
{
    eu_get_config()->m_session ^= true;
}

void
on_file_close_last_tab(void)
{
    eu_get_config()->m_exit ^= true;
}

void
on_file_new_eols(eu_tabpage *pnode, const int eol_mode)
{
    eu_get_config()->new_file_eol = eol_mode;
}

void
on_file_new_encoding(eu_tabpage *pnode, const int new_enc)
{
    eu_get_config()->new_file_enc = new_enc;
}

static int
on_file_do_restore(void *data, int count, char **column, char **names)
{
    file_backup bak = {0};
    for (int i = 0; i < count; ++i)
    {
        if (STRCMP(names[i], ==, "szName"))
        {
            if (MultiByteToWideChar(CP_UTF8, 0, column[i], -1, bak.rel_path, _countof(bak.rel_path)))
            {
                if (_tcsrchr(bak.rel_path, _T('&')))
                {
                    eu_wstr_replace(bak.rel_path, _countof(bak.rel_path), _T("&"), _T("&&"));
                }
            }
        }
        else if (STRCMP(names[i], ==, "szPos"))
        {
            bak.postion = _atoz(column[i]);
        }
        else if (STRCMP(names[i], ==, "szHex"))
        {
            bak.hex = atoi(column[i]);
        }
    }
    if (_tcslen(bak.rel_path) > 0)
    {
        if (url_has_remote(bak.rel_path))
        {
            if (on_file_open_remote(NULL, &bak, false) >= SKYLARK_OK)
            {
                UpdateWindowEx(g_tabpages);
                return SKYLARK_SQL_END;
            }
        }
        else if (on_file_only_open(&bak, false) >= SKYLARK_OK)
        {   // 刷新tab矩形区域
            UpdateWindowEx(g_tabpages);
            // 文件成功打开, 结束回调
            return SKYLARK_SQL_END;
        }
    }
    return SKYLARK_OK;
}

void
on_file_restore_recent(void)
{
    on_sql_mem_post("SELECT szName,szPos,szHex FROM file_recent ORDER BY szDate DESC;", on_file_do_restore, NULL);
}

void
on_file_reload_current(eu_tabpage *pnode)
{
    if (pnode)
    {
        bool reload = true;
        bool modified = pnode->be_modify;
        sptr_t pos = eu_sci_call(pnode, SCI_GETCURRENTPOS, 0, 0);
        sptr_t current_line = eu_sci_call(pnode, SCI_LINEFROMPOSITION, pos, 0);
        if (modified && !eu_get_config()->inter_reserved_0)
        {
            LOAD_APP_RESSTR(IDS_APP_TITLE, title);
            LOAD_I18N_RESSTR(IDS_FILE_RELOAD_STR, msg);
            int dec = eu_msgbox(eu_module_hwnd(), msg, title, MB_YESNOALWAYS);
            switch (dec) {
                case IDALWAYS:
                    eu_get_config()->inter_reserved_0 = 1;
                    break;
                case IDYES:
                    break;
                default:
                    reload = false;
                    break;
            }
        }
        if (reload)
        {
            eu_sci_call(pnode, SCI_CLEARALL, 0, 0);
            if (on_file_load(pnode, NULL, true) == SKYLARK_OK)
            {
                sptr_t max_line = eu_sci_call(pnode, SCI_GETLINECOUNT, 0, 0);
                if (current_line > max_line - 1)
                {
                    current_line = max_line - 1;
                }
                eu_sci_call(pnode, SCI_SETUNDOCOLLECTION, 1, 0);
                eu_sci_call(pnode, SCI_EMPTYUNDOBUFFER, 0, 0);
                eu_sci_call(pnode, SCI_SETSAVEPOINT, 0, 0);
                on_search_jmp_line(pnode, current_line, 0);
                pnode->st_mtime = util_last_time(pnode->pathfile);
            }
        }
    }
}
