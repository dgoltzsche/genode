/*
 * \brief  Directory file system
 * \author Norman Feske
 * \date   2012-04-23
 */

/*
 * Copyright (C) 2011-2014 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

#ifndef _INCLUDE__VFS__DIR_FILE_SYSTEM_H_
#define _INCLUDE__VFS__DIR_FILE_SYSTEM_H_

#include <vfs/file_system_factory.h>
#include <vfs/vfs_handle.h>


namespace Vfs { class Dir_file_system; }


class Vfs::Dir_file_system : public File_system
{
	public:

		enum { MAX_NAME_LEN = 128 };

	private:

		/* pointer to first child file system */
		File_system *_first_file_system;

		/* add new file system to the list of children */
		void _append_file_system(File_system *fs)
		{
			if (!_first_file_system) {
				_first_file_system = fs;
				return;
			}

			File_system *curr = _first_file_system;
			while (curr->next)
				curr = curr->next;

			curr->next = fs;
		}

		/**
		 * Directory name
		 */
		char _name[MAX_NAME_LEN];

		bool _is_root() const { return _name[0] == 0; }

	public:

		Dir_file_system(Xml_node node, File_system_factory &fs_factory)
		:
			_first_file_system(0)
		{
			/* remember directory name */
			if (node.has_type("fstab") || node.has_type("vfs"))
				_name[0] = 0;
			else
				node.attribute("name").value(_name, sizeof(_name));

			for (unsigned i = 0; i < node.num_sub_nodes(); i++) {

				Xml_node sub_node = node.sub_node(i);

				/* traverse into <dir> nodes */
				if (sub_node.has_type("dir")) {
					_append_file_system(new (env()->heap())
					                    Dir_file_system(sub_node, fs_factory));
					continue;
				}

				File_system *fs = fs_factory.create(sub_node);
				if (fs) {
					_append_file_system(fs);
					continue;
				}

				char type_name[64];
				sub_node.type_name(type_name, sizeof(type_name));
				PWRN("unknown fstab node type <%s>", type_name);
			}
		}

		/**
		 * Return portion of the path after the element corresponding to
		 * the current directory.
		 */
		char const *_sub_path(char const *path) const
		{
			/* do not strip anything from the path when we are root */
			if (_is_root())
				return path;

			/* skip heading slash in path if present */
			if (path[0] == '/')
				path++;

			size_t const name_len = strlen(_name);
			if (strcmp(path, _name, name_len) != 0)
				return 0;
			path += name_len;

			/*
			 * The first characters of the first path element are equal to
			 * the current directory name. Let's check if the length of the
			 * first path element matches the name length.
			 */
			if (*path != 0 && *path != '/')
				return 0;

			return path;
		}


		/*********************************
		 ** Directory-service interface **
		 *********************************/

		Dataspace_capability dataspace(char const *path) override
		{
			path = _sub_path(path);
			if (!path)
				return Dataspace_capability();

			/*
			 * Query sub file systems for dataspace using the path local to
			 * the respective file system
			 */
			File_system *fs = _first_file_system;
			for (; fs; fs = fs->next) {
				Dataspace_capability ds = fs->dataspace(path);
				if (ds.valid())
					return ds;
			}

			return Dataspace_capability();
		}

		void release(char const *path, Dataspace_capability ds_cap) override
		{
			path = _sub_path(path);
			if (!path)
				return;

			for (File_system *fs = _first_file_system; fs; fs = fs->next)
				fs->release(path, ds_cap);
		}

		Stat_result stat(char const *path, Stat &out) override
		{
			path = _sub_path(path);

			/* path does not match directory name */
			if (!path)
				return STAT_ERR_NO_ENTRY;

			/*
			 * If path equals directory name, return information about the
			 * current directory.
			 */
			if (strlen(path) == 0 || (strcmp(path, "/") == 0)) {
				out.size = 0;
				out.mode = STAT_MODE_DIRECTORY | 0755;
				out.uid  = 0;
				out.gid  = 0;
				return STAT_OK;
			}

			/*
			 * The given path refers to one of our sub directories.
			 * Propagate the request into our file systems.
			 */
			for (File_system *fs = _first_file_system; fs; fs = fs->next) {

				Stat_result const err = fs->stat(path, out);

				if (err == STAT_OK)
					return err;

				if (err != STAT_ERR_NO_ENTRY)
					return err;
			}

			/* none of our file systems felt responsible for the path */
			return STAT_ERR_NO_ENTRY;
		}

		/**
		 * The 'path' is relative to the child file systems.
		 */
		Dirent_result _dirent_of_file_systems(char const *path, off_t index, Dirent &out)
		{
			int base = 0;
			for (File_system *fs = _first_file_system; fs; fs = fs->next) {

				/*
				 * Determine number of matching directory entries within
				 * the current file system.
				 */
				int const fs_num_dirent = fs->num_dirent(path);

				/*
				 * Query directory entry if index lies with the file
				 * system.
				 */
				if (index - base < fs_num_dirent) {
					index = index - base;
					Dirent_result const err = fs->dirent(path, index, out);
					out.fileno += base;
					return err;
				}

				/* adjust base index for next file system */
				base += fs_num_dirent;
			}

			out.type = DIRENT_TYPE_END;
			return DIRENT_OK;
		}

		void _dirent_of_this_dir_node(off_t index, Dirent &out)
		{
			if (index == 0) {
				strncpy(out.name, _name, sizeof(out.name));

				out.type = DIRENT_TYPE_DIRECTORY;
				out.fileno = 1;
			} else {
				out.type = DIRENT_TYPE_END;
			}
		}

		Dirent_result dirent(char const *path, off_t index, Dirent &out) override
		{
			if (_is_root())
				return _dirent_of_file_systems(path, index, out);

			if (strcmp(path, "/") == 0) {
				_dirent_of_this_dir_node(index, out);
				return DIRENT_OK;
			}

			/* path contains at least one element */

			/* remove current element from path */
			path = _sub_path(path);

			/* path does not lie within our tree */
			if (!path)
				return DIRENT_ERR_INVALID_PATH;

			return _dirent_of_file_systems(path, index, out);
		}

		/*
		 * Accumulate number of directory entries that match in any of
		 * our sub file systems.
		 */
		size_t _sum_dirents_of_file_systems(char const *path)
		{
			size_t cnt = 0;
			for (File_system *fs = _first_file_system; fs; fs = fs->next) {
				cnt += fs->num_dirent(path);
			}
			return cnt;
		}

		size_t num_dirent(char const *path)
		{
			if (_is_root()) {
				return _sum_dirents_of_file_systems(path);

			} else {

				if (strcmp(path, "/") == 0)
					return 1;

				/*
				 * The path contains at least one element. Remove current
				 * element from path.
				 */
				path = _sub_path(path);

				/*
				 * If the resulting 'path' is non-NULL, the path lies
				 * within our tree. In this case, determine the sum of
				 * matching dirents of all our file systems. Otherwise,
				 * the specified path lies outside our directory node.
				 */
				return path ? _sum_dirents_of_file_systems(path) : 0;
			}
		}

		bool is_directory(char const *path)
		{
			path = _sub_path(path);
			if (!path)
				return false;

			if (strlen(path) == 0)
				return true;

			for (File_system *fs = _first_file_system; fs; fs = fs->next)
				if (fs->is_directory(path))
					return true;

			return false;
		}

		char const *leaf_path(char const *path)
		{
			path = _sub_path(path);
			if (!path)
				return 0;

			if (strlen(path) == 0)
				return path;

			for (File_system *fs = _first_file_system; fs; fs = fs->next) {
				char const *leaf_path = fs->leaf_path(path);
				if (leaf_path)
					return leaf_path;
			}

			return 0;
		}

		Open_result open(char const *path, unsigned mode, Vfs_handle **out_handle) override
		{
			/*
			 * If 'path' is a directory, we create a 'Vfs_handle'
			 * for the root directory so that subsequent 'dirent' calls
			 * are subjected to the stacked file-system layout.
			 */
			if (is_directory(path)) {
				*out_handle = new (env()->heap()) Vfs_handle(*this, *this, 0);
				return OPEN_OK;
			}

			/*
			 * If 'path' refers to a non-directory node, create a
			 * 'Vfs_handle' local to the file system that provides the
			 * file.
			 */

			path = _sub_path(path);

			/* check if path does not match directory name */
			if (!path)
				return OPEN_ERR_UNACCESSIBLE;

			/* path equals directory name */
			if (strlen(path) == 0) {
				*out_handle = new (env()->heap()) Vfs_handle(*this, *this, 0);
				return OPEN_OK;
			}

			/* path refers to any of our sub file systems */
			for (File_system *fs = _first_file_system; fs; fs = fs->next) {

				Open_result const err = fs->open(path, mode, out_handle);

				if (err == OPEN_OK)
					return err;
			}

			/* path does not match any existing file or directory */
			return OPEN_ERR_UNACCESSIBLE;
		}

		Unlink_result unlink(char const *path)
		{
			path = _sub_path(path);

			/* path does not match directory name */
			if (!path)
				return UNLINK_ERR_NO_ENTRY;

			/*
			 * Prevent unlinking if path equals directory name defined
			 * via the static fstab configuration.
			 */
			if (strlen(path) == 0)
				return UNLINK_ERR_NO_PERM;

			/*
			 * The given path refers to at least one of our sub
			 * directories. Propagate the request into all of our file
			 * systems. If at least one unlink operation succeeded, we
			 * return success.
			 */
			for (File_system *fs = _first_file_system; fs; fs = fs->next) {

				Unlink_result const err = fs->unlink(path);

				if (err == UNLINK_OK)
					return err;

				/*
				 * Keep the most meaningful error code. When using stacked file
				 * systems, most child file systems will eventually return
				 * 'UNLINK_ERR_NO_ENTRY' (or leave the error code unchanged).
				 * If any of those file systems has anything more interesting
				 * to tell (in particular 'UNLINK_ERR_NO_PERM'), return this
				 * information.
				 */
				if (err != UNLINK_ERR_NO_ENTRY)
					return err;
			}

			/* none of our file systems could successfully unlink the path */
			return UNLINK_ERR_NO_ENTRY;
		}

		Readlink_result readlink(char const *path, char *buf, size_t buf_size,
		                      size_t &out_len)
		{
			path = _sub_path(path);

			/* path does not match directory name */
			if (!path)
				return READLINK_ERR_NO_ENTRY;

			/* path refers to any of our sub file systems */
			for (File_system *fs = _first_file_system; fs; fs = fs->next) {

				Readlink_result const err = fs->readlink(path, buf, buf_size, out_len);

				if (err == READLINK_OK)
					return err;

				if (err != READLINK_ERR_NO_ENTRY)
					return err;
			}

			/* none of our file systems could read the link */
			return READLINK_ERR_NO_ENTRY;
		}

		Rename_result rename(char const *from_path, char const *to_path)
		{
			from_path = _sub_path(from_path);

			/* path does not match directory name */
			if (!from_path)
				return RENAME_ERR_NO_ENTRY;

			/*
			 * Prevent renaming if path equals directory name defined
			 * via the static fstab configuration.
			 */
			if (strlen(from_path) == 0)
				return RENAME_ERR_NO_PERM;

			/*
			 * Check if destination path resides within the same file
			 * system instance as the source path.
			 */
			to_path = _sub_path(to_path);
			if (!to_path)
				return RENAME_ERR_CROSS_FS;

			/* path refers to any of our sub file systems */
			for (File_system *fs = _first_file_system; fs; fs = fs->next) {

				Rename_result const err = fs->rename(from_path, to_path);

				if (err == RENAME_OK)
					return err;

				if (err != RENAME_ERR_NO_ENTRY)
					return err;
			}

			/* none of our file systems could successfully rename the path */
			return RENAME_ERR_NO_ENTRY;
		}

		Symlink_result symlink(char const *from, char const *to)
		{
			char const *path = _sub_path(to);

			/* path does not match directory name */
			if (!path)
				return SYMLINK_ERR_NO_ENTRY;

			/*
			 * Prevent symlink of path that equals directory name defined
			 * via the static fstab configuration.
			 */
			if (strlen(path) == 0)
				return SYMLINK_ERR_EXISTS;

			/* path refers to any of our sub file systems */
			for (File_system *fs = _first_file_system; fs; fs = fs->next) {

				Symlink_result const err = fs->symlink(from, path);

				if (err == SYMLINK_OK)
					return err;

				if (err != SYMLINK_ERR_NO_ENTRY)
					return err;
			}

			/* none of our file systems could create the symlink */
			return SYMLINK_ERR_NO_ENTRY;
		}

		Mkdir_result mkdir(char const *path, unsigned mode)
		{
			path = _sub_path(path);

			/* path does not match directory name */
			if (!path)
				return MKDIR_ERR_NO_ENTRY;

			/*
			 * Prevent mkdir of path that equals directory name defined
			 * via the static fstab configuration.
			 */
			if (strlen(path) == 0)
				return MKDIR_ERR_EXISTS;

			/* path refers to any of our sub file systems */
			for (File_system *fs = _first_file_system; fs; fs = fs->next) {

				Mkdir_result const err = fs->mkdir(path, mode);

				if (err == MKDIR_OK)
					return err;

				if (err != MKDIR_ERR_NO_ENTRY)
					return err;
			}

			/* none of our file systems could create the directory */
			return MKDIR_ERR_NO_ENTRY;
		}


		/***************************
		 ** File_system interface **
		 ***************************/

		char const *name() const { return "dir"; }

		/**
		 * Synchronize all file systems
		 */
		void sync() override
		{
			for (File_system *fs = _first_file_system; fs; fs = fs->next)
				fs->sync();
		}


		/********************************
		 ** File I/O service interface **
		 ********************************/

		Write_result write(Vfs_handle *handle, char const *, size_t, size_t &) override
		{
			return WRITE_ERR_INVALID;
		}

		Read_result read(Vfs_handle *, char *, size_t, size_t &) override
		{
			return READ_ERR_INVALID;
		}

		Ftruncate_result ftruncate(Vfs_handle *, size_t) override
		{
			return FTRUNCATE_ERR_NO_PERM;
		}
};

#endif /* _INCLUDE__VFS__DIR_FILE_SYSTEM_H_ */
