#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/fat.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "filesys/directory.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* On-disk inode.
 * Must be exactly DISK_SECTOR_SIZE bytes long. */
struct inode_disk {
	disk_sector_t start;                /* First data sector. */
	off_t length;                       /* File size in bytes. */
	bool is_dir;
	bool is_syml;
	char link[458];
	unsigned magic;                     /* Magic number. */
	uint32_t unused[10]; /* Not used. */
};

/* Returns the number of sectors to allocate for an inode SIZE
 * bytes long. */
static inline size_t
bytes_to_sectors (off_t size) {
	return DIV_ROUND_UP (size, DISK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode {
	struct list_elem elem;              /* Element in inode list. */
	disk_sector_t sector;               /* Sector number of disk location. */
	int open_cnt;                       /* Number of openers. */
	bool removed;                       /* True if deleted, false otherwise. */
	int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
	struct semaphore inode_sema;
	struct inode_disk data;             /* Inode content. */
};

#ifndef EFILESYS
/* Returns the disk sector that contains byte offset POS within
 * INODE.
 * Returns -1 if INODE does not contain data for a byte at offset
 * POS. */
static disk_sector_t
byte_to_sector (const struct inode *inode, off_t pos) {
	ASSERT (inode != NULL);
	if (pos < inode->data.length)
		return inode->data.start + pos / DISK_SECTOR_SIZE;
	else
		return -1;
}
#else
/* Returns the disk sector that contains byte offset POS within
 * INODE.
 * Returns -1 if INODE does not contain data for a byte at offset
 * POS. */
static disk_sector_t
byte_to_sector (const struct inode *inode, off_t pos) {
	ASSERT (inode != NULL);
	if (pos < inode->data.length) {
		size_t sectors = pos / DISK_SECTOR_SIZE; // 몇 개의 sector 에 걸쳐 데이터가 저장되어 있는지 나타내는 변수
		cluster_t cclst = sector_to_cluster(inode->data.start);
		int i;
		for(i = 0; i<sectors; i++) {
			ASSERT(cclst != 0);
			ASSERT(cclst != EOChain);
			cclst = fat_get(cclst);
		}
		ASSERT(cclst != 0);
		ASSERT(cclst != EOChain);
		return cluster_to_sector(cclst);
	}
	else
		return -1;
}
#endif /* EFILESYS */


/* List of open inodes, so that opening a single inode twice
 * returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) {
	list_init (&open_inodes);
}

#ifndef EFILESYS
/* Initializes an inode with LENGTH bytes of data and
 * writes the new inode to sector SECTOR on the file system
 * disk.
 * Returns true if successful.
 * Returns false if memory or disk allocation fails. */
bool
inode_create (disk_sector_t sector, off_t length) {
	struct inode_disk *disk_inode = NULL;
	bool success = false;

	ASSERT (length >= 0);

	/* If this assertion fails, the inode structure is not exactly
	 * one sector in size, and you should fix that. */
	ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

	disk_inode = calloc (1, sizeof *disk_inode);
	if (disk_inode != NULL) {
		size_t sectors = bytes_to_sectors (length);
		disk_inode->length = length;
		disk_inode->magic = INODE_MAGIC;
		if (free_map_allocate (sectors, &disk_inode->start)) {
			/* free_map_allocate (size_t cnt, disk_sector_t *sectorp)
			 * Allocates CNT consecutive sectors from the free map and stores the first into *SECTORP
			 */
			disk_write (filesys_disk, sector, disk_inode);
			/* disk_write (struct disk *d, disk_sector_t sec_no, const void *buffer)
			 * Write sector SEC_NO to disk D from BUFFER, which must contain DISK_SECTOR_SIZE bytes
			 */
			if (sectors > 0) {
				static char zeros[DISK_SECTOR_SIZE];
				size_t i;

				for (i = 0; i < sectors; i++) 
					disk_write (filesys_disk, disk_inode->start + i, zeros); 
			}
			success = true; 
		} 
		free (disk_inode);
	}
	return success;
}
#else
/* Initializes an inode with LENGTH bytes of data and
 * writes the new inode to sector SECTOR on the file system
 * disk.
 * Returns true if successful.
 * Returns false if memory or disk allocation fails. */
bool
inode_create (disk_sector_t sector, off_t length, bool is_dir) {
	struct inode_disk *disk_inode = NULL;
	bool success = false;

	ASSERT (length >= 0);

	/* If this assertion fails, the inode structure is not exactly
	 * one sector in size, and you should fix that. */
	ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

	disk_inode = calloc (1, sizeof *disk_inode);
	if (disk_inode != NULL) {
		size_t sectors = bytes_to_sectors (length);
		disk_inode->length = length;
		disk_inode->is_dir = is_dir;
		disk_inode->is_syml = false;
		disk_inode->magic = INODE_MAGIC;

		if(sectors > 0) {
			cluster_t nclst = fat_create_chain(0);
			if(nclst == 0) return false;
			disk_inode->start = cluster_to_sector(nclst);
			for (int i = 0; i < sectors-1; i++) {
				nclst = fat_create_chain(nclst);
				if(nclst == 0) {
					fat_remove_chain(sector_to_cluster(disk_inode->start), 0);
					return false; // Returns false if memory or disk allocation fails.
				}
			}
		}

		disk_write(filesys_disk, sector, disk_inode);
		if (sectors > 0) {
			static char zeros[DISK_SECTOR_SIZE];
			size_t i;
			cluster_t tclst = sector_to_cluster(disk_inode->start);
			for (i = 0; i < sectors; i++) {
				ASSERT(tclst != EOChain);
				disk_write(filesys_disk, cluster_to_sector(tclst), zeros);
				tclst = fat_get(tclst);
			}
		}
		success = true; 
		free (disk_inode);
	}
	return success;
}
#endif /* EFILESYS */

bool
inode_syml_create (disk_sector_t sector, const char *target) {
	struct inode_disk *disk_inode = NULL;
	bool success = false;

	ASSERT (strlen(target) >= 0);

	/* If this assertion fails, the inode structure is not exactly
	 * one sector in size, and you should fix that. */
	ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

	disk_inode = calloc (1, sizeof *disk_inode);
	if (disk_inode != NULL) {
		disk_inode->length = strlen(target) + 1;
		disk_inode->is_dir = false;
		disk_inode->is_syml = true;
		disk_inode->magic = INODE_MAGIC;
		strlcpy(disk_inode->link, target, strlen(target) + 1);

		cluster_t nclst = fat_create_chain(0);
		if(nclst == 0) return false;
		disk_inode->start = cluster_to_sector(nclst);
		disk_write(filesys_disk, sector, disk_inode);
		success = true; 
		free (disk_inode);
	}
	return success;
}

/* Reads an inode from SECTOR
 * and returns a `struct inode' that contains it.
 * Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (disk_sector_t sector) {
	int a = 1;
	if(debug_mode) printf("in inode_open %d\n", a);
	a++;
	struct list_elem *e;
	struct inode *inode;

	/* Check whether this inode is already open. */
	for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
			e = list_next (e)) {
		inode = list_entry (e, struct inode, elem);
		if (inode->sector == sector) {
			inode_reopen (inode);
			return inode; 
		}
	}

	if(debug_mode) printf("in inode_open %d\n", a);
	a++;
	/* Allocate memory. */
	inode = malloc (sizeof *inode);
	if (inode == NULL)
		return NULL;
	if(debug_mode) printf("in inode_open %d\n", a);
	a++;

	/* Initialize. */
	list_push_front (&open_inodes, &inode->elem);
	inode->sector = sector;
	inode->open_cnt = 1;
	inode->deny_write_cnt = 0;
	inode->removed = false;
	if(debug_mode) printf("in inode_open %d\n", a);
	a++;
	sema_init(&inode->inode_sema, 1);
	disk_read (filesys_disk, inode->sector, &inode->data);
	if(debug_mode) printf("in inode_open %d\n", a);
	a++;
	return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode) {
	if (inode != NULL)
		inode->open_cnt++;
	return inode;
}

/* Returns INODE's inode number. */
disk_sector_t
inode_get_inumber (const struct inode *inode) {
	return inode->sector;
}

#ifndef EFILESYS
/* Closes INODE and writes it to disk.
 * If this was the last reference to INODE, frees its memory.
 * If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) {
	/* Ignore null pointer. */
	if (inode == NULL)
		return;

	/* Release resources if this was the last opener. */
	if (--inode->open_cnt == 0) {
		/* Remove from inode list and release lock. */
		list_remove (&inode->elem);

		/* Deallocate blocks if removed. */
		if (inode->removed) {
			free_map_release (inode->sector, 1);
			free_map_release (inode->data.start,
					bytes_to_sectors (inode->data.length)); 
		}

		free (inode); 
	}
}
#else
/* Closes INODE and writes it to disk.
 * If this was the last reference to INODE, frees its memory.
 * If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) {
	/* Ignore null pointer. */
	// printf("in inode_close (inode:%p)\n", inode);
	if (inode == NULL)
		return;

	/* Release resources if this was the last opener. */
	if (--inode->open_cnt == 0) {
		/* Remove from inode list and release lock. */
		list_remove (&inode->elem);

		/* Deallocate blocks if removed. */
		if (inode->removed) {
			fat_remove_chain(inode->sector, 0);
			// printf("1\n");
			fat_remove_chain(inode->data.start, 0);
			// printf("2\n");
		}

		free (inode); 
	}
}
#endif

/* Marks INODE to be deleted when it is closed by the last caller who
 * has it open. */
void
inode_remove (struct inode *inode) {
	ASSERT (inode != NULL);
	inode->removed = true;
	if (inode->open_cnt == 0) {
		fat_remove_chain(inode->sector, 0);
		fat_remove_chain(inode->data.start, 0);
	}
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
 * Returns the number of bytes actually read, which may be less
 * than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) {
	uint8_t *buffer = buffer_;
	off_t bytes_read = 0;
	uint8_t *bounce = NULL;

	while (size > 0) {
		/* Disk sector to read, starting byte offset within sector. */
		disk_sector_t sector_idx = byte_to_sector (inode, offset);
		int sector_ofs = offset % DISK_SECTOR_SIZE;

		/* Bytes left in inode, bytes left in sector, lesser of the two. */
		off_t inode_left = inode_length (inode) - offset;
		int sector_left = DISK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

		/* Number of bytes to actually copy out of this sector. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;

		if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) {
			/* Read full sector directly into caller's buffer. */
			disk_read (filesys_disk, sector_idx, buffer + bytes_read); 
		} else {
			/* Read sector into bounce buffer, then partially copy
			 * into caller's buffer. */
			if (bounce == NULL) {
				bounce = malloc (DISK_SECTOR_SIZE);
				if (bounce == NULL)
					break;
			}
			disk_read (filesys_disk, sector_idx, bounce);
			memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
		}

		/* Advance. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_read += chunk_size;
	}
	free (bounce);

	return bytes_read;
}

#ifndef EFILESYS
/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
 * Returns the number of bytes actually written, which may be
 * less than SIZE if end of file is reached or an error occurs.
 * (Normally a write at end of file would extend the inode, but
 * growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
		off_t offset) {
	const uint8_t *buffer = buffer_;
	off_t bytes_written = 0;
	uint8_t *bounce = NULL;

	if (inode->deny_write_cnt)
		return 0;

	while (size > 0) {
		/* Sector to write, starting byte offset within sector. */
		disk_sector_t sector_idx = byte_to_sector (inode, offset);
		int sector_ofs = offset % DISK_SECTOR_SIZE;

		/* Bytes left in inode, bytes left in sector, lesser of the two. */
		off_t inode_left = inode_length (inode) - offset;
		int sector_left = DISK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

		/* Number of bytes to actually write into this sector. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;

		if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) {
			/* Write full sector directly to disk. */
			disk_write (filesys_disk, sector_idx, buffer + bytes_written); 
		} else {
			/* We need a bounce buffer. */
			if (bounce == NULL) {
				bounce = malloc (DISK_SECTOR_SIZE);
				if (bounce == NULL)
					break;
			}

			/* If the sector contains data before or after the chunk
			   we're writing, then we need to read in the sector
			   first.  Otherwise we start with a sector of all zeros. */
			if (sector_ofs > 0 || chunk_size < sector_left) 
				disk_read (filesys_disk, sector_idx, bounce);
			else
				memset (bounce, 0, DISK_SECTOR_SIZE);
			memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
			disk_write (filesys_disk, sector_idx, bounce); 
		}

		/* Advance. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_written += chunk_size;
	}
	free (bounce);

	return bytes_written;
}
#else
/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
 * Returns the number of bytes actually written, which may be
 * less than SIZE if end of file is reached or an error occurs.
 * (Normally a write at end of file would extend the inode, but
 * growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
		off_t offset) {
	const uint8_t *buffer = buffer_;
	off_t bytes_written = 0;
	uint8_t *bounce = NULL;

	if (inode->deny_write_cnt)
		return 0;
	sema_down(&inode->inode_sema);
	while (size > 0) {
		/* Sector to write, starting byte offset within sector. */
		disk_sector_t sector_idx = byte_to_sector (inode, offset+size);
		if(sector_idx == -1) {
			size_t sectors = bytes_to_sectors(offset + size) - bytes_to_sectors(inode->data.length);
			cluster_t tclst = (inode->data.start == 0) ? 0 : sector_to_cluster(inode->data.start);
			for (int i=0; i<sectors; i++) {
				tclst = fat_create_chain(tclst);
				ASSERT(tclst != 0);
				if(inode->data.start == 0) inode->data.start = cluster_to_sector(tclst);
			}
			inode->data.length = offset+size;
			disk_write(filesys_disk, inode->sector, &inode->data);
		}
		sector_idx = byte_to_sector (inode, offset);

		int sector_ofs = offset % DISK_SECTOR_SIZE;

		/* Bytes left in inode, bytes left in sector, lesser of the two. */
		off_t inode_left = inode_length (inode) - offset;
		int sector_left = DISK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

		/* Number of bytes to actually write into this sector. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;

		if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) {
			/* Write full sector directly to disk. */
			disk_write (filesys_disk, sector_idx, buffer + bytes_written); 
		} else {
			/* We need a bounce buffer. */
			if (bounce == NULL) {
				bounce = malloc (DISK_SECTOR_SIZE);
				if (bounce == NULL)
					break;
			}

			/* If the sector contains data before or after the chunk
			   we're writing, then we need to read in the sector
			   first.  Otherwise we start with a sector of all zeros. */
			if (sector_ofs > 0 || chunk_size < sector_left) 
				disk_read (filesys_disk, sector_idx, bounce);
			else
				memset (bounce, 0, DISK_SECTOR_SIZE);
			memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
			disk_write (filesys_disk, sector_idx, bounce); 
		}

		/* Advance. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_written += chunk_size;
	}
	free (bounce);
	sema_up(&inode->inode_sema);

	return bytes_written;
}
#endif /* EFILESYS */

/* Disables writes to INODE.
   May be called at most once per inode opener. */
	void
inode_deny_write (struct inode *inode) 
{
	inode->deny_write_cnt++;
	ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
 * Must be called once by each inode opener who has called
 * inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) {
	ASSERT (inode->deny_write_cnt > 0);
	ASSERT (inode->deny_write_cnt <= inode->open_cnt);
	inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode) {
	return inode->data.length;
}

bool
inode_check_dir (struct inode *inode) {
	return inode->data.is_dir;
}

bool
inode_check_syml (struct inode *inode) {
	return inode->data.is_syml;
}

bool
inode_check_opened (struct inode *inode) {
	if(debug_mode) printf("in inode_check_open\n");
	return inode->open_cnt > 1;
}

int
get_inode_opencnt (struct inode *inode) {
	if(debug_mode) printf("in inode_check_open\n");
	return inode->open_cnt;
}

struct inode*
syml_to_inode(struct inode *inode) {
	// int a = 1;
	// printf("in syml_to_inode %d\n", a);
	// a++;

	char *dir_copy;
	dir_copy = palloc_get_page (0);
	if (dir_copy == NULL) return NULL;
	strlcpy(dir_copy, inode->data.link, PGSIZE);
	// printf("in syml_to_inode inode->data.link is %s\n", dir_copy);

	struct thread *curr = thread_current();
	// 복사 과정 reference: process_create_initd (process.c)
	// printf("in syml_to_inode %d\n", a);
	// a++;

	struct dir *cdir;
	if (dir_copy[0] == '/') {
		cdir = dir_open_root();
	} else {
		if(curr->wdir != NULL) cdir = dir_reopen(curr->wdir);
		else cdir = dir_open_root();
	}
	struct inode *cinode;
	// printf("in syml_to_inode %d\n", a);
	// a++;

	// char bf[NAME_MAX + 1];
	// dir_skip_dot(cdir);
	// if(dir_readdir (cdir, bf))
	// {
	// 	printf("readdir: %s\n", bf);
	// }

	// printf("in syml_to_inode %d\n", a);
	// a++;
	char *ctoken;
	char *ntoken;
	char *save_ptr;
	ctoken = strtok_r(dir_copy, "/", &save_ptr);
	ntoken = strtok_r(NULL, "/", &save_ptr);
	while (ctoken != NULL && ntoken != NULL) {
		// printf("in while\n");
		// printf("in syml_to_inode, cdir sector is %p, ctoken is %s\n", inode_get_inumber(dir_get_inode(cdir)), ctoken);
		if(!dir_lookup(cdir, ctoken, &cinode)) {
			// printf("in syml_to_inode, in if\n");
			dir_close(cdir);
			return NULL;
		} else if(!inode_check_dir(cinode)) {
			// printf("in syml_to_inode, in else if\n");
			dir_close(cdir);
			palloc_free_page(dir_copy);
			return NULL;
		}
		// printf("in syml_to_inode, else\n");
		dir_close(cdir);
		cdir = dir_open(cinode);

		ctoken = ntoken;
		ntoken = strtok_r(NULL, "/", &save_ptr);
	}
	if(ctoken == NULL) ctoken = ".";

	// printf("in syml_to_inode %d\n", a);
	// a++;
	struct inode *tinode = NULL;

	if (cdir != NULL)
		dir_lookup (cdir, ctoken, &tinode);
	dir_close (cdir);
	// printf("in syml_to_inode %d\n", a);
	// a++;

	if (tinode != NULL) {
		if(inode_check_syml(tinode)) {
			// printf("in if\n");
			// printf("in filesys_open inode sector is %p\n", inode_get_inumber(inode));
			tinode = syml_to_inode(tinode);
		}
	}

	palloc_free_page(dir_copy);
	return tinode;
}
