////////////////////////////////////////////////////////////////////////////
//  Copyright (C) 2008-2010 by Alexander Galanin                          //
//  al@galanin.nnov.ru                                                    //
//  http://galanin.nnov.ru/~al                                            //
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

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "bigBuffer.h"
#include "fileNode.h"

BigBuffer::BigBuffer(): len(0) {
}

BigBuffer::BigBuffer(struct zip *z, int nodeId, ssize_t length): len(length) {
    struct zip_file *zf = zip_fopen_index(z, nodeId, 0);
    char *buf = (char*)malloc(chunkSize);
    if (buf == NULL) {
        throw std::bad_alloc();
    }
    ssize_t nr;
    while ((nr = zip_fread(zf, buf, chunkSize)) > 0) {
        chunks.push_back(buf);
        buf = (char*)malloc(chunkSize);
        if (buf == NULL) {
            for (chunks_t::iterator i = chunks.begin(); i != chunks.end(); ++i) {
                free(*i);
            }
            throw std::bad_alloc();
        }
    }
    free(buf);
    if (zip_fclose(zf)) {
        throw std::exception();
    }
}

BigBuffer::~BigBuffer() {
    for (chunks_t::iterator i = chunks.begin(); i != chunks.end(); ++i) {
        if (*i != NULL) {
            free(*i);
        }
    }
}

int BigBuffer::read(char *buf, size_t size, offset_t offset) const {
    if (offset > len) {
        return -EINVAL;
    }
    int chunk = offset / chunkSize;
    int pos = offset % chunkSize;
    int nread = 0;
    if (size > unsigned(len - offset)) {
        size = len - offset;
    }
    while (size > 0) {
        size_t r = chunkSize - pos;
        if (r > size) {
            r = size;
        }
        if (chunks[chunk] != NULL) {
            memcpy(buf, chunks[chunk] + pos, r);
        } else {
            memset(buf, 0, r);
        }
        size -= r;
        nread += r;
        buf += r;
        ++chunk;
        pos = 0;
    }
    return nread;
}

int BigBuffer::write(const char *buf, size_t size, offset_t offset) {
    int chunk = offset / chunkSize;
    int pos = offset % chunkSize;
    int nwritten = 0;
    for (unsigned int i = chunks.size(); i <= (offset + size) / chunkSize; ++i) {
        chunks.push_back(NULL);
    }
    if (len < offset || size > unsigned(len - offset)) {
        len = size + offset;
    }
    while (size > 0) {
        size_t w = chunkSize - pos;
        if (w > size) {
            w = size;
        }
        if (chunks[chunk] == NULL) {
            chunks[chunk] = (char*)malloc(chunkSize);
            if (!chunks[chunk]) {
                return -EIO;
            }
        }
        memcpy(chunks[chunk] + pos, buf, w);
        size -= w;
        nwritten += w;
        buf += w;
        ++ chunk;
        pos = 0;
    }
    return nwritten;
}

/**
 * 1. Free chunks after offset
 * 2. Resize chunks vector to a new size
 * 3. Fill data block that made readable by resize with zeroes
 */
int BigBuffer::truncate(offset_t offset) {
    if (offset < len) {
        for (unsigned int i = (offset + chunkSize - 1)/chunkSize; i < chunks.size(); ++i) {
            if (chunks[i] != NULL) {
                free(chunks[i]);
            }
        }
    }

    chunks.resize((offset + chunkSize - 1)/chunkSize, NULL);

    if (offset > len) {
        // Fill end of last non-empty chunk with zeroes
        unsigned int pos = len / chunkSize;
        if (chunks[pos] != NULL) {
            offset_t end = offset % chunkSize;
            if (offset / chunkSize > pos) {
                end = chunkSize - 1;
            }
            offset_t start = len % chunkSize;
            memset(chunks[pos] + start, 0, end - start + 1);
        }
    }

    len = offset;
    return 0;
}

ssize_t BigBuffer::zipUserFunctionCallback(void *state, void *data, size_t len, enum zip_source_cmd cmd) {
    CallBackStruct *b = (CallBackStruct*)state;
    switch (cmd) {
        case ZIP_SOURCE_OPEN: {
            b->pos = 0;
            return 0;
        }
        case ZIP_SOURCE_READ: {
            int r = b->buf->read((char*)data, len, b->pos);
            b->pos += r;
            return r;
        }
        case ZIP_SOURCE_STAT: {
            struct zip_stat *st = (struct zip_stat*)data;
            zip_stat_init(st);
            st->size = b->buf->len;
            st->mtime = b->fileNode->stat.mtime;
            return sizeof(struct zip_stat);
        }
        case ZIP_SOURCE_FREE: {
            delete b;
            return 0;
        }
        default: {
            return 0;
        }
    }
}

int BigBuffer::saveToZip(const FileNode *fileNode, struct zip *z, const char *fname, bool newFile, int index) {
    struct zip_source *s;
    struct CallBackStruct *cbs = new CallBackStruct();
    cbs->buf = this;
    cbs->fileNode = fileNode;
    if ((s=zip_source_function(z, zipUserFunctionCallback, cbs)) == NULL) {
        return -ENOMEM;
    }
    if ((newFile && zip_add(z, fname, s) < 0) || (!newFile && zip_replace(z, index, s) < 0)) {
        zip_source_free(s);
        return -ENOMEM;
    }
    return 0;
}
