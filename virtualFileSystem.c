#include "virtualFileSystem.h"
#include "virtualStorage.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>

#define MAX_DESCRIPTORS 256

typedef struct virtual_file
{
    storage_region content_region;
    storage_region metadata_region;
    size_t length;
    size_t reader_position;
} virtual_file;

typedef struct directory_navigation_result
{
    char* remainder_path;
    storage_region directory_region;
} directory_navigation_result;

// More entry types could be added for e.g. shortcuts/symbolic links
typedef enum { NULL_ENTRY = 0, UNUSED_ENTRY = 1,
               FILE_ENTRY = 2, DIRECTORY_ENTRY = 3 } entry_type;

virtual_file* descriptors_[MAX_DESCRIPTORS] = { NULL };
const storage_region root_directory_region_ = 0;

file_descriptor last_used_descriptor_ = -1;

virtual_file find_virtual_file(const char* file_path);
virtual_file create_virtual_file(const char* file_path);
directory_navigation_result navigate_to_virtual_directory();
void update_virtual_file_metadata(
    storage_region metadata_region, size_t file_size);
void invalidate_last_descriptor();
void jump_to_file_if_needed(file_descriptor file_descriptor);

file_descriptor open_virtual(const char* path, int flags)
{
    if (!storage_initialized())
    {
        storage_initialize();
    }

    // Find an available descriptor to associate with this virtual file
    file_descriptor first_available_descriptor = -1;

    for (file_descriptor i = 0; i < MAX_DESCRIPTORS; i++)
    {
        if (descriptors_[i] == NULL)
        {
            first_available_descriptor = i;
            break;
        }
    }

    if (first_available_descriptor == -1)
    {
        // No descriptors available
        return -1;
    }

    invalidate_last_descriptor();

    // Open or create the virtual file
    virtual_file file = find_virtual_file(path);

    // If virtual file failed to open
    if (file.content_region == INVALID_REGION)
    {
        if (!(flags & O_CREAT))
        {
            // New virtual file creation not allowed
            return -1;
        }

        file = create_virtual_file(path);

        // If virtual file failed to be created
        if (file.content_region == INVALID_REGION)
        {
            return -1;
        }
    }
    else if (flags & O_EXCL)
    {
        // Virtual file exists, but O_EXCL requires it to not exist beforehand
        return -1;
    }

    descriptors_[first_available_descriptor] = malloc(sizeof(virtual_file));
    *descriptors_[first_available_descriptor] = file;

    if (flags & O_TRUNC)
    {
        // Delete the existing contents of the virtual file
        storage_free_region(file.content_region);
        file.content_region = storage_allocate_region();

        file.length = 0;
    }

    if (flags & O_APPEND)
    {
        file.reader_position = file.length;
    }

    return first_available_descriptor;
}

void close_virtual(file_descriptor file_descriptor)
{
    if (file_descriptor < 0 || file_descriptor >= MAX_DESCRIPTORS
        || descriptors_[file_descriptor] == NULL)
    {
        return;
    }

    free(descriptors_[file_descriptor]);
    descriptors_[file_descriptor] = NULL;
}

int mkdir_virtual(const char* directory_path)
{
    invalidate_last_descriptor();

    directory_navigation_result navigation_result
        = navigate_to_virtual_directory(directory_path);

    if (navigation_result.directory_region == INVALID_REGION)
    {
        // Could not navigate to the directory where the new directory was to
        // be created in
        free(navigation_result.remainder_path);

        return -1;
    }

    // Allocate regions for the new virtual directory
	storage_region content_region = storage_allocate_region();

	if (content_region == INVALID_REGION)
	{
        free(navigation_result.remainder_path);

		return -1;
	}

	storage_region metadata_region = storage_allocate_region();

	if (metadata_region == INVALID_REGION)
	{
		storage_free_region(content_region);
        free(navigation_result.remainder_path);

		return -1;
	}

    // Find the first available directory entry in the directory where the new
    // directory was created in
    storage_jump_to_region(navigation_result.directory_region);

	while (true)
	{
		char entry_type;
		storage_read_in_region(&entry_type, sizeof(char));

		if (entry_type == NULL_ENTRY || entry_type == UNUSED_ENTRY)
		{
			break;
		}

		storage_seek_in_region(sizeof(storage_region) + sizeof(storage_region));
	}

	storage_seek_in_region(-sizeof(char));

    // Write the data of the newly created directory to the directory entry
	entry_type entry_type = DIRECTORY_ENTRY;

	storage_write_in_region(&entry_type, sizeof(char));
	storage_write_in_region(&metadata_region, sizeof(storage_region));
	storage_write_in_region(&content_region, sizeof(storage_region));

	storage_jump_to_region(metadata_region);

    char remainder_path_length = strlen(navigation_result.remainder_path);
    storage_write_in_region(&remainder_path_length, sizeof(char));
    storage_write_in_region(navigation_result.remainder_path, remainder_path_length);

    free(navigation_result.remainder_path);

	return 0;
}

int rmdir_virtual(const char* directory_path)
{
    invalidate_last_descriptor();

    directory_navigation_result navigation_result
        = navigate_to_virtual_directory(directory_path);

    if (navigation_result.directory_region == INVALID_REGION)
    {
        // Could not navigate to the directory where the directory was to
        // be deleted from
        free(navigation_result.remainder_path);

        return -1;
    }

    // Find the directory entry of the directory being deleted
	while (true)
	{
		size_t entry_position = storage_seek_in_region(0);

		entry_type entry_type = 0;
		storage_read_in_region(&entry_type, sizeof(char));

        // Null entry means end of directory
		if (entry_type == NULL_ENTRY)
		{
			break;
		}

        // Skip past other entry types
		if (entry_type != DIRECTORY_ENTRY)
		{
			storage_seek_in_region(sizeof(storage_region) * 2);

			continue;
		}

        // Read the entry data of a directory
		storage_region metadata_region;
		storage_read_in_region(&metadata_region, sizeof(storage_region));

		storage_region content_region;
		storage_read_in_region(&content_region, sizeof(storage_region));

		size_t next_entry_position = storage_seek_in_region(0);

		storage_jump_to_region(metadata_region);

		char directory_name_length;
		storage_read_in_region(&directory_name_length, sizeof(char));

		char* directory_name = malloc(directory_name_length + 1);
		storage_read_in_region(directory_name, directory_name_length);
		directory_name[directory_name_length] = '\0';

        // Compare directory names to see if this is the directory we want to
        // delete
        if (strcmp(directory_name, navigation_result.remainder_path) == 0)
		{
			free(directory_name);
            free(navigation_result.remainder_path);

			storage_jump_to_region(content_region);

            // Go through the directory entries of the directory being deleted
			while (true)
			{
				entry_type = 0;
				storage_read_in_region(&entry_type, sizeof(char));

				if (entry_type == NULL_ENTRY)
				{
					break;
				}

				if (entry_type == UNUSED_ENTRY)
				{
					storage_seek_in_region(sizeof(storage_region) * 2);

					continue;
				}

                // This directory contains files or other directories: it can't
                // be deleted before deleting those first
				return -1;
			}

			entry_type = UNUSED_ENTRY;

			// Mark the table of contents entry as unused
            storage_jump_to_region(root_directory_region_);
			storage_seek_in_region(entry_position);
			storage_write_in_region(&entry_type, sizeof(char));

            // Delete the regions used by this directory
			storage_free_region(content_region);
			storage_free_region(metadata_region);

			return 0;
		}

		free(directory_name);

        storage_jump_to_region(navigation_result.directory_region);
		storage_seek_in_region(next_entry_position);
	}

    // No directory entry found: directory to be deleted does not exist
    free(navigation_result.remainder_path);

    return -1;
}

int unlink_virtual(const char* file_path)
{
    invalidate_last_descriptor();

    directory_navigation_result navigation_result
        = navigate_to_virtual_directory(file_path);

    if (navigation_result.directory_region == INVALID_REGION)
    {
        // Could not navigate to the directory where the file was to be deleted
        // from
        free(navigation_result.remainder_path);

        return -1;
    }

    // Find the directory entry of the file being deleted
    while (true)
    {
        size_t entry_position = storage_seek_in_region(0);

        entry_type entry_type = 0;
        storage_read_in_region(&entry_type, sizeof(char));

        // Null entry means end of directory
        if (entry_type == NULL_ENTRY)
        {
            break;
        }

        // Skip past other entry types
        if (entry_type != FILE_ENTRY)
        {
            storage_seek_in_region(sizeof(storage_region) * 2);

            continue;
        }

        // Read the entry data of a file
        storage_region metadata_region;
        storage_read_in_region(&metadata_region, sizeof(storage_region));

        storage_region content_region;
        storage_read_in_region(&content_region, sizeof(storage_region));

        size_t next_entry_position = storage_seek_in_region(0);

        storage_jump_to_region(metadata_region);

        size_t file_length;
        storage_read_in_region(&file_length, sizeof(size_t));

        char file_name_length;
        storage_read_in_region(&file_name_length, sizeof(char));

        char* file_name = malloc(file_name_length + 1);
        storage_read_in_region(file_name, file_name_length);
        file_name[file_name_length] = '\0';

        // Compare file names to see if this is the file we want to delete
        if (strcmp(file_name, navigation_result.remainder_path) == 0)
        {
            free(file_name);
            free(navigation_result.remainder_path);

            // Mark the table of contents entry as unused
			entry_type = UNUSED_ENTRY;

            storage_jump_to_region(navigation_result.directory_region);
            storage_seek_in_region(entry_position);
            storage_write_in_region(&entry_type, sizeof(char));

            // Delete the regions used by this file
            storage_free_region(content_region);
            storage_free_region(metadata_region);

            return 0;
        }

        free(file_name);
		
        storage_jump_to_region(navigation_result.directory_region);
        storage_seek_in_region(next_entry_position);
    }

    // No directory entry found: file to be deleted does not exist
    free(navigation_result.remainder_path);

    return -1;
}

ssize_t read_virtual(file_descriptor file_descriptor, void* buffer, size_t n_bytes)
{
    if (file_descriptor < 0 || file_descriptor >= MAX_DESCRIPTORS
        || descriptors_[file_descriptor] == NULL)
    {
        return 0;
    }

    jump_to_file_if_needed(file_descriptor);

    size_t bytes_to_read = n_bytes;

    // If the virtual file is too small to contain all the bytes requested,
    // clamp the byte count to the amount of bytes available
    if (descriptors_[file_descriptor]->reader_position + bytes_to_read
        > descriptors_[file_descriptor]->length)
    {
        bytes_to_read = descriptors_[file_descriptor]->length
            - descriptors_[file_descriptor]->reader_position;
    }

    storage_read_in_region(buffer, bytes_to_read);

    descriptors_[file_descriptor]->reader_position += bytes_to_read;

    return bytes_to_read;
}

ssize_t write_virtual(file_descriptor file_descriptor, void* buffer, size_t n_bytes)
{
    if (file_descriptor < 0 || file_descriptor >= MAX_DESCRIPTORS
        || descriptors_[file_descriptor] == NULL)
    {
        return 0;
    }

    jump_to_file_if_needed(file_descriptor);
    storage_write_in_region(buffer, n_bytes);

    descriptors_[file_descriptor]->reader_position += n_bytes;

    // Update file length if the write operation wrote past the file's
    // previous length
    if (descriptors_[file_descriptor]->reader_position >=
        descriptors_[file_descriptor]->length)
    {
        descriptors_[file_descriptor]->length =
                descriptors_[file_descriptor]->reader_position + 1;

        update_virtual_file_metadata(
            descriptors_[file_descriptor]->metadata_region,
            descriptors_[file_descriptor]->length);
    }

    return n_bytes;
}

off_t seek_virtual(file_descriptor file_descriptor, off_t offset, int whence)
{
    if (file_descriptor < 0 || file_descriptor >= MAX_DESCRIPTORS
        || descriptors_[file_descriptor] == NULL)
    {
        return -1;
    }

    off_t new_position = descriptors_[file_descriptor]->reader_position;

    switch (whence)
    {
    case SEEK_SET:
    {
        new_position = offset;
        break;
    }

    case SEEK_CUR:
    {
        new_position = descriptors_[file_descriptor]->reader_position + offset;
        break;
    }

    case SEEK_END:
    {
        new_position = descriptors_[file_descriptor]->length + offset;
        break;
    }

    default:
    {
        return descriptors_[file_descriptor]->reader_position;
    }
    }

    // Clamp new position
    if (new_position < 0)
    {
        new_position = 0;
    }
    else if (new_position > descriptors_[file_descriptor]->length)
    {
        new_position = descriptors_[file_descriptor]->length;
    }

    descriptors_[file_descriptor]->reader_position = new_position;

    return descriptors_[file_descriptor]->reader_position;
}

virtual_file find_virtual_file(const char* file_path)
{
    directory_navigation_result navigation_result
        = navigate_to_virtual_directory(file_path);

    if (navigation_result.directory_region == INVALID_REGION)
    {
        // Could not navigate to the directory where the file was expected to
        // be
        free(navigation_result.remainder_path);

        return (virtual_file) { INVALID_REGION, INVALID_REGION, 0, 0 };
    }

    // Find the directory entry of the file
    while (true)
    {
        entry_type entry_type = 0;
        storage_read_in_region(&entry_type, sizeof(char));

        // Null entry means end of directory
        if (entry_type == NULL_ENTRY)
        {
            break;
        }

        // Skip past other entry types
        if (entry_type != FILE_ENTRY)
        {
            storage_seek_in_region(sizeof(storage_region) * 2);

            continue;
        }

        // Read the entry data of a file
        storage_region metadata_region;
        storage_read_in_region(&metadata_region, sizeof(storage_region));

        storage_region content_region;
        storage_read_in_region(&content_region, sizeof(storage_region));

        size_t next_entry_position = storage_seek_in_region(0);

        storage_jump_to_region(metadata_region);

        size_t file_length;
        storage_read_in_region(&file_length, sizeof(size_t));

        char file_name_length;
        storage_read_in_region(&file_name_length, sizeof(char));

        char* file_name = malloc(file_name_length + 1);
        storage_read_in_region(file_name, file_name_length);
        file_name[file_name_length] = '\0';

        // Compare file names to see if this is the file we are looking for
        if (strcmp(file_name, navigation_result.remainder_path) == 0)
        {
            free(file_name);
            free(navigation_result.remainder_path);

            return (virtual_file)
                { content_region, metadata_region, file_length, 0 };
        }

        free(file_name);

        storage_jump_to_region(navigation_result.directory_region);
        storage_seek_in_region(next_entry_position);
    }

    // No directory entry found: file does not exist
    free(navigation_result.remainder_path);

    return (virtual_file) { INVALID_REGION, INVALID_REGION, 0, 0 };
}

virtual_file create_virtual_file(const char* file_path)
{
    directory_navigation_result navigation_result
        = navigate_to_virtual_directory(file_path);

    if (navigation_result.directory_region == INVALID_REGION)
    {
        // Could not navigate to the directory where the file was going to be
        // created in
        free(navigation_result.remainder_path);

        return (virtual_file) { INVALID_REGION, INVALID_REGION, 0, 0 };
    }

    // Allocate regions for the new virtual file
	storage_region content_region = storage_allocate_region();

	if (content_region == INVALID_REGION)
	{
        free(navigation_result.remainder_path);

		return (virtual_file) { INVALID_REGION, INVALID_REGION, 0, 0 };
	}

	storage_region metadata_region = storage_allocate_region();

	if (metadata_region == INVALID_REGION)
	{
        free(navigation_result.remainder_path);
		storage_free_region(content_region);

		return (virtual_file) { INVALID_REGION, INVALID_REGION, 0, 0 };
	}

    // Write a directory entry for the new file
    storage_jump_to_region(navigation_result.directory_region);

    // Find the first available directory entry in the directory where the new
    // file was created in
    while (true)
    {
        char entry_type;
        storage_read_in_region(&entry_type, sizeof(char));

        if (entry_type == NULL_ENTRY || entry_type == UNUSED_ENTRY)
        {
            break;
        }

        storage_seek_in_region(sizeof(storage_region) + sizeof(storage_region));
    }

    storage_seek_in_region(-sizeof(char));

    // Write the data of the newly created file to the directory entry
	entry_type entry_type = FILE_ENTRY;

    storage_write_in_region(&entry_type, sizeof(char));
    storage_write_in_region(&metadata_region, sizeof(storage_region));
    storage_write_in_region(&content_region, sizeof(storage_region));

    storage_jump_to_region(metadata_region);

    size_t file_length = 0;
    char remainder_path_length = strlen(navigation_result.remainder_path);
    storage_write_in_region(&file_length, sizeof(size_t));
    storage_write_in_region(&remainder_path_length, sizeof(char));
    storage_write_in_region(navigation_result.remainder_path, remainder_path_length);

    free(navigation_result.remainder_path);

    return (virtual_file) { content_region, metadata_region, file_length, 0 };
}

directory_navigation_result navigate_to_virtual_directory(char* path)
{
    if (!storage_initialized())
    {
        storage_initialize();
    }

    storage_region directory_region = root_directory_region_;
    int last_slash_position = -1;

    storage_jump_to_region(directory_region);

    for (int i = 0; i < strlen(path); i++)
    {
        // Split the path according to forward slashes and try to find each
        // directory in the path starting from the root directory
        if (path[i] == '/')
        {
            // Get the name of the next directory that needs to be found
            int directory_name_length = (i - 1) - last_slash_position;
            char* directory_name = malloc(directory_name_length + 1);
            memcpy(directory_name, path + last_slash_position + 1,
                directory_name_length);
            directory_name[directory_name_length] = '\0';

            // Go through the entries of the current directory
            while (true)
            {
                entry_type entry_type = 0;
                storage_read_in_region(&entry_type, sizeof(char));

                if (entry_type == NULL_ENTRY)
                {
                    // Null entry means end of directory: next directory to go
                    // into did not exist
                    return (directory_navigation_result) { NULL, INVALID_REGION };
                }

                // Skip over other entries
                if (entry_type != DIRECTORY_ENTRY)
                {
                    storage_seek_in_region(sizeof(storage_region) * 2);

                    continue;
                }

                // Read the entry data of a virtual directory
                storage_region metadata_region;
                storage_read_in_region(&metadata_region, sizeof(storage_region));

                storage_region content_region;
                storage_read_in_region(&content_region, sizeof(storage_region));

                size_t next_entry_position = storage_seek_in_region(0);

                storage_jump_to_region(metadata_region);

                char entry_name_length;
                storage_read_in_region(&entry_name_length, sizeof(char));

                char* entry_name = malloc(entry_name_length + 1);
                storage_read_in_region(entry_name, entry_name_length);
                entry_name[entry_name_length] = '\0';

                // Compare directory names to see if this is the directory we
                // are looking for
                if (strcmp(entry_name, directory_name) == 0)
                {
                    free(entry_name);

                    // Directory found: jump into it and look for the next
                    // directory in the path
                    directory_region = content_region;
                    storage_jump_to_region(directory_region);

                    break;
                }

                free(entry_name);

                storage_jump_to_region(directory_region);
                storage_seek_in_region(next_entry_position);
            }

            free(directory_name);

            last_slash_position = i;
        }
    }

    // Navigation was successful: finally, isolate the name of the file or last
    // directory in the given path for use in other functions
    int remainder_name_length = (strlen(path) - 1) - last_slash_position;
    char* remainder_name = malloc(remainder_name_length + 1);
    memcpy(remainder_name, path + last_slash_position + 1,
        remainder_name_length);
    remainder_name[remainder_name_length] = '\0';

    return (directory_navigation_result) { remainder_name, directory_region };
}

void update_virtual_file_metadata(storage_region metadata_region, size_t file_size)
{
    storage_jump_to_region(metadata_region);
    storage_write_in_region(&file_size, sizeof(size_t));

    invalidate_last_descriptor();
}

void invalidate_last_descriptor()
{
    // This is needed whenever the storage region is changed
    last_used_descriptor_ = -1;
}

void jump_to_file_if_needed(file_descriptor file_descriptor)
{
    // This function optimizes reading and writing by making sure the storage
    // doesn't jump to the file content region if it's already there
    if (last_used_descriptor_ == file_descriptor)
    {
        return;
    }

    storage_jump_to_region(descriptors_[file_descriptor]->content_region);
    storage_seek_in_region(descriptors_[file_descriptor]->reader_position);

    last_used_descriptor_ = file_descriptor;
}
