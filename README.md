findstr
=======

A tool for searching text or byte patterns in binary files.
Like `grep` for binaries.

 * searches for byte patterns
 * support regular expressions
 * supports several optimized string search algorithms, like Boyer-Moore
 * searches for unicode patterns. Finds UTF-16 and UTF-32 encoded strings.
 * wildcards can be specified in byte strings.
 * searches little endian words patterns
 * added -0 option, to only match the start of the file.
 * added '-S mask', which searches for a byte-mask pattern.
 * (OSX only) added -o, -L, -h to search in memory of the specified process.


USAGE
=====

    Usage: findstr [options]  pattern  files...
       -w       (regex) match words
       -b       binary match ( no unicode match )
       -I       case sensitive match
       -x       pattern is in hex
       -g       pattern is a guid
       -v       verbose
       -r       recurse
       -l       list matching files
       -c       count number of matches per file
       -f       follow, keep checking file for new data
       -M NUM   max file size
       -S NAME  search algorithm: regex, std, stdbm, stdbmh, boostbm, boostbmh, boostkmp, mask
       -Q       use posix::read, instead of posix::mmap


EXAMPLE
=======

    findstr -x "11 22 33 44|55 66 ?? 77"  *.bin

Searches for occurences of these byte sequences: {0x11, 0x22, 0x33, 0x44}  or {0x55, 0x66, ANY, 0x77} .


    findstr "test1234" *.bin

Searches for the ascii, utf-16, or utf-32 encoded string  `"test1234"`.


    findstr -x 12345678  *.bin

Searches for the little endian DWORD:  0x12345678: the byte pattern: { 0x78, 0x56, 0x34, 0x12 }.


search algorithm
================

You can specify an alternate search algorithm using the `-S` option.
Available are:

| type       | algorithm           |
| :--------  | :----------------   |
| regex      | `std::regex`                                |
| std        | `std::default_searcher`                     |
| stdbm      | `boyer_moore_searcher`                      |
| stdbmh     | `boyer_moore_horspool_searcher`             |
| boostbm    | `boost::algorithm::boyer_moore`             |
| boostbmh   | `boost::algorithm::boyer_moore_horspool`    |
| boostkmp   | `boost::algorithm::knuth_morris_pratt`      |
| mask       | `custom`                                    |

Depending on the search pattern and the file searched, a different algorithm may be the fastest. You will have to experiment to see
what works best in your specific case.


BUILDING
========

## Dependencies

`findstr` depends on [cpputils](hhttps://github.com/nlitsme/cpputils), [hexdumper](https://github.com/nlitsme/hexdumper) and [boost](https://boost.org/).

## make

The toplevel `Makefile` will invoke cmake for you, and then build.

Cmake will automatically download cpputils and hexdumper for you.
You will need to manually install `boost`.
Type: `make`  to run the cmake / gnumake build.

You can avoid automatic downloads by creating symlinks for cpputils and hexdumper in the `symlinks` subdirectory.


TODO
====

 * specify what context is printed for matches:
   * only the offset
   * encoding: the raw bytes, as ascdumped-text, as has.
   * the entire line.
   * a range of bytes.
   * the file block.
   * some way of defining a 'record'

 * add support for linux `/proc/<pid>/mem`,  reading info from ../maps

AUTHOR
======

(c) 2004-2019  Willem Hengeveld <itsme@xs4all.nl>


