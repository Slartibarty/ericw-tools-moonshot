/*  Copyright (C) 1996-1997  Id Software, Inc.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

    See file, 'COPYING', for details.
*/

#include <common/cmdlib.hh>
#include <common/log.hh>
#include <common/threads.hh>

#include <sys/types.h>
#include <sys/stat.h>

#ifdef WIN32
#include <direct.h>
#include <windows.h>
#endif

#ifdef LINUX
#include <sys/time.h>
#include <unistd.h>
#include <cstring>
#endif

#include <cstdint>

#include <string>

/* set these before calling CheckParm */
int myargc;
char **myargv;

char com_token[1024];
bool com_eof;

/*
 * =================
 * Error
 * For abnormal program terminations
 * =================
 */
[[noreturn]] void Error(const char *error)
{
    /* Using lockless prints so we can error out while holding the lock */
    InterruptThreadProgress__();
    LogPrintLocked("************ ERROR ************\n{}\n", error);
    exit(1);
}

void // mxd
string_replaceall(std::string &str, const std::string &from, const std::string &to)
{
    if (from.empty())
        return;
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // In case 'to' contains 'from', like replacing 'x' with 'yx'
    }
}

bool // mxd
string_iequals(const std::string &a, const std::string &b)
{
    size_t sz = a.size();
    if (b.size() != sz)
        return false;
    for (size_t i = 0; i < sz; ++i)
        if (tolower(a[i]) != tolower(b[i]))
            return false;
    return true;
}

std::filesystem::path qdir, gamedir, basedir;

/** It's possible to compile quake 1/hexen 2 maps without a qdir */
inline void ClearQdir()
{
    qdir.clear();
    gamedir.clear();
    basedir.clear();
}

constexpr const char *MAPS_FOLDER = "maps";

// mxd. Expects the path to contain "maps" folder
void SetQdirFromPath(const std::string &basedirname, std::filesystem::path path)
{
    // expand canonicals, and fetch parent of source file
    // (maps/source.map -> C:/Quake/ID1/maps/)
    path = std::filesystem::canonical(path).parent_path();

    // make sure we're maps/
    if (path.filename() != "maps") {
        FLogPrint("WARNING: '{}' is not directly inside '{}'\n", path, MAPS_FOLDER);
        return;
    }

    // set gamedir (it should be "above" the source)
    // (C:/Quake/ID1/maps/ -> C:/Quake/ID1/)
    gamedir = path.parent_path();
    LogPrint("INFO: gamedir: '{}'\n", gamedir);

    // set qdir (it should be above gamedir)
    // (C:/Quake/ID1/ -> C:/Quake/)
    qdir = gamedir.parent_path();
    LogPrint("INFO: qdir: '{}'\n", qdir);

    // Set base dir and make sure it exists
    basedir = qdir / basedirname;

    if (!std::filesystem::exists(basedir)) {
        FLogPrint("WARNING: failed to find '{}' in '{}'\n", basedirname, qdir);
        ClearQdir();
        return;
    }
}

qfile_t SafeOpenWrite(const std::filesystem::path &filename)
{
    FILE *f;

#ifdef _WIN32
    f = _wfopen(filename.c_str(), L"wb");
#else
    f = fopen(filename.string().c_str(), "wb");
#endif

    if (!f)
        FError("Error opening {}: {}", filename, strerror(errno));

    return {f, fclose};
}

qfile_t SafeOpenRead(const std::filesystem::path &filename, bool must_exist)
{
    FILE *f;

#ifdef _WIN32
    f = _wfopen(filename.c_str(), L"rb");
#else
    f = fopen(filename.string().c_str(), "rb");
#endif

    if (!f) {
        if (must_exist)
            FError("Error opening {}: {}", filename, strerror(errno));

        return {nullptr, nullptr};
    }

    return {f, fclose};
}

size_t SafeRead(const qfile_t &f, void *buffer, size_t count)
{
    if (fread(buffer, 1, count, f.get()) != (size_t)count)
        FError("File read failure");

    return count;
}

size_t SafeWrite(const qfile_t &f, const void *buffer, size_t count)
{
    if (fwrite(buffer, 1, count, f.get()) != (size_t)count)
        FError("File write failure");

    return count;
}

void SafeSeek(const qfile_t &f, long offset, int32_t origin)
{
    fseek(f.get(), offset, origin);
}

long SafeTell(const qfile_t &f)
{
    return ftell(f.get());
}

struct pakheader_t
{
    char magic[4];
    unsigned int tableofs;
    unsigned int numfiles;
};

struct pakfile_t
{
    char name[56];
    unsigned int offset;
    unsigned int length;
};

/*
 * ==============
 * LoadFilePak
 * reads a file directly out of a pak, to make re-lighting friendlier
 * writes to the filename, stripping the pak part of the name
 * ==============
 */
long LoadFilePak(std::filesystem::path &filename, void *destptr)
{
    // check if we have a .pak file in this path
    for (auto p = filename.parent_path(); !p.empty() && p != p.root_path(); p = p.parent_path()) {
        if (p.extension() == ".pak") {
            qfile_t file = SafeOpenRead(p);

            // false positive
            if (!file)
                continue;

            // got one; calculate the relative remaining path
            auto innerfile = filename.lexically_relative(p);

            uint8_t **bufferptr = static_cast<uint8_t **>(destptr);
            pakheader_t header;
            long length = -1;
            SafeRead(file, &header, sizeof(header));

            header.numfiles = LittleLong(header.numfiles) / sizeof(pakfile_t);
            header.tableofs = LittleLong(header.tableofs);

            if (!strncmp(header.magic, "PACK", 4)) {
                pakfile_t *files = new pakfile_t[header.numfiles];

                SafeSeek(file, header.tableofs, SEEK_SET);
                SafeRead(file, files, header.numfiles * sizeof(*files));

                for (uint32_t i = 0; i < header.numfiles; i++) {
                    if (innerfile == files[i].name) {
                        SafeSeek(file, files[i].offset, SEEK_SET);
                        *bufferptr = new uint8_t[files[i].length + 1];
                        SafeRead(file, *bufferptr, files[i].length);
                        length = files[i].length;
                        break;
                    }
                }
                delete[] files;
            }

            if (length < 0)
                FError("Unable to find '{}' inside '{}'", innerfile, filename);

            filename = innerfile;
            return length;
        }
    }

    // not in a pak, so load it normally
    return LoadFile(filename, destptr);
}

/*
 * ===========
 * filelength
 * ===========
 */
static long Sys_FileLength(const qfile_t &f)
{
    long pos = ftell(f.get());
    fseek(f.get(), 0, SEEK_END);
    long end = ftell(f.get());
    fseek(f.get(), pos, SEEK_SET);

    return end;
}
/*
 * ==============
 * LoadFile
 * ==============
 */
long LoadFile(const std::filesystem::path &filename, void *destptr)
{
    uint8_t **bufferptr = static_cast<uint8_t **>(destptr);

    qfile_t file = SafeOpenRead(filename);

    long length = Sys_FileLength(file);

    uint8_t *buffer = *bufferptr = new uint8_t[length + 1];

    if (!buffer)
        FError("allocation of {} bytes failed.", length);

    SafeRead(file, buffer, length);

    buffer[length] = 0;

    return length;
}
/* ========================================================================= */

/*
 * FIXME: byte swap?
 *
 * this is a 16 bit, non-reflected CRC using the polynomial 0x1021
 * and the initial and final xor values shown below...  in other words, the
 * CCITT standard CRC used by XMODEM
 */

#define CRC_INIT_VALUE 0xffff
#define CRC_XOR_VALUE 0x0000

static unsigned short crctable[256] = {0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7, 0x8108, 0x9129,
    0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef, 0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
    0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de, 0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7,
    0x44a4, 0x5485, 0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d, 0x3653, 0x2672, 0x1611, 0x0630,
    0x76d7, 0x66f6, 0x5695, 0x46b4, 0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc, 0x48c4, 0x58e5,
    0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823, 0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
    0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12, 0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58,
    0xbb3b, 0xab1a, 0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41, 0xedae, 0xfd8f, 0xcdec, 0xddcd,
    0xad2a, 0xbd0b, 0x8d68, 0x9d49, 0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70, 0xff9f, 0xefbe,
    0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78, 0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
    0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067, 0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c,
    0xe37f, 0xf35e, 0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256, 0xb5ea, 0xa5cb, 0x95a8, 0x8589,
    0xf56e, 0xe54f, 0xd52c, 0xc50d, 0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405, 0xa7db, 0xb7fa,
    0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c, 0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab, 0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1,
    0x3882, 0x28a3, 0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a, 0x4a75, 0x5a54, 0x6a37, 0x7a16,
    0x0af1, 0x1ad0, 0x2ab3, 0x3a92, 0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9, 0x7c26, 0x6c07,
    0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1, 0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
    0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0};

void CRC_Init(unsigned short *crcvalue)
{
    *crcvalue = CRC_INIT_VALUE;
}

void CRC_ProcessByte(unsigned short *crcvalue, uint8_t data)
{
    *crcvalue = (*crcvalue << 8) ^ crctable[(*crcvalue >> 8) ^ data];
}

unsigned short CRC_Value(unsigned short crcvalue)
{
    return crcvalue ^ CRC_XOR_VALUE;
}

unsigned short CRC_Block(const unsigned char *start, int count)
{
    unsigned short crc;
    CRC_Init(&crc);
    while (count--)
        crc = (crc << 8) ^ crctable[(crc >> 8) ^ *start++];
    return crc;
}
