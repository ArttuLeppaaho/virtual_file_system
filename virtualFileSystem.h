#ifndef VIRTUALFILESYSTEM_H
#define VIRTUALFILESYSTEM_H

// This module provides an interface for creating and accessing virtual files
// and handles file metadata to keep track of open files, file lengths and file
// names.

#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>

// Windows compatibility
#ifdef _MSC_VER
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#else
#include <unistd.h>
#endif

typedef int file_descriptor;

file_descriptor open_virtual(const char* path, int flags);
void close_virtual(file_descriptor file_descriptor);

int unlink_virtual(const char* path);

int mkdir_virtual(const char* path);
int rmdir_virtual(const char* path);

ssize_t read_virtual(file_descriptor file_descriptor, void* buffer, size_t n_bytes);
ssize_t write_virtual(file_descriptor file_descriptor, void* buffer, size_t n_bytes);
off_t seek_virtual(file_descriptor file_descriptor, off_t offset, int whence);

#endif // VIRTUALFILESYSTEM_H
