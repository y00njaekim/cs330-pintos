#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/disk.h"

struct bitmap;

void inode_init (void);
#ifndef EFILESYS
bool inode_create (disk_sector_t, off_t);
#else
bool inode_create (disk_sector_t, off_t, bool);
#endif /* EFILESYS */
bool inode_syml_create(disk_sector_t, const char *);
struct inode *inode_open(disk_sector_t);
struct inode *inode_reopen (struct inode *);
disk_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);
bool inode_check_dir(struct inode *);
bool inode_check_syml(struct inode *);
bool inode_check_opened(struct inode *);
int get_inode_opencnt(struct inode *);
struct inode *syml_to_inode(struct inode *);

#endif /* filesys/inode.h */
