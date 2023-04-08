#ifndef VIRTUALSTORAGE_H
#define VIRTUALSTORAGE_H

// This module implements a block-based storage system that is stored in a
// single file. The storage system is used by allocating regions which can
// then be written to and read from like byte streams. The module underneath
// handles allocating and freeing memory blocks as necessary and writing the
// data to the disk. Only one region is active at a time, and it must be
// switched manually to access another region.

#include <stdbool.h>
#include <sys/types.h>

#define INVALID_REGION 65535

typedef unsigned short storage_region;

int storage_initialize();
bool storage_initialized();

storage_region storage_allocate_region();
int storage_free_region(storage_region region);

int storage_jump_to_region(storage_region region);

size_t storage_read_in_region(void* buffer, size_t n_bytes);
size_t storage_write_in_region(void* buffer, size_t n_bytes);
size_t storage_seek_in_region(off_t offset);

#endif // VIRTUALSTORAGE_H
