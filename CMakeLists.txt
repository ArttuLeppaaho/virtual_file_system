cmake_minimum_required(VERSION 3.5)

project(virtual-file-system C)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(virtual-file-system
    main.c
    virtualFileSystem.h
    virtualFileSystem.c
    virtualStorage.h
    virtualStorage.c)
