#ifndef CON_FILE_H
#define CON_FILE_H

#define MAGIC 0xC04E55ED
#define MEGABYTE 1024*1024*1024

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#define exit_safely() do { 		\
	free(h);					\
	fclose(fp);					\
	return NULL; } while (0);	\


/* The container. */
struct containerhandle
{
	char name[32];
	size_t files;

	FILE *fp;

	size_t loffset;
	size_t eindex; 

	size_t optblk;

	struct file_index *f;

	void (*filename_callback)(const char*, const char*);
};

typedef struct containerhandle con;

struct file_index
{
	struct _file_header **files;
	size_t len;
	size_t capacity;
};

typedef struct file_index file_index;

/* The header at the top of the file. Determines information about it, so it can be retrieved. */
struct __attribute__((__packed__)) con_header
{
	int magic;
	char name[32];
	size_t files;
};

/* The header describing the file. */

/* Padded for fast access. */
struct _file_header
{
	char name[64];
	char directory[32];

	size_t length;
	size_t offset;
	size_t mode;
};

/* For the actual header. */
struct __attribute__((__packed__)) file_header
{
	char name[64];
	char directory[32];

	size_t length;
	size_t offset;
	size_t mode;
};

size_t optimal_block_size()
{
	struct stat s;
	stat("/", &s);
	return s.st_blksize;
}

/* Calculate the end of the index based on how many files there are. (Every struct is the same size!) */
/* Used for setting the file pointer for actually getting the file data. */

size_t con_end_of_index(con *h)
{
	return sizeof(struct con_header) + (sizeof(struct file_header) * h->f->len);
}

/* Get the file index when reading a container. */
file_index *con_get_index(con *h)
{
	file_index *f = (file_index*) malloc(sizeof *f);
	f->files = (struct _file_header**) malloc(h->files * sizeof(struct _file_header*));
	f->len = 0;

	size_t prev = ftell(h->fp);

	fseek(h->fp, sizeof(struct con_header), SEEK_SET);

	for (size_t i = 0; i < h->files; i++, f->len++)
	{
		struct file_header _hdr;
		struct _file_header *hdr = (struct _file_header*) malloc(sizeof(struct _file_header));
		fread(&_hdr, 1, sizeof(struct file_header), h->fp);

		strcpy(hdr->directory, _hdr.directory);
		strcpy(hdr->name, _hdr.name);
		
		hdr->offset = _hdr.offset;
		hdr->length = _hdr.length;

		f->files[f->len] = hdr;
	}

	fseek(h->fp, prev, SEEK_SET);

	return f;
}

/* Open a container in memory to return a con struct pointer. To read an existing one, set read to 1. */
con *init_container(const char *name, const char *filename, bool read)
{
	con *h = (con*) malloc(sizeof *h);
	FILE *fp = fopen(filename, read ? "rb+" : "wb+");

	h->fp = fp;
	h->filename_callback = NULL; 
	h->files = 0;
	h->loffset = 0;
	h->optblk = optimal_block_size();
	strncpy(h->name, name, sizeof(h->name));

	if (read)
	{
		struct con_header hd;
		fread(&hd, 1, sizeof(struct con_header), fp);
		
		if (hd.magic != MAGIC)
			exit_safely();

		h->files = hd.files;
		strncpy(h->name, hd.name, sizeof(h->name));

		h->f = con_get_index(h);
		h->eindex = con_end_of_index(h);
	}
	else
	{
		h->f = (file_index*) malloc(sizeof(file_index));
		h->f->len = 0;
		h->f->capacity = 8;
		h->f->files = (struct _file_header**) malloc(h->f->capacity * sizeof(struct _file_header*));

		/* Go past the container header, to the index headers. */
		fseek(h->fp, sizeof(struct con_header), SEEK_SET);
	}


	return h;	
}

/* Call this on a returned index to avoid a memory leak. */
void con_index_free(file_index *f)
{
	for (size_t i = 0; i < f->len; i++)
	{
		free(f->files[i]);
	}

	free(f->files);
	free(f);
}

/* Add a file to the handle's index. */
void con_add_file(con *h, const char *dir, const char *filename)
{
	struct _file_header *hdr = (struct _file_header*) malloc(sizeof(struct _file_header));
	
	strncpy(hdr->directory, dir, sizeof(hdr->directory));
	hdr->length = 0;
	strncpy(hdr->name, filename, sizeof(hdr->name));


	/* Increase the allocation. */
	if (h->f->len == h->f->capacity)
	{
		/* Allocation is done exponentially. */
		h->f->capacity = h->f->capacity * 5;
		h->f->files = (struct _file_header**) realloc(h->f->files, sizeof(struct _file_header*) * h->f->capacity);
	}

	h->f->files[h->f->len] = hdr;
	h->f->len++;
	h->files++;
} 

/* This will actually start the containment once all of the files wanted are added. */
void con_write_files(con *h, char *source)
{
	char buffer[optimal_block_size()];
	
	/* Find the end of the index and fill it with zeros. */
	size_t index_size = con_end_of_index(h);
	posix_fallocate(fileno(h->fp), 0, index_size);

	/* Hold the index and data offset for this function's work only! */
	size_t index_offset = ftell(h->fp);
	size_t data_offset = index_offset + index_size;

	/* For every file that was added. */
	for (size_t i = 0; i < h->f->len; i++)
	{
		struct _file_header *file = h->f->files[i];

		/* Filename callback. */
		if (h->filename_callback != NULL)
			(*h->filename_callback)(file->directory, file->name);

		/* Initialize the packed struct. */
		struct file_header fh;
		memset(&fh, 0, sizeof(struct file_header));

		strncpy(fh.name, file->name, sizeof(fh.name));
		strncpy(fh.directory, file->directory, sizeof(fh.directory));
		
		/* Construct a meaningful path to get the file from, where source is the directory to compress. */
		char absolute[strlen(file->name) + strlen(file->directory) + strlen(source) + 8];
		memset(absolute, 0, sizeof(absolute));
		snprintf(absolute, sizeof(absolute), "%s/%s/%s", source, file->directory, file->name);

		FILE *fp = fopen(absolute, "rb");

		/* Obtain the length of the file in question. */
		fseek(fp, 0, SEEK_END);
		fh.length = ftell(fp);
		fseek(fp, 0, SEEK_SET);

		/* Write the offset which is relative to the end of the file index. */
		fh.offset = h->loffset;
		fwrite(&fh, 1, sizeof(struct file_header), h->fp);

		/* Okay, we're now writing the data of the file which should begin after the file index. */
		fseek(h->fp, data_offset, SEEK_SET);
		while (!feof(fp))
		{
			size_t len = fread(buffer, 1, sizeof(buffer), fp);
			fwrite(buffer, 1, len, h->fp);
		}

		fclose(fp);

		data_offset += fh.length; // The data offset has moved n-bytes (that of the size of the file.)
		h->loffset = ftell(h->fp) - index_size; // Relative to end of file index?
		index_offset += sizeof(struct file_header); // Increment the file index offset by its size.

		fseek(h->fp, index_offset, SEEK_SET); // Onto the next one!
	}
}

/* Get a file from the container by index and save it accordingly. */
/* This should be done for every file that the container has (according to its header). */
void con_get_file(con *h, size_t index, const char *to, const char *output)
{
	file_index *f = h->f;
	
	struct _file_header *fh = f->files[index];
	size_t offset = fh->offset;
	size_t length = fh->length;

	char buffer[optimal_block_size()];

	char absolutepath[strlen(fh->directory) + strlen(fh->name) + strlen(to == NULL ? "" : to) + 3];
	memset(absolutepath, 0, sizeof(absolutepath));
	
	if (to != NULL)
		snprintf(absolutepath, sizeof(absolutepath), "%s/", to);

	/* If there are directories that the file is in: */
	if (!!strcmp(fh->directory, ""))
	{
		strncat(absolutepath, fh->directory, sizeof(absolutepath));
		strncat(absolutepath, "/", sizeof(absolutepath));
		
		/* Pesky stack not clearing! */
		struct stat s;
		memset(&s, 0, sizeof(struct stat));

		stat(absolutepath, &s);

		/* Create each directory in the chain, separated by /, if it does not exist. */
		if (!(S_ISDIR(s.st_mode)))
		{
			char iterpath[sizeof(absolutepath)];
			memset(iterpath, 0, sizeof(iterpath));

			if (to != NULL)
			{
				strcpy(iterpath, to);
				strcat(iterpath, "/");
			}

			char *dirmut = strdup(fh->directory);
			char *token = strtok(dirmut, "/");

			while (token != NULL)
			{
				strcat(iterpath, token);
				strcat(iterpath, "/");

				mkdir(iterpath, 0755);

				token = strtok(NULL, "/");
			}
			
			free(dirmut);
		}
	}

	strcat(absolutepath, output != NULL ? output : fh->name);
	FILE *fp = fopen(absolutepath, "wb");
	
	if (h->filename_callback != NULL)
		(*h->filename_callback)(fh->directory, fh->name);

	/* We wish to get how many times we could read the file in intervals of sizeof(buffer) and then */
	/* what's left over. We do NOT wish to read byte-by-byte until filesize--that would be slow! */
	size_t iterations = floor(fh->length / sizeof(buffer));
	size_t fraction = fh->length % sizeof(buffer);

	size_t prev = ftell(h->fp);

	fseek(h->fp, h->eindex + fh->offset, SEEK_SET);

	/* Read buffer-sized pieces into the new file. */
	for (size_t i = 0; i < iterations; i++)
	{
		size_t len = fread(buffer, 1, sizeof(buffer), h->fp);
		fwrite(buffer, 1, len, fp);
	}

	/* Then, read the amount that is left over from the previous whole division. */
	if (fraction != 0)
	{
		size_t len = fread(buffer, 1, fraction, h->fp);
		fwrite(buffer, 1, len, fp);
	}

	fseek(h->fp, prev, SEEK_SET);
	fclose(fp);
}

/* Call this second-to-last when creating a container. It will write the header information which is imperative. */
void con_save(con *h)
{
	struct con_header head;
	
	/* Write the container header. The reason for doing this last is because now we have a count of all of */
	/* the files. It is also really inexpensive to do as we left space for it when we initialized this */
	/* container. */

	strncpy(head.name, h->name, sizeof(head.name));
	head.files = h->files;
	head.magic = MAGIC; // Endianness may affect this.

	fseek(h->fp, 0, SEEK_SET);
	fwrite(&head, 1, sizeof(struct con_header), h->fp);
}

/* Free the memory occupied by this container. Do this, lest you get a memory leak. */
void con_free(con *h)
{
	fclose(h->fp);
	con_index_free(h->f);
	free(h);
}

#endif