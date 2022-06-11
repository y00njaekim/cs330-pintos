#include "filesys/filesys.h"
#include "threads/synch.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "devices/disk.h"
#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/mmu.h"
#include "lib/kernel/hash.h"
#include "userprog/syscall.h"
#include "lib/user/syscall.h"
#include <string.h>
#include "userprog/process.h"
#include "filesys/fat.h"

/* The disk that contains the file system. */
struct disk *filesys_disk;

static void do_format (void);

static struct semaphore filesys_sema;

/* Initializes the file system module.
 * If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) {
	filesys_disk = disk_get(0, 1);
	if (filesys_disk == NULL)
		PANIC ("hd0:1 (hdb) not present, file system initialization failed");

	sema_init(&filesys_sema, 1);
	inode_init ();

#ifdef EFILESYS
	fat_init ();

	if (format)
		do_format ();

	fat_open ();
#else
	/* Original FS */
	free_map_init ();

	if (format)
		do_format ();

	free_map_open ();
#endif
}

/* Shuts down the file system module, writing any unwritten data
 * to disk. */
void
filesys_done (void) {
	/* Original FS */
#ifdef EFILESYS
	fat_close ();
#else
	free_map_close ();
#endif
}

#ifndef EFILESYS
/* Creates a file named NAME with the given INITIAL_SIZE.
 * Returns true if successful, false otherwise.
 * Fails if a file named NAME already exists,
 * or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) {
	sema_down(&filesys_sema);
	disk_sector_t inode_sector = 0;
	struct dir *dir = dir_open_root ();
	bool success = (dir != NULL
			&& free_map_allocate (1, &inode_sector)
			&& inode_create (inode_sector, initial_size)
			&& dir_add (dir, name, inode_sector));
	if (!success && inode_sector != 0)
		free_map_release (inode_sector, 1);
	dir_close (dir);

	sema_up(&filesys_sema);
	return success;
}
#else
/* Creates a file named NAME with the given INITIAL_SIZE.
 * Returns true if successful, false otherwise.
 * Fails if a file named NAME already exists,
 * or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) {
	sema_down(&filesys_sema);
	disk_sector_t inode_sector = 0;

	bool fat_allocate = true;
	cluster_t nclst = fat_create_chain(0);
	if(nclst == 0) fat_allocate = false;
	inode_sector = cluster_to_sector(nclst);

	struct thread *curr = thread_current();
	char *dir_copy;
	dir_copy = palloc_get_page (0);
	if (dir_copy == NULL) return false;
	strlcpy(dir_copy, name, PGSIZE);
	// 복사 과정 reference: process_create_initd (process.c)

	struct dir *cdir;
	if (dir_copy[0] == '/') {
		cdir = dir_open_root();
	} else {
		if(curr->wdir != NULL) cdir = dir_reopen(curr->wdir);
		else cdir = dir_open_root();
	}
	struct inode *cinode;

	char *ctoken;
	char *ntoken;
	char *save_ptr;
	ctoken = strtok_r(dir_copy, "/", &save_ptr);
	ntoken = strtok_r(NULL, "/", &save_ptr);
	while (ctoken != NULL && ntoken != NULL) {
		if(!dir_lookup(cdir, ctoken, &cinode)) {
			dir_close(cdir);
			palloc_free_page(dir_copy);
			sema_up(&filesys_sema);
			if(inode_sector != 0) fat_remove_chain(sector_to_cluster(inode_sector), 0);
			return false;
		}
		if(inode_check_syml(cinode)) cinode = syml_to_inode(cinode);
		if(!inode_check_dir(cinode)) {
			dir_close(cdir);
			palloc_free_page(dir_copy);
			sema_up(&filesys_sema);
			if(inode_sector != 0) fat_remove_chain(sector_to_cluster(inode_sector), 0);
			return false;
		}
		dir_close(cdir);
		cdir = dir_open(cinode);

		ctoken = ntoken;
		ntoken = strtok_r(NULL, "/", &save_ptr);
	}
	bool success = (cdir != NULL
			&& fat_allocate
			&& inode_create (inode_sector, initial_size, false)
			&& dir_add (cdir, ctoken, inode_sector));
	
	// if(success) {
	// 	printf("in filesys_create: success, ctoken is %s dir inode sector is %p\n", ctoken, inode_get_inumber(dir_get_inode(cdir)));
	// } else {
	// 	printf("in filesys_create: fail\n");
	// }

	dir_close(cdir);
	if (!success && inode_sector != 0) fat_remove_chain(sector_to_cluster(inode_sector), 0);
	palloc_free_page(dir_copy);
	sema_up(&filesys_sema);
	return success;
}

/* Creates a dir named NAME.
 * Returns true if successful, false otherwise.
 * Fails if a file named NAME already exists,
 * or if internal memory allocation fails. */
bool
filesys_dir_create (const char *name) {
	sema_down(&filesys_sema);
	disk_sector_t inode_sector = 0;

	bool fat_allocate = true;
	cluster_t nclst = fat_create_chain(0);
	if(nclst == 0) fat_allocate = false;
	inode_sector = cluster_to_sector(nclst);

	struct thread *curr = thread_current();
	char *dir_copy;
	dir_copy = palloc_get_page (0);
	if (dir_copy == NULL) return false;
	strlcpy(dir_copy, name, PGSIZE);
	// 복사 과정 reference: process_create_initd (process.c)

	struct dir *cdir;
	if (dir_copy[0] == '/') {
		cdir = dir_open_root();
	} else {
		if(curr->wdir != NULL) cdir = dir_reopen(curr->wdir);
		else cdir = dir_open_root();
	}
	struct inode *cinode;

	char *ctoken;
	char *ntoken;
	char *save_ptr;
	ctoken = strtok_r(dir_copy, "/", &save_ptr);
	ntoken = strtok_r(NULL, "/", &save_ptr);
	while (ctoken != NULL && ntoken != NULL) {
		if(!dir_lookup(cdir, ctoken, &cinode)) {
			dir_close(cdir);
			if(inode_sector != 0) fat_remove_chain(sector_to_cluster(inode_sector), 0);
			palloc_free_page(dir_copy);
			sema_up(&filesys_sema);
			return false;
		}
		if(inode_check_syml(cinode)) cinode = syml_to_inode(cinode);
		if(!inode_check_dir(cinode)) {
			dir_close(cdir);
			if(inode_sector != 0) fat_remove_chain(sector_to_cluster(inode_sector), 0);
			palloc_free_page(dir_copy);
			sema_up(&filesys_sema);
			return false;
		}
		dir_close(cdir);
		cdir = dir_open(cinode);

		ctoken = ntoken;
		ntoken = strtok_r(NULL, "/", &save_ptr);
	}
	// 여기서 cdir 위에 ctoken 이름의 dir 만들면 됨

	struct inode *inode_itself;
	struct dir *dir_itself = NULL;

	bool success =
			(cdir != NULL
			&& fat_allocate
			&& dir_create(inode_sector, 16)
			&& dir_add(cdir, ctoken, inode_sector)
			&& dir_lookup(cdir, ctoken, &inode_itself)
			&& dir_add(dir_itself = dir_open(inode_itself), ".", inode_sector)
			&& dir_add(dir_itself, "..", inode_get_inumber(dir_get_inode(cdir))));

	if(dir_itself != NULL) dir_close(dir_itself);
	dir_close(cdir);

	if (!success && inode_sector != 0) fat_remove_chain(sector_to_cluster(inode_sector), 0);
	if(strcmp(name, "/0/0/0/0") == 0) {
		printf("@@@@@@@@@@ /0/0/0/0 disk is %d\n", inode_sector);
		printf("@@@@@@@@@@ dir_itself's open_cnt is %d\n", dir_get_inode_opencnt(dir_itself));
	}
	palloc_free_page(dir_copy);
	sema_up(&filesys_sema);
	return success;
}
#endif /* EFILESYS */


bool
filesys_syml_create (const char* target, const char* linkpath) {
	sema_down(&filesys_sema);
	disk_sector_t inode_sector = 0;

	bool fat_allocate = true;
	cluster_t nclst = fat_create_chain(0);
	if(nclst == 0) fat_allocate = false;
	inode_sector = cluster_to_sector(nclst);

	struct thread *curr = thread_current();
	char *dir_copy;
	dir_copy = palloc_get_page (0);
	if (dir_copy == NULL) return false;
	strlcpy(dir_copy, linkpath, PGSIZE);
	// 복사 과정 reference: process_create_initd (process.c)

	struct dir *cdir;
	if (dir_copy[0] == '/') {
		cdir = dir_open_root();
	} else {
		if(curr->wdir != NULL) cdir = dir_reopen(curr->wdir);
		else cdir = dir_open_root();
	}
	struct inode *cinode;

	char *ctoken;
	char *ntoken;
	char *save_ptr;
	ctoken = strtok_r(dir_copy, "/", &save_ptr);
	ntoken = strtok_r(NULL, "/", &save_ptr);
	while (ctoken != NULL && ntoken != NULL) {
		// printf("in while\n");
		if (!dir_lookup(cdir, ctoken, &cinode))
		{
			dir_close(cdir);
			palloc_free_page(dir_copy);
			sema_up(&filesys_sema);
			if(inode_sector != 0) fat_remove_chain(sector_to_cluster(inode_sector), 0);
			return false;
		}
		else if (!inode_check_dir(cinode))
		{
			dir_close(cdir);
			palloc_free_page(dir_copy);
			sema_up(&filesys_sema);
			if(inode_sector != 0) fat_remove_chain(sector_to_cluster(inode_sector), 0);
			return false;
		}
		dir_close(cdir);
		cdir = dir_open(cinode);

		ctoken = ntoken;
		ntoken = strtok_r(NULL, "/", &save_ptr);
	}
	bool success = (cdir != NULL
			&& fat_allocate
			&& inode_syml_create (inode_sector, target)
			&& dir_add (cdir, ctoken, inode_sector));
	// printf("in filesys_syml_create cdir:%s dir inode sector is %p\n", ctoken, inode_get_inumber(dir_get_inode(cdir)));
	// if(success) {
	// 	printf("success\n");
	// } else {
	// 	printf("fail\n");
	// }
	dir_close(cdir);
	if (!success && inode_sector != 0) fat_remove_chain(sector_to_cluster(inode_sector), 0);
	palloc_free_page(dir_copy);
	sema_up(&filesys_sema);
	return success;
}

#ifndef EFILESYS
/* Opens the file with the given NAME.
 * Returns the new file if successful or a null pointer
 * otherwise.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name) {

	sema_down(&filesys_sema);
	struct dir *dir = dir_open_root();
	struct inode *inode = NULL;

	if (dir != NULL)
		dir_lookup (dir, name, &inode);
	dir_close (dir);

	struct file *file = file_open(inode);
	sema_up(&filesys_sema);
	return  file; //file_open(inode);
}
#else
/* Opens the file with the given NAME.
 * Returns the new file if successful or a null pointer
 * otherwise.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name) {
	int a = 1;
	// printf("in filesys_open %d\n", a);
	a++;
	sema_down(&filesys_sema);

	struct thread *curr = thread_current();
	char *dir_copy;
	dir_copy = palloc_get_page (0);
	if (dir_copy == NULL) return false;
	strlcpy(dir_copy, name, PGSIZE);
	// 복사 과정 reference: process_create_initd (process.c)

	struct dir *cdir;
	if (dir_copy[0] == '/') {
		// printf("@@ if\n");
		cdir = dir_open_root();
	} else {
		// printf("@@ else\n");
		if(curr->wdir != NULL) {
			cdir = dir_reopen(curr->wdir);
			// printf("@@ else if\n");
		}
		else {
			cdir = dir_open_root();
			// printf("@@ else else\n");
			// printf("in filesys_open, root inode sector1 is %p\n", inode_get_inumber(dir_get_inode(cdir)));
		}
	}
	// printf("in filesys_open %d\n", a);
	// a++;
	struct inode *cinode;

	char *ctoken;
	char *ntoken;
	char *save_ptr;
	ctoken = strtok_r(dir_copy, "/", &save_ptr);
	ntoken = strtok_r(NULL, "/", &save_ptr);
	while (ctoken != NULL && ntoken != NULL) {
		// printf("in filesys_open in while\n");
		if (!dir_lookup(cdir, ctoken, &cinode))
		{
			dir_close(cdir);
			return false;
		}
		if(inode_check_syml(cinode)) cinode = syml_to_inode(cinode);
		if (!inode_check_dir(cinode))
		{
			dir_close(cdir);
			palloc_free_page(dir_copy);
			sema_up(&filesys_sema);
			return NULL;
		}
		dir_close(cdir);
		cdir = dir_open(cinode);

		ctoken = ntoken;
		ntoken = strtok_r(NULL, "/", &save_ptr);
	}
	// printf("in filesys_open, root inode sector2 is %p\n", inode_get_inumber(dir_get_inode(cdir)));
	if(ctoken == NULL) ctoken = ".";
	// 여기서 cdir 위에 ctoken 이름의 dir 만들면 됨
	// printf("in filesys_open %d\n", a);
	// a++;

	struct inode *inode = NULL;

	if (cdir != NULL)
		dir_lookup (cdir, ctoken, &inode);
	// printf("in filesys_open %d\n", a);
	// a++;
	// printf("in filesys_open %d\n", a);
	// a++;

	palloc_free_page(dir_copy);
	if (inode != NULL) {
		if(inode_check_syml(inode)) {
			// printf("in if\n");
			// printf("in filesys_open inode sector is %p\n", inode_get_inumber(inode));
			struct dir *old_dir = curr->wdir;
			curr->wdir = cdir;
			// printf("in filesys_open cdir inode sector is %p\n", inode_get_inumber(dir_get_inode(cdir)));
			inode = syml_to_inode(inode);
			curr->wdir = old_dir;
		}
	}
	// printf("in filesys_open %d\n", a);
	// a++;
	dir_close (cdir);
	struct file *file = file_open(inode);
	sema_up(&filesys_sema);
	return file;
}
#endif

#ifndef EFILESYS
/* Deletes the file named NAME.
 * Returns true if successful, false on failure.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) {

	sema_down(&filesys_sema);
	struct dir *dir = dir_open_root ();
	bool success = dir != NULL && dir_remove (dir, name);
	dir_close (dir);

	sema_up(&filesys_sema);
	return success;
}
#else
/* Deletes the file named NAME.
 * Returns true if successful, false on failure.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) {
	sema_down(&filesys_sema);
	struct thread *curr = thread_current();
	char *dir_copy;
	dir_copy = palloc_get_page (0);
	if (dir_copy == NULL) return false;
	strlcpy(dir_copy, name, PGSIZE);
	// 복사 과정 reference: process_create_initd (process.c)
	int a = 1;
	// printf("in filesys_remove %d\n", a);
	a++;
	struct dir *cdir;
	if (dir_copy[0] == '/') {
		cdir = dir_open_root();
	} else {
		if(curr->wdir != NULL) cdir = dir_reopen(curr->wdir);
		else cdir = dir_open_root();
	}
	// printf("in filesys_remove %d\n", a);
	a++;
	struct inode *cinode;

	char *ctoken;
	char *ntoken;
	char *save_ptr;
	ctoken = strtok_r(dir_copy, "/", &save_ptr);
	ntoken = strtok_r(NULL, "/", &save_ptr);
	while (ctoken != NULL && ntoken != NULL) {
		if(!dir_lookup(cdir, ctoken, &cinode)) {
			dir_close(cdir);
			palloc_free_page(dir_copy);
			sema_up(&filesys_sema);
			return false;
		} 
		if(inode_check_syml(cinode)) cinode = syml_to_inode(cinode);
		if(!inode_check_dir(cinode)) {
			dir_close(cdir);
			palloc_free_page(dir_copy);
			sema_up(&filesys_sema);
			return false;
		}
		dir_close(cdir);
		cdir = dir_open(cinode);

		ctoken = ntoken;
		ntoken = strtok_r(NULL, "/", &save_ptr);
	}
	// printf("in filesys_remove %d\n", a);
	// a++;

	// printf("in filesys_remove cdir is %p ctoken is %s\n", inode_get_inumber(dir_get_inode(cdir)), ctoken);
	bool success = cdir != NULL && dir_remove(cdir, ctoken);
	dir_close(cdir);
	// printf("in filesys_remove %d\n", a);
	// a++;
	// printf("in filesys_remove %d\n", a);
	// a++;
	palloc_free_page(dir_copy);
	// printf("in filesys_remove %d\n", a);
	// a++;
	// if(success) {
	// 	printf("success\n");
	// } else {
	// 	printf("fail\n");
	// }
	sema_up(&filesys_sema);
	return success;
}
#endif

/* Formats the file system. */
static void
do_format (void) {
	printf ("Formatting file system...");

#ifdef EFILESYS
	/* Create FAT and save it to the disk. */
	fat_create ();
	if (!dir_create(cluster_to_sector(ROOT_DIR_CLUSTER), 16)) {
			PANIC("root directory creation failed");
	}
	struct dir* rdir = dir_open_root();
	dir_add(rdir, ".", cluster_to_sector(ROOT_DIR_CLUSTER));
	dir_add(rdir, "..", cluster_to_sector(ROOT_DIR_CLUSTER));
	dir_close(rdir);
	fat_close ();
#else
	free_map_create ();
	if (!dir_create (ROOT_DIR_SECTOR, 16))
		PANIC ("root directory creation failed");
	free_map_close ();
#endif

	printf ("done.\n");
}
