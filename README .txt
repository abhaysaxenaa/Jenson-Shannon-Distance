Systems Programming - CS214 - Spring 2021
Project 2 - Multithreading (Jensen-Shannon Distribution)


Name: Jesse Fisher
NetID: jaf490


Name: Abhay Saxena
NetID: ans192




Testing Strategy:
The testing strategy for the program "compare.c" mainly revolved around considering multiple cases that test the program's accuracy, unit testing and edge case handling.




1. Jensen-Shannon Distribution Computation Check:
        - Two Empty Files: 0.0
        - One empty file and one non-empty file: 0.707107
        - Non-overlapping files (no common words): 1.0
        - Completely overlapping files (all common words): 0.0
        - Partially overlapping files: Somewhere between 0.0 and 1.0

2. Word Frequency Distribution Check:
* Uppercase characters are converted to lowercase characters.
* Special characters are ignored.
* Only consider letters, numbers and a hyphen.
* Ensure that the sum of the mean frequencies for each Linked List of words was equal to 1.


3. Optional Arguments Check:
        - If file threads are 4 (-f4), then we SHOULD have 4 file threads operating.
        - If directory threads are 4 (-d4), then we SHOULD have 4 directory threads operating.
        - If analysis threads are 4 (-a4), then we SHOULD have 4 analysis threads operating.
        - If file suffix is set to ".txt", then we SHOULD only consider and compare ".txt" files.
        - If the suffix argument is given as “-s”, then we will attempt to compare all files.


4. Files and Scenarios:
        - Cannot run program for executables.
        - If permission to access a file is denied, report an error and continue.


5. Critical Edge Cases:
        - If file or directory cannot be opened, report using perror() and continue.
        - If there is a thread or lock error, report error and terminate immediately.
        - If less than 2 files are provided, then report an error and exit program.
        - Even if the number of threads is greater than the number of files, the program should work properly.




Memory Leak Check:
We compiled our program using Address Sanitizer or Valgrind at all times, to ensure that there was no missing deallocation of resources. Checked if we had deallocated every single byte of data allocated while calling malloc(), using Valgrind specifically.




Overall the program is pretty much bulletproof, and designed to be as general and intuitive as possible.