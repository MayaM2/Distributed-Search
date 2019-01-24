# Distributed Search

## The program gets three variables as command line arguments:
1. Search root directory (search for files within this directory and its subdirectories).
2. Search term (search for file names that include the search term).
3. Number of threads to be used for the search (assume a valid number greater than 0).

## Program Flow:
1. Create a queue that holds directories.
2. Put the search root directory in the queue.
3. Create n threads (the number received in argument 3). Each thread removes directories from the queue and searches for file names with the specified term. The flow of a thread is described below.
4. When there are no more directories in the queue, and all threads are idle (not searching for content within a directory) the program should exit with exit code 0, and print to stdout how many matching files were found.

## Threads Flow:
1. Dequeue one directory from the queue. If the queue is empty, sleep until it becomes non-empty.
2. Iterate through each file in that directory.
3. If the file name contains the search term (argument 2, case sensitive), print the full path of that file (including the file’s name) to stdout.
4. If the file is a directory, don’t match its name to the search term. Instead, add that directory to the shared queue and wake up a thread to process it.
5. When done iterating through all files within the directory, repeat from 1.
