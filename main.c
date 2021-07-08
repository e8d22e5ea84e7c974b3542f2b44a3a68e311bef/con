#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>

#include "file.h"

void print_file(const char *dir, const char *file)
{
    printf("Filename: %s/%s\n", dir, file);
}

/* Find files from a specific path and recursively add them to the container. */
void add_files(con *handle, const char *path)
{
    DIR *folder = opendir(path);
    struct dirent *entry;

    while ((entry = readdir(folder)))
    {
        /* UNIX artifacts. */
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;

        /* A folder, prepare to recursively execute this function again, with a new path. */
        if (entry->d_type == DT_DIR)
        {
            char newpath[256];
            memset(newpath, 0, sizeof(newpath));

            strncpy(newpath, path, sizeof(newpath));
            strncat(newpath, "/", sizeof(newpath)-2);
            strncat(newpath, entry->d_name, sizeof(newpath)-2);

            add_files(handle, newpath);
            continue;
        }

        char absolute[256];
        strncpy(absolute, path, sizeof(absolute));
        strncat(absolute, "/", sizeof(absolute) -2);
        strncat(absolute, entry->d_name, sizeof(absolute)-2);

        con_add_file(handle, strchr(path, '/') == NULL ? "" : strchr(path, '/')+1, entry->d_name);
        /*                   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ remove path of source folder. */ 
    }

    closedir(folder);
}

void create_container(const char *name, const char *source)
{
    char filename[strlen(name) + 4];
    sprintf(filename, "%s.%s", name, "con");

    con *handle = init_container(name, filename, false);

    handle->filename_callback = &print_file;

    add_files(handle, source);

    con_write_files(handle, source);

    con_save(handle);
    con_free(handle);
}

void extract_container(const char *name)
{
    con *handle = init_container(name, name, 1);
    handle->filename_callback = &print_file;
    
    mkdir(handle->name, 0755);

    for (size_t i = 0; i < handle->files; i++)
    {
        con_get_file(handle, i, handle->name, NULL);
    }

    con_free(handle);
}

void show_index(const char *file)
{
    con *handle = init_container(file, file, 1);
    file_index *f = handle->f;

    for (size_t i = 0; i < f->len; i++)
    {
        struct _file_header *fh = f->files[i];
        size_t offset = con_end_of_index(handle) + fh->offset;
        printf("[%d]: SIZE: %d, PATH: %s/%s, OFFSET: %lu or 0x%x\n", i, fh->length, fh->directory, fh->name, 
                                                                offset, offset);
    }


    con_free(handle);
}

void get_file_by_index(const char *archive, size_t index, const char *output)
{
    con *handle = init_container(archive, archive, 1);
    con_get_file(handle, index, NULL, output);
    con_free(handle);
}

void help()
{
    const char *menu =
    "contain - Contain files and their subdirectories into a single file, with support for random access.\n"
    "USAGE: \n"
    "--compress/-c [result name] [source dir] - Compress a directory.\n"
    "--extract/-e [containment file] - Extract a container.\n"
    "--show-index/-s [containment file] - Show the index.\n"
    "--index/-i [containment file] [output file] - Select a file from the index and save it.\n"
    "--help/-h - Show this menu\n";

    printf("%s\n", menu);
}

int main(int argc, char **argv, char **envp)
{
    if (argc < 2)
    {
        fprintf(stderr, "You need to provide an argument.\n");
        return 1;
    }

    char *operation = argv[1];

    if (!strcmp(operation, "--extract") || !strcmp(operation, "-e"))
    {
        if (argc < 3)
        {
            fprintf(stderr, "You need a filename to extract!\n");
            return 2;
        }

        char *filename = argv[2];
        extract_container(filename);
    }
    else if (!strcmp(operation, "--compress") || !strcmp(operation, "-c"))
    {
        if (argc < 4)
        {
            fprintf(stderr, "You need a name for the new archive and a source folder!\n");
            return 3;
        }

        char *name = argv[2];
        char *source = argv[3];
        create_container(name, source);
    }
    else if (!strcmp(operation, "--show-index") || !strcmp(operation, "-s"))
    {
        char *filename = argv[2];
        show_index(filename);
    }
    else if (!strcmp(operation, "--index") || !strcmp(operation, "-i"))
    {
        char *filename = argv[2];
        int index = atoi(argv[3]);
        char *output = argv[4];

        get_file_by_index(filename, index, output);
    }
    else if (!strcmp(operation, "--help") || !strcmp(operation, "-h"))
        help();
    else
    {
        fprintf(stderr, "Invalid operation!\n");
        help();
        return -1;
    }

    return 0;
}