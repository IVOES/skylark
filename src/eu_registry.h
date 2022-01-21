/******************************************************************************
 * This file is part of Skylark project
 * Copyright ©2021 Hua andy <hua.andy@gmail.com>

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

#ifndef _EU_REGISTRY_H_
#define _EU_REGISTRY_H_

#ifdef __cplusplus
extern "C" {
#endif

int on_reg_file_popup_menu(void);
int on_reg_dir_popup_menu(void);
int on_reg_files_association(void);
bool on_reg_admin(void);
unsigned __stdcall on_reg_update_menu(void* lp);

#ifdef __cplusplus
}
#endif

#endif  // _EU_REGISTRY_H_
