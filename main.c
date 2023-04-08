#include "virtualFileSystem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

int main()
{
    // Write to virtual files
	mkdir_virtual("Documents");
	mkdir_virtual("Documents2");

    int virtual_file = open_virtual("Documents/testFile.txt", O_CREAT);

    int virtual_file_2 = open_virtual("Documents2/testFile2.txt", O_CREAT);

    char* deleted_message = "This text will be written to a virtual file, but "
        "the file will be deleted and the storage space will be claimed by"
        "another file";

    write_virtual(virtual_file_2, deleted_message, strlen(deleted_message));

    char* overwritten_message = "This text will be overwritten and will not"
        "be printed later";

    char* test_message = "This long string is spread across multiple memory "
        "blocks of the virtual storage file, but it can be written and read "
        "seamlessly through the virtual file system interface without "
        "knowledge of the memory block boundaries and thier connections\n";

    write_virtual(virtual_file, overwritten_message, strlen(overwritten_message));

    close_virtual(virtual_file_2);

    unlink_virtual("Documents2/testFile2.txt");
	rmdir_virtual("Documents2");

    seek_virtual(virtual_file, 0, SEEK_SET);
    write_virtual(virtual_file, test_message, strlen(test_message));

    // Seek back and fix a typo in the test message
    seek_virtual(virtual_file, -16, SEEK_CUR);
    write_virtual(virtual_file, "ei", 2);

    close_virtual(virtual_file);

    // Read from virtual file
    virtual_file = open_virtual("Documents/testFile.txt", 0);

    char* test_message_2 = malloc(strlen(test_message) + 1);
    test_message_2[strlen(test_message)] = '\0';

    read_virtual(virtual_file, test_message_2, strlen(test_message));

    printf("%s", test_message_2);

    free(test_message_2);

    close_virtual(virtual_file);

    return 0;
}
