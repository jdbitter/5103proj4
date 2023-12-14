This project was written by Ethan Dewitt and Jd Bitter.

In order to test our code, we executed the commands on the provided disks,
making sure that the functions like create, delete, and debug worked correctly.
In addition to running these on normal conditions, we also created a
fullInode disk with a size of 5 blocks with all the inodes taken to make sure
that create correctly handles there being no open inodes.

To test read, we tried it under different conditions through the use of cat,
making sure that the output and bytes read by our ./simplefs matched the results
of the provided ./simplefs-solution
