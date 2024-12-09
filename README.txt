Kevin Johnson

What I have done: 
Implemented read, rm, ln, mkdir, rmdir
For testing read I create multiple files of multiple lengths, including multiple blocks, and made sure that reading
worked in the first block, across blocks, and in the 2nd block.
For testing rm I tested making a file and removing and seeing that the file system was updated properly. I also tested removing a file
with a hardlink after I implemented hardlink.
for ln I tested making a link and seeing that it had the same inode as the file it was linked to.
for mkdir i tested making directories in the root directory as well making directories inside of directories and navigating between them to make
sure everything was correct.
for rmdir, i test removing directories, nested directories, unnested directory in another directory, and removing directories with files to make sure
they wont be removed.

Design description: not really sure what to put here. followed the design of EXT2 and what was given in the powerpoint. Code is mostly commented aswell.