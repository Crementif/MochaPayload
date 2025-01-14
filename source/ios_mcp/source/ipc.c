/***************************************************************************
 * Copyright (C) 2016
 * by Dimok
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any
 * damages arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any
 * purpose, including commercial applications, and to alter it and
 * redistribute it freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you
 * must not claim that you wrote the original software. If you use
 * this software in a product, an acknowledgment in the product
 * documentation would be appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and
 * must not be misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source
 * distribution.
 ***************************************************************************/
#include "../../common/kernel_commands.h"
#include "fsa.h"
#include "imports.h"
#include "logger.h"
#include "svc.h"
#include "wupserver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IOSUHAX_MAGIC_WORD           0x4E696365
#define IOS_ERROR_UNKNOWN_VALUE      0xFFFFFFD6
#define IOS_ERROR_INVALID_ARG        0xFFFFFFE3
#define IOS_ERROR_INVALID_SIZE       0xFFFFFFE9
#define IOS_ERROR_UNKNOWN            0xFFFFFFF7
#define IOS_ERROR_NOEXISTS           0xFFFFFFFA

#define IOCTL_MEM_WRITE              0x00
#define IOCTL_MEM_READ               0x01
#define IOCTL_SVC                    0x02
#define IOCTL_KILL_SERVER            0x03
#define IOCTL_MEMCPY                 0x04
#define IOCTL_REPEATED_WRITE         0x05
#define IOCTL_KERN_READ32            0x06
#define IOCTL_KERN_WRITE32           0x07
#define IOCTL_READ_OTP               0x08

#define IOCTL_FSA_OPEN               0x40
#define IOCTL_FSA_CLOSE              0x41
#define IOCTL_FSA_MOUNT              0x42
#define IOCTL_FSA_UNMOUNT            0x43
#define IOCTL_FSA_GETINFO            0x44
#define IOCTL_FSA_OPENDIR            0x45
#define IOCTL_FSA_READDIR            0x46
#define IOCTL_FSA_CLOSEDIR           0x47
#define IOCTL_FSA_MAKEDIR            0x48
#define IOCTL_FSA_OPENFILE           0x49
#define IOCTL_FSA_READFILE           0x4A
#define IOCTL_FSA_WRITEFILE          0x4B
#define IOCTL_FSA_GETSTATFILE        0x4C
#define IOCTL_FSA_CLOSEFILE          0x4D
#define IOCTL_FSA_SETPOSFILE         0x4E
#define IOCTL_FSA_GETSTAT            0x4F
#define IOCTL_FSA_REMOVE             0x50
#define IOCTL_FSA_REWINDDIR          0x51
#define IOCTL_FSA_CHDIR              0x52
#define IOCTL_FSA_RENAME             0x53
#define IOCTL_FSA_RAW_OPEN           0x54
#define IOCTL_FSA_RAW_READ           0x55
#define IOCTL_FSA_RAW_WRITE          0x56
#define IOCTL_FSA_RAW_CLOSE          0x57
#define IOCTL_FSA_CHANGEMODE         0x58
#define IOCTL_FSA_FLUSHVOLUME        0x59
#define IOCTL_CHECK_IF_IOSUHAX       0x5B

// Extended mode
#define IOCTL_FSA_CHANGEOWNER        0x5C
#define IOCTL_FSA_OPENFILEEX         0x5D
#define IOCTL_FSA_READFILEWITHPOS    0x5E
#define IOCTL_FSA_WRITEFILEWITHPOS   0x5F
#define IOCTL_FSA_APPENDFILE         0x60
#define IOCTL_FSA_APPENDFILEEX       0x61
#define IOCTL_FSA_FLUSHFILE          0x62
#define IOCTL_FSA_TRUNCATEFILE       0x63
#define IOCTL_FSA_GETPOSFILE         0x64
#define IOCTL_FSA_ISEOF              0x65
#define IOCTL_FSA_ROLLBACKVOLUME     0x66
#define IOCTL_FSA_GETCWD             0x67
#define IOCTL_FSA_MAKEQUOTA          0x68
#define IOCTL_FSA_FLUSHQUOTA         0x69
#define IOCTL_FSA_ROLLBACKQUOTA      0x6A
#define IOCTL_FSA_ROLLBACKQUOTAFORCE 0x6B
#define IOCTL_FSA_CHANGEMODEEX       0x6C
#define IOCTL_FSA_REGISTERFLUSHQUOTA 0x6D
#define IOCTL_FSA_FLUSHMULTIQUOTA    0x6E

// Old bindings that are now renamed
#define IOCTL_FSA_GETDEVICEINFO      IOCTL_FSA_GETINFO
#define IOCTL_FSA_SETFILEPOS         IOCTL_FSA_SETPOSFILE

static int ipcNodeKilled;
static u8 threadStack[0x1000] __attribute__((aligned(0x20)));

static int ipc_ioctl(ipcmessage *message) {
    int res = 0;

    switch (message->ioctl.command) {
        case IOCTL_MEM_WRITE: {
            if (message->ioctl.length_in < 4) {
                res = IOS_ERROR_INVALID_SIZE;
            } else {
                memcpy((void *) message->ioctl.buffer_in[0], message->ioctl.buffer_in + 1, message->ioctl.length_in - 4);
            }
            break;
        }
        case IOCTL_MEM_READ: {
            if (message->ioctl.length_in < 4) {
                res = IOS_ERROR_INVALID_SIZE;
            } else {
                memcpy(message->ioctl.buffer_io, (void *) message->ioctl.buffer_in[0], message->ioctl.length_io);
            }
            break;
        }
        case IOCTL_SVC: {
            if ((message->ioctl.length_in < 4) || (message->ioctl.length_io < 4)) {
                res = IOS_ERROR_INVALID_SIZE;
            } else {
                int svc_id         = message->ioctl.buffer_in[0];
                int size_arguments = message->ioctl.length_in - 4;

                u32 arguments[8];
                memset(arguments, 0x00, sizeof(arguments));
                memcpy(arguments, message->ioctl.buffer_in + 1, (size_arguments < 8 * 4) ? size_arguments : (8 * 4));

                // return error code as data
                message->ioctl.buffer_io[0] = ((int (*const)(u32, u32, u32, u32, u32, u32, u32, u32))(MCP_SVC_BASE + svc_id * 8))(arguments[0], arguments[1], arguments[2], arguments[3], arguments[4],
                                                                                                                                  arguments[5], arguments[6], arguments[7]);
            }
            break;
        }
        case IOCTL_KILL_SERVER: {
            ipcNodeKilled = 1;
            wupserver_deinit();
            break;
        }
        case IOCTL_MEMCPY: {
            if (message->ioctl.length_in < 12) {
                res = IOS_ERROR_INVALID_SIZE;
            } else {
                memcpy((void *) message->ioctl.buffer_in[0], (void *) message->ioctl.buffer_in[1], message->ioctl.buffer_in[2]);
            }
            break;
        }
        case IOCTL_REPEATED_WRITE: {
            if (message->ioctl.length_in < 12) {
                res = IOS_ERROR_INVALID_SIZE;
            } else {
                u32 *dst         = (u32 *) message->ioctl.buffer_in[0];
                u32 *cache_range = (u32 *) (message->ioctl.buffer_in[0] & ~0xFF);
                u32 value        = message->ioctl.buffer_in[1];
                u32 n            = message->ioctl.buffer_in[2];

                u32 old = *dst;
                int i;
                for (i = 0; i < n; i++) {
                    if (*dst != old) {
                        if (*dst == 0x0) old = *dst;
                        else {
                            *dst = value;
                            svcFlushDCache(cache_range, 0x100);
                            break;
                        }
                    } else {
                        svcInvalidateDCache(cache_range, 0x100);
                        usleep(50);
                    }
                }
            }
            break;
        }
        case IOCTL_KERN_READ32: {
            if ((message->ioctl.length_in < 4) || (message->ioctl.length_io < 4)) {
                res = IOS_ERROR_INVALID_SIZE;
            } else {
                for (u32 i = 0; i < (message->ioctl.length_io / 4); i++) {
                    message->ioctl.buffer_io[i] = svcCustomKernelCommand(KERNEL_READ32, message->ioctl.buffer_in[0] + i * 4);
                }
            }
            break;
        }
        case IOCTL_KERN_WRITE32: {
            if ((message->ioctl.length_in < 8)) {
                res = IOS_ERROR_INVALID_SIZE;
            } else {
                svcCustomKernelCommand(KERNEL_WRITE32, message->ioctl.buffer_in[0], message->ioctl.buffer_in[1]);
            }
            break;
        }
        case IOCTL_READ_OTP: {
            if ((message->ioctl.length_io < 0x400)) {
                res = IOS_ERROR_INVALID_SIZE;
            } else {
                svcCustomKernelCommand(KERNEL_READ_OTP, message->ioctl.buffer_io);
            }
            break;
        }
            //!--------------------------------------------------------------------------------------------------------------
            //! FSA handles for better performance
            //!--------------------------------------------------------------------------------------------------------------
            //! TODO: add checks for i/o buffer length
        case IOCTL_FSA_OPEN: {
            message->ioctl.buffer_io[0] = svcOpen("/dev/fsa", 0);
            break;
        }
        case IOCTL_FSA_CLOSE: {
            int fd                      = message->ioctl.buffer_in[0];
            message->ioctl.buffer_io[0] = svcClose(fd);
            break;
        }
        case IOCTL_FSA_MOUNT: {
            int fd             = message->ioctl.buffer_in[0];
            char *device_path  = ((char *) message->ioctl.buffer_in) + message->ioctl.buffer_in[1];
            char *volume_path  = ((char *) message->ioctl.buffer_in) + message->ioctl.buffer_in[2];
            u32 flags          = message->ioctl.buffer_in[3];
            char *arg_string   = (message->ioctl.buffer_in[4] > 0) ? (((char *) message->ioctl.buffer_in) + message->ioctl.buffer_in[4]) : 0;
            int arg_string_len = message->ioctl.buffer_in[5];

            message->ioctl.buffer_io[0] = FSA_Mount(fd, device_path, volume_path, flags, arg_string, arg_string_len);
            break;
        }
        case IOCTL_FSA_UNMOUNT: {
            int fd            = message->ioctl.buffer_in[0];
            char *device_path = ((char *) message->ioctl.buffer_in) + message->ioctl.buffer_in[1];
            u32 flags         = message->ioctl.buffer_in[2];

            message->ioctl.buffer_io[0] = FSA_Unmount(fd, device_path, flags);
            break;
        }
        case IOCTL_FSA_GETINFO: {
            int fd            = message->ioctl.buffer_in[0];
            char *device_path = ((char *) message->ioctl.buffer_in) + message->ioctl.buffer_in[1];
            int type          = message->ioctl.buffer_in[2];

            message->ioctl.buffer_io[0] = FSA_GetInfo(fd, device_path, type, message->ioctl.buffer_io + 1);
            break;
        }
        case IOCTL_FSA_OPENDIR: {
            int fd     = message->ioctl.buffer_in[0];
            char *path = ((char *) message->ioctl.buffer_in) + message->ioctl.buffer_in[1];

            message->ioctl.buffer_io[0] = FSA_OpenDir(fd, path, (int *) message->ioctl.buffer_io + 1);
            break;
        }
        case IOCTL_FSA_READDIR: {
            int fd     = message->ioctl.buffer_in[0];
            int handle = message->ioctl.buffer_in[1];

            message->ioctl.buffer_io[0] = FSA_ReadDir(fd, handle, (FSDirectory *) (message->ioctl.buffer_io + 1));
            break;
        }
        case IOCTL_FSA_CLOSEDIR: {
            int fd     = message->ioctl.buffer_in[0];
            int handle = message->ioctl.buffer_in[1];

            message->ioctl.buffer_io[0] = FSA_CloseDir(fd, handle);
            break;
        }
        case IOCTL_FSA_MAKEDIR: {
            int fd     = message->ioctl.buffer_in[0];
            char *path = ((char *) message->ioctl.buffer_in) + message->ioctl.buffer_in[1];
            u32 flags  = message->ioctl.buffer_in[2];

            message->ioctl.buffer_io[0] = FSA_MakeDir(fd, path, flags);
            break;
        }
        case IOCTL_FSA_OPENFILE: {
            int fd     = message->ioctl.buffer_in[0];
            char *path = ((char *) message->ioctl.buffer_in) + message->ioctl.buffer_in[1];
            char *mode = ((char *) message->ioctl.buffer_in) + message->ioctl.buffer_in[2];

            message->ioctl.buffer_io[0] = FSA_OpenFile(fd, path, mode, (int *) (message->ioctl.buffer_io + 1));
            break;
        }
        case IOCTL_FSA_READFILE: {
            int fd         = message->ioctl.buffer_in[0];
            u32 size       = message->ioctl.buffer_in[1];
            u32 cnt        = message->ioctl.buffer_in[2];
            int fileHandle = message->ioctl.buffer_in[3];
            u32 flags      = message->ioctl.buffer_in[4];

            message->ioctl.buffer_io[0] = FSA_ReadFile(fd, ((u8 *) message->ioctl.buffer_io) + 0x40, size, cnt, fileHandle, flags);
            break;
        }
        case IOCTL_FSA_WRITEFILE: {
            int fd         = message->ioctl.buffer_in[0];
            u32 size       = message->ioctl.buffer_in[1];
            u32 cnt        = message->ioctl.buffer_in[2];
            int fileHandle = message->ioctl.buffer_in[3];
            u32 flags      = message->ioctl.buffer_in[4];

            message->ioctl.buffer_io[0] = FSA_WriteFile(fd, ((u8 *) message->ioctl.buffer_in) + 0x40, size, cnt, fileHandle, flags);
            break;
        }
        case IOCTL_FSA_GETSTATFILE: {
            int fd         = message->ioctl.buffer_in[0];
            int fileHandle = message->ioctl.buffer_in[1];

            message->ioctl.buffer_io[0] = FSA_GetStatFile(fd, fileHandle, (FSStat *) (message->ioctl.buffer_io + 1));
            break;
        }
        case IOCTL_FSA_CLOSEFILE: {
            int fd         = message->ioctl.buffer_in[0];
            int fileHandle = message->ioctl.buffer_in[1];

            message->ioctl.buffer_io[0] = FSA_CloseFile(fd, fileHandle);
            break;
        }
        case IOCTL_FSA_SETPOSFILE: {
            int fd         = message->ioctl.buffer_in[0];
            int fileHandle = message->ioctl.buffer_in[1];
            u32 position   = message->ioctl.buffer_in[2];

            message->ioctl.buffer_io[0] = FSA_SetPosFile(fd, fileHandle, position);
            break;
        }
        case IOCTL_FSA_GETSTAT: {
            int fd            = message->ioctl.buffer_in[0];
            char *device_path = ((char *) message->ioctl.buffer_in) + message->ioctl.buffer_in[1];

            message->ioctl.buffer_io[0] = FSA_GetStat(fd, device_path, (FSStat *) (message->ioctl.buffer_io + 1));
            break;
        }
        case IOCTL_FSA_REMOVE: {
            int fd     = message->ioctl.buffer_in[0];
            char *path = ((char *) message->ioctl.buffer_in) + message->ioctl.buffer_in[1];

            message->ioctl.buffer_io[0] = FSA_Remove(fd, path);
            break;
        }
        case IOCTL_FSA_REWINDDIR: {
            int fd     = message->ioctl.buffer_in[0];
            int handle = message->ioctl.buffer_in[1];

            message->ioctl.buffer_io[0] = FSA_RewindDir(fd, handle);
            break;
        }
        case IOCTL_FSA_CHDIR: {
            int fd     = message->ioctl.buffer_in[0];
            char *path = ((char *) message->ioctl.buffer_in) + message->ioctl.buffer_in[1];

            message->ioctl.buffer_io[0] = FSA_ChangeDir(fd, path);
            break;
        }
        case IOCTL_FSA_RENAME: {
            int fd         = message->ioctl.buffer_in[0];
            char *old_path = ((char *) message->ioctl.buffer_in) + message->ioctl.buffer_in[1];
            char *new_path = ((char *) message->ioctl.buffer_in) + message->ioctl.buffer_in[2];

            message->ioctl.buffer_io[0] = FSA_Rename(fd, old_path, new_path);
            break;
        }
        case IOCTL_FSA_RAW_OPEN: {
            int fd     = message->ioctl.buffer_in[0];
            char *path = ((char *) message->ioctl.buffer_in) + message->ioctl.buffer_in[1];

            message->ioctl.buffer_io[0] = FSA_RawOpen(fd, path, (int *) (message->ioctl.buffer_io + 1));
            break;
        }
        case IOCTL_FSA_RAW_READ: {
            int fd            = message->ioctl.buffer_in[0];
            u32 block_size    = message->ioctl.buffer_in[1];
            u32 cnt           = message->ioctl.buffer_in[2];
            u64 sector_offset = ((u64) message->ioctl.buffer_in[3] << 32ULL) | message->ioctl.buffer_in[4];
            int deviceHandle  = message->ioctl.buffer_in[5];

            message->ioctl.buffer_io[0] = FSA_RawRead(fd, ((u8 *) message->ioctl.buffer_io) + 0x40, block_size, cnt, sector_offset, deviceHandle);
            break;
        }
        case IOCTL_FSA_RAW_WRITE: {
            int fd            = message->ioctl.buffer_in[0];
            u32 block_size    = message->ioctl.buffer_in[1];
            u32 cnt           = message->ioctl.buffer_in[2];
            u64 sector_offset = ((u64) message->ioctl.buffer_in[3] << 32ULL) | message->ioctl.buffer_in[4];
            int deviceHandle  = message->ioctl.buffer_in[5];

            message->ioctl.buffer_io[0] = FSA_RawWrite(fd, ((u8 *) message->ioctl.buffer_in) + 0x40, block_size, cnt, sector_offset, deviceHandle);
            break;
        }
        case IOCTL_FSA_RAW_CLOSE: {
            int fd           = message->ioctl.buffer_in[0];
            int deviceHandle = message->ioctl.buffer_in[1];

            message->ioctl.buffer_io[0] = FSA_RawClose(fd, deviceHandle);
            break;
        }
        case IOCTL_FSA_CHANGEMODE: {
            int fd     = message->ioctl.buffer_in[0];
            char *path = ((char *) message->ioctl.buffer_in) + message->ioctl.buffer_in[1];
            int mode   = message->ioctl.buffer_in[2];

            message->ioctl.buffer_io[0] = FSA_ChangeMode(fd, path, mode);
            break;
        }
        case IOCTL_FSA_FLUSHVOLUME: {
            int fd            = message->ioctl.buffer_in[0];
            char *device_path = ((char *) message->ioctl.buffer_in) + message->ioctl.buffer_in[1];

            message->ioctl.buffer_io[0] = FSA_FlushVolume(fd, device_path);
            break;
        }
        case IOCTL_CHECK_IF_IOSUHAX: {
            message->ioctl.buffer_io[0] = IOSUHAX_MAGIC_WORD;
            break;
        }

        case IOCTL_FSA_CHANGEOWNER: {
            int fd     = message->ioctl.buffer_in[0];
            char *path = ((char *) message->ioctl.buffer_in) + message->ioctl.buffer_in[1];
            u32 owner  = message->ioctl.buffer_in[2];
            u32 group  = message->ioctl.buffer_in[3];

            message->ioctl.buffer_io[0] = FSA_ChangeOwner(fd, path, owner, group);
            break;
        }
        case IOCTL_FSA_OPENFILEEX: {
            int fd                = message->ioctl.buffer_in[0];
            char *path            = ((char *) message->ioctl.buffer_in) + message->ioctl.buffer_in[1];
            char *mode            = ((char *) message->ioctl.buffer_in) + message->ioctl.buffer_in[2];
            u32 flags             = message->ioctl.buffer_in[3];
            int create_mode       = message->ioctl.buffer_in[4];
            u32 create_alloc_size = message->ioctl.buffer_in[5];

            message->ioctl.buffer_io[0] = FSA_OpenFileEx(fd, path, mode, flags, create_mode, create_alloc_size, (int *) (message->ioctl.buffer_io + 1));
            break;
        }
        case IOCTL_FSA_READFILEWITHPOS: {
            int fd         = message->ioctl.buffer_in[0];
            u32 size       = message->ioctl.buffer_in[1];
            u32 cnt        = message->ioctl.buffer_in[2];
            u32 pos        = message->ioctl.buffer_in[3];
            int fileHandle = message->ioctl.buffer_in[4];
            u32 flags      = message->ioctl.buffer_in[5];

            message->ioctl.buffer_io[0] = FSA_ReadFileWithPos(fd, ((u8 *) message->ioctl.buffer_io) + 0x40, size, cnt, pos, fileHandle, flags);
            break;
        }
        case IOCTL_FSA_WRITEFILEWITHPOS: {
            int fd         = message->ioctl.buffer_in[0];
            u32 size       = message->ioctl.buffer_in[1];
            u32 cnt        = message->ioctl.buffer_in[2];
            u32 pos        = message->ioctl.buffer_in[3];
            int fileHandle = message->ioctl.buffer_in[4];
            u32 flags      = message->ioctl.buffer_in[5];

            message->ioctl.buffer_io[0] = FSA_WriteFileWithPos(fd, ((u8 *) message->ioctl.buffer_in) + 0x40, size, cnt, pos, fileHandle, flags);
            break;
        }
        case IOCTL_FSA_APPENDFILE: {
            int fd         = message->ioctl.buffer_in[0];
            u32 size       = message->ioctl.buffer_in[1];
            u32 cnt        = message->ioctl.buffer_in[2];
            int fileHandle = message->ioctl.buffer_in[3];

            message->ioctl.buffer_io[0] = FSA_AppendFile(fd, size, cnt, fileHandle);
            break;
        }
        case IOCTL_FSA_APPENDFILEEX: {
            int fd         = message->ioctl.buffer_in[0];
            u32 size       = message->ioctl.buffer_in[1];
            u32 cnt        = message->ioctl.buffer_in[2];
            int fileHandle = message->ioctl.buffer_in[3];
            u32 flags      = message->ioctl.buffer_in[4];

            message->ioctl.buffer_io[0] = FSA_AppendFileEx(fd, size, cnt, fileHandle, flags);
            break;
        }
        case IOCTL_FSA_FLUSHFILE: {
            int fd         = message->ioctl.buffer_in[0];
            int fileHandle = message->ioctl.buffer_in[1];

            message->ioctl.buffer_io[0] = FSA_FlushFile(fd, fileHandle);
            break;
        }
        case IOCTL_FSA_TRUNCATEFILE: {
            int fd         = message->ioctl.buffer_in[0];
            int fileHandle = message->ioctl.buffer_in[1];

            message->ioctl.buffer_io[0] = FSA_TruncateFile(fd, fileHandle);
            break;
        }
        case IOCTL_FSA_GETPOSFILE: {
            int fd         = message->ioctl.buffer_in[0];
            int fileHandle = message->ioctl.buffer_in[1];

            message->ioctl.buffer_io[0] = FSA_GetPosFile(fd, fileHandle, (u32 *) (message->ioctl.buffer_io + 1));
            break;
        }
        case IOCTL_FSA_ISEOF: {
            int fd         = message->ioctl.buffer_in[0];
            int fileHandle = message->ioctl.buffer_in[1];

            message->ioctl.buffer_io[0] = FSA_IsEof(fd, fileHandle);
            break;
        }
        case IOCTL_FSA_ROLLBACKVOLUME: {
            int fd            = message->ioctl.buffer_in[0];
            char *device_path = ((char *) message->ioctl.buffer_in) + message->ioctl.buffer_in[1];

            message->ioctl.buffer_io[0] = FSA_RollbackVolume(fd, device_path);
            break;
        }
        case IOCTL_FSA_GETCWD: {
            int fd          = message->ioctl.buffer_in[0];
            int output_size = message->ioctl.buffer_in[1];

            message->ioctl.buffer_io[0] = FSA_GetCwd(fd, (char *) (message->ioctl.buffer_io + 1), output_size);
            break;
        }
        case IOCTL_FSA_MAKEQUOTA: {
            int fd     = message->ioctl.buffer_in[0];
            char *path = ((char *) message->ioctl.buffer_in) + message->ioctl.buffer_in[1];
            u32 flags  = message->ioctl.buffer_in[2];
            u64 size   = ((u64) message->ioctl.buffer_in[3] << 32ULL) | message->ioctl.buffer_in[4];

            message->ioctl.buffer_io[0] = FSA_MakeQuota(fd, path, flags, size);
            break;
        }
        case IOCTL_FSA_FLUSHQUOTA: {
            int fd     = message->ioctl.buffer_in[0];
            char *path = ((char *) message->ioctl.buffer_in) + message->ioctl.buffer_in[1];

            message->ioctl.buffer_io[0] = FSA_FlushQuota(fd, path);
            break;
        }
        case IOCTL_FSA_ROLLBACKQUOTA: {
            int fd     = message->ioctl.buffer_in[0];
            char *path = ((char *) message->ioctl.buffer_in) + message->ioctl.buffer_in[1];

            message->ioctl.buffer_io[0] = FSA_RollbackQuota(fd, path);
            break;
        }
        case IOCTL_FSA_ROLLBACKQUOTAFORCE: {
            int fd     = message->ioctl.buffer_in[0];
            char *path = ((char *) message->ioctl.buffer_in) + message->ioctl.buffer_in[1];

            message->ioctl.buffer_io[0] = FSA_RollbackQuotaForce(fd, path);
            break;
        }
        case IOCTL_FSA_CHANGEMODEEX: {
            int fd     = message->ioctl.buffer_in[0];
            char *path = ((char *) message->ioctl.buffer_in) + message->ioctl.buffer_in[1];
            int mode   = message->ioctl.buffer_in[2];
            int mask   = message->ioctl.buffer_in[3];

            message->ioctl.buffer_io[0] = FSA_ChangeModeEx(fd, path, mode, mask);
            break;
        }
        case IOCTL_FSA_REGISTERFLUSHQUOTA: {
            int fd     = message->ioctl.buffer_in[0];
            char *path = ((char *) message->ioctl.buffer_in) + message->ioctl.buffer_in[1];

            message->ioctl.buffer_io[0] = FSA_RegisterFlushQuota(fd, path);
            break;
        }
        case IOCTL_FSA_FLUSHMULTIQUOTA: {
            int fd     = message->ioctl.buffer_in[0];
            char *path = ((char *) message->ioctl.buffer_in) + message->ioctl.buffer_in[1];

            message->ioctl.buffer_io[0] = FSA_FlushMultiQuota(fd, path);
            break;
        }

        default:
            res = IOS_ERROR_INVALID_ARG;
            break;
    }

    return res;
}

static int ipc_thread(void *arg) {
    int res;
    ipcmessage *message;
    u32 messageQueue[0x10];

    int queueId = svcCreateMessageQueue(messageQueue, sizeof(messageQueue) / 4);

    if (svcRegisterResourceManager("/dev/iosuhax", queueId) == 0) {
        while (!ipcNodeKilled) {
            res = svcReceiveMessage(queueId, &message, 0);
            if (res < 0) {
                usleep(10000);
                continue;
            }

            switch (message->command) {
                case IOS_OPEN: {
                    log_printf("IOS_OPEN\n");
                    res = 0;
                    break;
                }
                case IOS_CLOSE: {
                    log_printf("IOS_CLOSE\n");
                    res = 0;
                    break;
                }
                case IOS_IOCTL: {
                    log_printf("IOS_IOCTL\n");
                    res = ipc_ioctl(message);
                    break;
                }
                case IOS_IOCTLV: {
                    log_printf("IOS_IOCTLV\n");
                    res = 0;
                    break;
                }
                default: {
                    log_printf("unexpected command 0x%X\n", message->command);
                    res = IOS_ERROR_UNKNOWN_VALUE;
                    break;
                }
            }

            svcResourceReply(message, res);
        }
    }

    svcDestroyMessageQueue(queueId);
    return 0;
}

void ipc_init(void) {
    ipcNodeKilled = 0;

    int threadId = svcCreateThread(ipc_thread, 0, (u32 *) (threadStack + sizeof(threadStack)), sizeof(threadStack), 0x78, 1);
    if (threadId >= 0)
        svcStartThread(threadId);
}

void ipc_deinit(void) {
    int fd = svcOpen("/dev/iosuhax", 0);
    if (fd >= 0) {
        int dummy = 0;
        svcIoctl(fd, IOCTL_KILL_SERVER, &dummy, sizeof(dummy), &dummy, sizeof(dummy));
        svcClose(fd);
    }
}
