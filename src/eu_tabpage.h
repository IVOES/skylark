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

#ifndef _H_SKYLARK_TABPAGES_
#define _H_SKYLARK_TABPAGES_

#define CONFIG_KEY_MATERIAL_TABPAGES "EU_TABPAG"
#define MOVE_SPLITE_LINE 2
#define TABS_HEIGHT_DEFAULT   21

#ifdef __cplusplus
extern "C"
{
#endif

typedef int (*tab_ptr)(eu_tabpage *p);

extern HWND  g_tabpages;
extern HMENU pop_tab_menu;
extern HMENU pop_editor_menu;

struct _tabpage
{
    HWND hwnd_sc;               // 当前编辑器句柄
    sptr_t eusc;                // 当前编辑器类指针
    RECT rect_sc;               // 编辑器矩形区域
    int match_count;            // 查找时匹配计数
    HWND hwnd_symlist;          // tab关联的子窗口句柄
    RECT rect_symlist;
    HWND hwnd_symtree;
    RECT rect_symtree;
    HWND hwnd_qredit;
    RECT rect_qredit;
    HWND hwnd_qrtable;
    RECT rect_qrtable;
    int  tab_id;                // tab编号,用于保存会话
    bool edit_show;             // 是否显示文件运行窗口
    bool sym_show;              // 是否显示右侧边栏
    bool foldline;              // 是否存在折叠线
    db_conn db_config;          // 数据库配置
    db_handles udb;
    bool db_is_connect;
    redis_conn redis_config;    // redis配置
    redis_handles rs_handle;
    bool redis_is_connect;
    TCHAR pathfile[MAX_PATH];   // 文件带路径名
    TCHAR pathname[MAX_PATH];   // 文件所在路径名
    TCHAR filename[MAX_PATH];   // 文件名
    TCHAR extname[_MAX_EXT];    // 扩展名
    TCHAR bakpath[MAX_PATH];    // 备份后的名称
    time_t st_mtime;            // 文件修改时间
    uint32_t file_attr;         // 文件属性,只读/可写...
    bool is_blank;              // 新建文件,空白标签
    bool at_close;              // 是否绘制了关闭按钮
    doctype_t *doc_ptr;         // 文件类型指针
    int codepage;               // 文件编码
    char pre_context[4+1];      // 保存bom
    size_t pre_len;             // bom的长度
    bool needpre;               // 是否需要bom      
    int eol;                    // 换行符
    int64_t begin_pos;          // 开始选择位置
    remotefs fs_server;         // SFTP
    uint64_t raw_size;          // 文件初始大小
    volatile long pcre_id;      // pcre线程id
    volatile long json_id;      // 解析json线程id
    size_t bytes_remaining;     // 文件变动后的大小
    size_t bytes_written;       // 文件保存时写入的长度
    uint8_t *write_buffer;      // 文件保存时写入的缓存区
    PHEXVIEW phex;              // 二进制视图
    bool hex_mode;              // 是否处于16禁止编辑状态
    bool be_modify;             // 文档是否修改, 同步hex模式
    bool last_focus;            // 保存前台焦点
    int nc_line;                // 关闭编辑器时, 所处的行号
    int zoom_level;             // 标签页的放大倍数
};

int  on_tabpage_create_dlg(HWND hwnd);
int  on_tabpage_add(eu_tabpage *pnode);
int  on_tabpage_remove(eu_tabpage **ppnode);
int  on_tabpage_reload_file(eu_tabpage *pnode, int flags);
int  on_tabpage_editor_modify(eu_tabpage *pnode, const char *);
int  on_tabpage_create_rclick(void);
int  on_tabpage_theme_changed(eu_tabpage *p);
void on_tabpage_adjust_box(RECT *ptp);
void on_tabpage_adjust_window(eu_tabpage *pnode);
void on_tabpage_set_title(int ntab, TCHAR *title);
void on_tabpage_selection(eu_tabpage *pnode, int index);
void on_tabpage_changing(void);
void on_tabpage_destroy_rclick(void);
void on_tabpage_symlist_click(eu_tabpage *pnode);
void on_tabpage_foreach(tab_ptr fntab);
void on_tabpage_newdoc_reload(void);

eu_tabpage *on_tabpage_get_handle(void *hwnd_sc);
eu_tabpage *on_tabpage_get_ptr(int index);
eu_tabpage *on_tabpage_select_index(int index);
eu_tabpage *on_tabpage_focus_at(void);
TCHAR *on_tabpage_newdoc_name(TCHAR *filename, int len);
LRESULT on_tabpage_draw_item(HWND hwnd, WPARAM wParam, LPARAM lParam);

#ifdef __cplusplus
}
#endif

#endif
