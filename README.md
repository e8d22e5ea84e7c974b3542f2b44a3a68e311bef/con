# con
An alternative to .tar with specialty in random-access of large archives. 

con is a minimalist version of .tar which lacks some features, such as preserving modification times and file permissions. Nonetheless, con can become much more efficient than tar when randomly accessing an archive with 10,000 files or more (or less, if they are big). This is due to the fact that con takes a different approach in how it stores the files.

To spare you the details, this is the help menu (as issued by -h/--help):

    contain - Contain files and their subdirectories into a single file, with support for random access.
    USAGE: 
    --compress/-c [result name] [source dir] - Compress a directory.
    --extract/-e [containment file] - Extract a container.
    --show-index/-s [containment file] - Show the index.
    --index/-i [containment file] [output file] - Select a file from the index and save it.
    --help/-h - Show this menu


con stores its magic number 0xC04E55ED in the first four bytes of the resulting con file, as seen in the initial con header--endianness will differ across systems, where little endian is preferred. con has two headers that it uses: *con_header* and *file_header*. 

*con_header* will store the aforementioned magic number as size *int*, as well as store the name of the archive and more importantly, the number of files it contains (if one is to lie about the number of files, undefined behavior happens; yes, con is not meant for production). After *con_header*, immediately begins the file headers.

    struct __attribute__((__packed__)) con_header
    {
	    int magic;
	    char name[32];
	    size_t files;
    };


*file_header* will describe each file that is in the archive and its characteristics needed to extract it: the name of the file, its directory, its file size, and its offset in the archive (relative to that of the end of the file indices). Importantly, unlike tar, the file header does not prefix the file in question: it merely points to it. This means that in order to get a list of all files, you only need to read the entirety of all indexes (the size given by `files * sizeof(struct file_header)`) , not all of the data of every file as well, like tar. Once you have the file you want, you seek to the end of the file header (the offset in the file for fseek given by `sizeof(struct con_header) + (files * sizeof(struct file_header))`) and go to the offset given by the given file header and read its length. 

    struct __attribute__((__packed__)) file_header
    {
	    char name[64];
	    char directory[32];
	    size_t length;
	    size_t offset;
	    size_t mode;
    };


When tested with 10,000 files with each a size of 1MB, con is approximately 167x faster than an a tar archive of the exact same characteristics, when accessing the 10,000th file. This is heavily dependent on various other factors, such as hard drive speed and processor speed (and even the implementation of con and tar); it is not a scientific test. 

In spite of the efficiency of random access in comparison to .tar, con is inferior and slower in general. con also has security flaws that have not been properly addressed. It is, therefore, not recommended for any actual practical usage.  

