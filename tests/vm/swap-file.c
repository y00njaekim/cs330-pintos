/* Checks if file-mapped pages 
 * are properly swapped out and swapped in 
 * For this test, Pintos memory size is 128MB 
 * First, fills the memory with with anonymous pages
 * and then tries to map a file into the page */

#include <string.h>
#include <syscall.h>
#include <stdio.h>
#include <stdint.h>
#include "tests/lib.h"
#include "tests/main.h"
#include "tests/vm/large.inc"

void
test_main (void) 
{
    size_t handle;
    char *actual = (char *) 0x10000000;
    void *map;
    size_t i;
    // printf("1@@@@@\n");
    /* Map a page to a file */
    CHECK ((handle = open ("large.txt")) > 1, "open \"large.txt\"");
    // printf("2@@@@@\n");
    CHECK ((map = mmap (actual, sizeof(large), 0, handle, 0)) != MAP_FAILED, "mmap \"large.txt\"");
    // printf("3@@@@@\n");

    /* Check that data is correct. */
    if (memcmp (actual, large, strlen (large)))
        fail ("read of mmap'd file reported bad data");
    // printf("4@@@@@\n");

    /* Verify that data is followed by zeros. */
    size_t len = strlen(large);
   //  printf("5@@@@@\n");
    size_t page_end;
    // printf("6@@@@@\n");
    for(page_end = 0; page_end < len; page_end+=4096);
    // printf("7@@@@@\n");

    for (i = len+1; i < page_end; i++) 
    {
        if (actual[i] != 0) {
            fail ("byte %zu of mmap'd region has value %02hhx (should be 0)", i, actual[i]);
        }
    }
    // printf("8@@@@@\n");

    /* Unmap and close opend file */ 
    munmap (map);
    // printf("9@@@@@\n");
    close (handle);
    // printf("10@@@@@\n");
}

