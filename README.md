# Simulated File System
This code implements a simulated file system that has equivalents for the C system calls open(), close(), read(), write(), lseek(), unlink(), mkdir() and rmdir(). As such, the file system supports creating and deleting files, reading from, writing to and seeking in them, as well as creating and deleting directories. open() supports the flags O_APPEND, O_CREAT, O_EXCL and O_TRUNC. rmdir() fails if the directory is not empty.

Multiple files can be open at the same time, but concurrent operations are not supported. main.c contains a short test run for the system, but it's not a part of the system itself. When compiled using the included CMake configuration, the resulting program runs the test run and prints the contents of an example virtual text file.

## Virtual block storage
The virtual files are saved into a single real storage file on the computer. This file is divided into equal-sized blocks that can be allocated for virtual file contents as well as metadata. Blocks can be connected together using block indices which makes it possible to divide a long continuous data segment between multiple blocks. The blocks do not need to be adjacent to be connected. This block-based approach was chosen to minimize the amount of data that needs to be moved when virtual files are deleted or appended to. The virtualStorage module handles this part of the system, and it's used by allocating regions which internally correspond to a list of connected blocks. Regions are used like continuous byte streams and virtualStorage manages the underlying blocks that store their data.

The storage file contains a short header followed by n blocks where n is the block count listed in the header. The header is structured as follows:

|Offset|Bytes|Description|
|--|--|--|
|0|2|Block size in bytes (unsigned integer)|
|2|2|Block count (unsigned integer)|

Each block is structured as follows:

|Offset|Bytes|Description|
|--|--|--|
|0|1|Block usage marker: 0 if unused, 1 if in use|
|1|2|Index of previous block (unsigned integer)|
|3|2|Index of next block (unsigned integer)|
|5|Block size|Contents of the block|

The block header is not included in the block size, so each block uses (block size) + 5 bytes of space on the disk. All the data in the simulated file system is stored in these blocks. The structure of the storage file can be useful to inspect with a hex editor.

## Virtual file system
The blocks of the storage file are used for storing three things: the contents of virtual files, the contents of virtual directories and file and directory metadata. The first block in the storage file is reserved to start the region that contains the root directory of the virtual file system: this way the system has a guaranteed safe entry point into the blocks that it can use to find other data in the virtual file system.

Virtual directories consist of a list of entries that are structured as follows:
|Offset|Bytes|Description|
|--|--|--|
|0|1|Entry type: 0 if null, 1 if unused, 2 if file, 3 if directory|
|1|2|Index of block that starts this entry's metadata (unsigned integer)|
|3|2|Index of block that starts this entry's content (unsigned integer)|

Null entries are guaranteed to not contain any more entries after them, so the system knows it has reached the end of the entry list when it encounters the first null entry. Unused entries are needed to avoid moving entries around when virtual files and directories are deleted, and they can be later repurposed for new virtual files and directories.

A virtual file's metadata is structured as follows:
|Offset|Bytes|Description|
|--|--|--|
|0|sizeof(size_t)|Length of virtual file in bytes (size_t)|
|sizeof(size_t)|1|Length of file name in bytes (unsigned integer)|
|sizeof(size_t) + 1|Length of file name|File name (char array)|

The file metadata format could be expanded to include permission data as well as miscellaneous details such as file creation date, thumbnail, creator name etc.

A virtual directory's metadata is structured as follows:
|Offset|Bytes|Description|
|--|--|--|
|0|1|Length of directory name in bytes (unsigned integer)|
|1|Length of directory name|Directory name (char array)|

A virtual file's content region has no predefined structure as it contains the virtual file's raw data.

When a virtual file or directory is deleted, the actual data is not erased in any way: instead, the blocks and directory entries used by the file or directory are marked as unused and thus become inaccessible by the open_virtual() function. Block and entry allocations in the future can then overwrite the "deleted" data when needed. This way deleting files and directories is very efficient as it does not require erasing or moving any data.
