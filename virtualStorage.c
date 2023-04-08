// The block-based approach of this module was chosen to avoid moving data
// around when virtual files are created and deleted. If the virtual files
// were stored back-to-back without dividing them between blocks, they would
// have to be moved if they were resized and that could be very inefficient
// if the virtual files are large. Deleting files would also leave unevenly
// sized gaps that might not be easy to reuse.

// The module uses the functions read(), write() and lseek() without checking
// their return values. The reason for this is because as long as the storage
// file is structured correctly, these functions should always succeed when
// used by the module.

#include "virtualStorage.h"
#include <fcntl.h>
#include <stdlib.h>

// Windows compatibility
#ifdef _MSC_VER
#include <BaseTsd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <io.h>
#define S_IRUSR S_IREAD
#define S_IWUSR S_IWRITE
#else
#include <unistd.h>
#define O_BINARY 0
#endif

#define STORAGE_PATH "./virtualStorage"
#define DEFAULT_BLOCK_SIZE 10
#define DEFAULT_BLOCK_COUNT 128
#define FIRST_BLOCK_POSITION 4

typedef unsigned short block_index;

const char BLOCK_NOT_IN_USE_INDICATOR = 0;
const char BLOCK_IN_USE_INDICATOR = 1;
const block_index INVALID_BLOCK = INVALID_REGION;
const char BLOCK_HEADER_SIZE = sizeof(char) + sizeof(block_index) * 2;

typedef struct block_info
{
    bool in_use;
    block_index previous_block;
    block_index next_block;
} block_info;

int storage_file_ = -1;

unsigned short active_block_size_ = 0;
unsigned short active_block_count_ = 0;

block_index current_block_index_ = 0;
size_t current_block_position_ = 0;
size_t current_region_position_ = 0;

block_info current_block_;

void create_storage_file(unsigned short block_size,
                         unsigned short block_count);

block_index allocate_block(block_index previous_block);
void jump_to_block(block_index block);
void read_block_header();

int storage_initialize()
{
    if (storage_initialized())
    {
        return -1;
    }

    // Try to open existing storage file
    storage_file_ = open(STORAGE_PATH, O_RDWR | O_BINARY);

    if (storage_file_ == -1)
    {
        // Opening existing file failed, try to create a new one
        create_storage_file(DEFAULT_BLOCK_SIZE, DEFAULT_BLOCK_COUNT);

        storage_file_ = open(STORAGE_PATH, O_RDWR | O_BINARY);

        if (storage_file_ == -1)
        {
            // Failed to create file, can't continue
            return -1;
        }
    }

    // Read storage file header to update active block size and count
    read(storage_file_, &active_block_size_, sizeof(unsigned short));
    read(storage_file_, &active_block_count_, sizeof(unsigned short));

    return 0;
}

bool storage_initialized()
{
    return storage_file_ != -1;
}

storage_region storage_allocate_region()
{
    if (!storage_initialized())
    {
        return INVALID_REGION;
    }

    // Region IDs are actually just the first block's index in the region
    return allocate_block(INVALID_BLOCK);
}

int storage_free_region(storage_region region)
{
    if (!storage_initialized())
    {
        return -1;
    }

    block_index next_block = region;

    // Free all the blocks in this region
    while (next_block != INVALID_BLOCK)
    {
        jump_to_block(next_block);
        next_block = current_block_.next_block;

        // Overwrite the block's header to mark it as unused: the actual data
        // does not need to be deleted. The block can later be reallocated and
        // filled with other data
        lseek(storage_file_, -BLOCK_HEADER_SIZE, SEEK_CUR);
        write(storage_file_, &BLOCK_NOT_IN_USE_INDICATOR, sizeof(char));
        write(storage_file_, &INVALID_BLOCK, sizeof(block_index));
        write(storage_file_, &INVALID_BLOCK, sizeof(block_index));
    }

    return 0;
}

int storage_jump_to_region(storage_region region)
{
    if (!storage_initialized())
    {
        return -1;
    }

    // Region IDs are actually just the first block's index in the region
    jump_to_block(region);

    current_region_position_ = 0;

    return 0;
}

size_t storage_read_in_region(void* buffer, size_t n_bytes)
{
    if (!storage_initialized())
    {
        return 0;
    }

    size_t read_bytes = 0;

    // While the amount of bytes to read exceeds the amount of bytes remaining
    // in the current block
    while (current_block_position_ + n_bytes - read_bytes >= active_block_size_)
    {
        // Read the rest of the bytes in this block and then jump to the next
        // block
        int bytes_to_read = active_block_size_ - current_block_position_;
        read(storage_file_, (char*)buffer + read_bytes, bytes_to_read);
        read_bytes += bytes_to_read;

        if (current_block_.next_block == INVALID_BLOCK)
        {
            return read_bytes;
        }

        jump_to_block(current_block_.next_block);
    }

    // The remaining bytes to read are in the current block, no more jumping is
    // needed. Read them and finish
    read(storage_file_, (char*)buffer + read_bytes, n_bytes - read_bytes);

    current_block_position_ += n_bytes - read_bytes;
    current_region_position_ += n_bytes;

    return n_bytes;
}

size_t storage_write_in_region(void* buffer, size_t n_bytes)
{
    if (!storage_initialized())
    {
        return 0;
    }

    size_t written_bytes = 0;

    // While the amount of bytes to write exceeds the amount of bytes remaining
    // in the current block
    while (current_block_position_ + n_bytes - written_bytes
           >= active_block_size_)
    {
        // Overwrite the rest of the bytes in this block and then jump to the
        // next block
        int bytes_to_write = active_block_size_ - current_block_position_;
        write(storage_file_, (char*)buffer + written_bytes, bytes_to_write);
        written_bytes += bytes_to_write;

        if (current_block_.next_block != INVALID_BLOCK)
        {
            jump_to_block(current_block_.next_block);
        }
        else
        {
            // If there is no next block, allocate a new one
            block_index current_block = current_block_index_;
            block_index new_block = allocate_block(current_block_index_);

            if (new_block == INVALID_BLOCK)
            {
                // Failed to allocate block: out of storage space. Stop writing
                return written_bytes;
            }

            jump_to_block(current_block);

            // Update the current block's header to point to the new block
            lseek(storage_file_,
                  -BLOCK_HEADER_SIZE + sizeof(char) + sizeof(block_index),
                  SEEK_CUR);
            write(storage_file_, &new_block, sizeof(block_index));

            jump_to_block(new_block);
        }
    }

    // The remaining bytes to write fit in the current block, no more jumping is
    // needed. Write them and finish
    write(storage_file_, (char*)buffer + written_bytes, n_bytes - written_bytes);

    current_block_position_ += n_bytes - written_bytes;
    current_region_position_ += n_bytes;

    return n_bytes;
}

size_t storage_seek_in_region(off_t offset)
{
    if (!storage_initialized())
    {
        return current_region_position_;
    }

    off_t sought_bytes = 0;

    // This function can only seek from the current position
    if (offset > 0)
    {
        // While the amount of bytes to seek exceeds the amount of bytes
        // remaining in the current block
        while (current_block_position_ + offset - sought_bytes
            >= active_block_size_)
        {
            // Jump to the next block
            sought_bytes += active_block_size_ - current_block_position_;

            jump_to_block(current_block_.next_block);
        }

        // The final position is in the current block: seek there and finish
        lseek(storage_file_, offset - sought_bytes, SEEK_CUR);
        current_block_position_ += offset - sought_bytes;
    }
    else if (offset < 0)
    {
        // While the amount of bytes to skip exceeds the amount of bytes
        // remaining in the current block
        while (current_block_position_ + offset - sought_bytes < 0)
        {
            // Jump to the previous block
            sought_bytes -= current_block_position_ + 1;

            jump_to_block(current_block_.previous_block);

            lseek(storage_file_, active_block_size_ - 1, SEEK_CUR);
            current_block_position_ = active_block_size_ - 1;
        }

        // The final position is in the current block: seek there and finish
        lseek(storage_file_, offset - sought_bytes, SEEK_CUR);
        current_block_position_ += offset - sought_bytes;
    }

    current_region_position_ += offset;

    return current_region_position_;
}

void create_storage_file(unsigned short block_size,
                         unsigned short block_count)
{
    // Create an empty storage file for virtual storage. O_BINARY is a Windows-
    // specific modifier needed so that Windows does not treat the file as text
    // and automatically add line endings that would break the file structure
    int file = open(
        STORAGE_PATH, O_CREAT | O_EXCL | O_BINARY | O_WRONLY, S_IRUSR | S_IWUSR);

    if (file == -1)
    {
        return;
    }

    // Write header
    write(file, &block_size, sizeof(unsigned short));
    write(file, &block_count, sizeof(unsigned short));

    char* zeroChars = (char*)malloc(block_size);
    memset(zeroChars, 0, block_size);

    // Write reserved empty first block
    write(file, &BLOCK_IN_USE_INDICATOR, sizeof(char));
    write(file, &INVALID_BLOCK, sizeof(block_index));
    write(file, &INVALID_BLOCK, sizeof(block_index));
    write(file, zeroChars, block_size);

    // Write the remaining empty blocks
    for (int i = 1; i < block_count; i++)
    {
        write(file, &BLOCK_NOT_IN_USE_INDICATOR, sizeof(char));
        write(file, &INVALID_BLOCK, sizeof(block_index));
        write(file, &INVALID_BLOCK, sizeof(block_index));
        write(file, zeroChars, block_size);
    }

    free(zeroChars);

    close(file);
}

block_index allocate_block(block_index previous_block)
{
    block_index inspected_block_index = 0;

    // Find, reserve and return the first free block by going through them one
    // at a time. This could be optimized by caching a list of available blocks
    while (inspected_block_index < active_block_count_)
    {
        jump_to_block(inspected_block_index);

        if (!current_block_.in_use)
        {
            // Set header data of new block
            lseek(storage_file_, -BLOCK_HEADER_SIZE, SEEK_CUR);
            write(storage_file_, &BLOCK_IN_USE_INDICATOR, sizeof(char));
            write(storage_file_, &previous_block, sizeof(block_index));
            write(storage_file_, &INVALID_BLOCK, sizeof(block_index));

            return inspected_block_index;
        }

        inspected_block_index++;
    }

    // Went through every block and none were available: out of storage space
    return INVALID_BLOCK;
}

void jump_to_block(block_index block)
{
    if (block >= active_block_count_)
    {
        return;
    }

    lseek(storage_file_, FIRST_BLOCK_POSITION
          + (active_block_size_ + BLOCK_HEADER_SIZE)
          * block, SEEK_SET);

    read_block_header();

    current_block_index_ = block;
}

void read_block_header()
{
    read(storage_file_, &current_block_.in_use, sizeof(char));
    read(storage_file_, &current_block_.previous_block, sizeof(block_index));
    read(storage_file_, &current_block_.next_block, sizeof(block_index));

    current_block_position_ = 0;
}
