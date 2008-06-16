////////////////////////////////////////////////////////////////////////////
//  Copyright (C) 2008 by Alexander Galanin                               //
//  gaa.nnov@mail.ru                                                      //
//                                                                        //
//  This program is free software; you can redistribute it and/or modify  //
//  it under the terms of the GNU Lesser General Public License as        //
//  published by the Free Software Foundation; either version 3 of the    //
//  License, or (at your option) any later version.                       //
//                                                                        //
//  This program is distributed in the hope that it will be useful,       //
//  but WITHOUT ANY WARRANTY; without even the implied warranty of        //
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         //
//  GNU General Public License for more details.                          //
//                                                                        //
//  You should have received a copy of the GNU Lesser General Public      //
//  License along with this program; if not, write to the                 //
//  Free Software Foundation, Inc.,                                       //
//  51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA               //
////////////////////////////////////////////////////////////////////////////

#define ROOT_NODE_INDEX (-1)

#include <zip.h>
#include <syslog.h>

#include "fuseZipData.h"

FuseZipData::FuseZipData(struct zip *z, char *cwd): m_cwd(cwd), m_zip(z) {
    if (cwd == NULL) {
        throw std::bad_alloc();
    }
    build_tree();
}

FuseZipData::~FuseZipData() {
    if (chdir(m_cwd) != 0) {
        syslog(LOG_ERR, "Unable to chdir() to archive directory %s. Trying to save file into /tmp", m_cwd);
        if (chdir(getenv("TMP")) != 0) {
            chdir("/tmp");
        }
    }
    int res = zip_close(m_zip);
    if (res != 0) {
        syslog(LOG_ERR, "Error while closing archive: %s", zip_strerror(m_zip));
    }
    for (filemap_t::iterator i = files.begin(); i != files.end(); ++i) {
        delete i->second;
    }
    free(m_cwd);
}

void FuseZipData::build_tree() {
    FileNode *root_node = new FileNode(this, "", ROOT_NODE_INDEX);
    root_node->is_dir = true;

    int n = zip_get_num_files(m_zip);
    for (int i = 0; i < n; ++i) {
        FileNode *node = new FileNode(this, zip_get_name(m_zip, i, 0), i);
        (void) node;
    }
}

