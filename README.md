# preallocate
IO helper for suppress [fragmentation](https://en.wikipedia.org/wiki/File_system_fragmentation) of large files(backup, Blu-ray image, SDHC-card etc).

###### (or thing just I using daily basis)



## what the heck is this for?
for example:
```
$ dd if=/dev/vg/lv_large_snapshot bs=4096 | \
    gzip > /mnt/backup/backup_2020_06_06.gz
```
depend on `/mnt/backup` contents however this is not good in term of fragmentation. because bash(or zsh or whatever) is no way to know `> /mnt/backup/backup_2020_06_06.gz`'s size, before work done. it's mean, no way to use [fallocate()](https://www.man7.org/linux/man-pages/man2/fallocate.2.html).

let's see actual 60GB file example.

```bash
$ dd if=/dev/vg/lv_large_snapshot bs=4096 | \
  gzip | dd of=/mnt/backup/backup_2020_06_06.gz status=progress
~~snip~~
$
$ filefrag -v /mnt/backup/backup_2020_06_06.gz
Filesystem type is: 58465342
File size of /mnt/backup/backup_2020_06_06.gz is 65143209044 (15904104 blocks of 4096 bytes)
 ext:     logical_offset:        physical_offset: length:   expected: flags:
   0:        0..  262127:  142449069.. 142711196: 262128:
   1:   262128.. 1048559:  146483443.. 147269874: 786432:  142711197:
   2:  1048560.. 2097135:  148304107.. 149352682: 1048576:  147269875:
   3:  2097136.. 3145711:  149352683.. 150401258: 1048576:
   4:  3145712.. 5242862:   73590272..  75687422: 2097151:  150401259:
   5:  5242863.. 7145021:    2604764..   4506922: 1902159:   75687423:
   6:  7145022.. 7339998:    2080376..   2275352: 194977:    4506923:
   7:  7339999.. 8388559:   24583686..  25632246: 1048561:    2275353:
   8:  8388560.. 9437135:   66764570..  67813145: 1048576:   25632247:
   9:  9437136..10938887:  132545031.. 134046782: 1501752:   67813146:
  10: 10938888..11534286:  134060490.. 134655888: 595399:  134046783:
  11: 11534287..12582847:  150401259.. 151449819: 1048561:  134655889:
  12: 12582848..13631423:  156011326.. 157059901: 1048576:  151449820:
  13: 13631424..15068563:   71649882..  73087021: 1437140:  157059902:
  14: 15068564..15728574:   70674710..  71334720: 660011:   73087022:
  15: 15728575..15904103:   95949946..  96125474: 175529:   71334721: last,eof
/mnt/backup/backup_2020_06_06.gz: 15 extents found
```
filesystem is XFS, about 90% used. file is fragmented in this case, compared to
```bash
$ rm /mnt/backup/backup_2020_06_06.gz
$ sync
$ fallocate -l 65143209044 /mnt/backup/backup_2020_06_06.gz
$ filefrag -v /mnt/backup/backup_2020_06_06.gz
Filesystem type is: 58465342
File size of /mnt/backup/backup_2020_06_06.gz is 65143209044 (15904104 blocks of 4096 bytes)
 ext:     logical_offset:        physical_offset: length:   expected: flags:
   0:        0.. 2097150:  148304107.. 150401257: 2097151:             unwritten
   1:  2097151.. 4194301:   73590272..  75687422: 2097151:  150401258: unwritten
   2:  4194302.. 6096460:    2604764..   4506922: 1902159:   75687423: unwritten
   3:  6096461.. 7598212:  132545031.. 134046782: 1501752:    4506923: unwritten
   4:  7598213.. 9035352:   71649882..  73087021: 1437140:  134046783: unwritten
   5:  9035353..10451117:  150401258.. 151817022: 1415765:   73087022: unwritten
   6: 10451118..11756279:  146483443.. 147788604: 1305162:  151817023: unwritten
   7: 11756280..13031353:  156011326.. 157286399: 1275074:  147788605: unwritten
   8: 13031354..14221255:   24583686..  25773587: 1189902:  157286400: unwritten
   9: 14221256..15299485:   66764570..  67842799: 1078230:   25773588: unwritten
  10: 15299486..15904103:   70059668..  70664285: 604618:   67842800: last,unwritten,eof
/mnt/backup/backup_2020_06_06.gz: 11 extents found
$
$ sudo xfs_fsr -v /mnt/backup/backup_2020_06_06.gz
No improvement will be made (skipping): /mnt/backup/backup_2020_06_06.gz
$
$ dd if=/dev/vg/lv_large_snapshot bs=4096 | gzip | \
  dd of=/mnt/backup/backup_2020_06_06.gz conv=notrunc status=progress
...
```

notice `length` column became descending order. because allocate as large-freespace as possible first (at least with XFS). well done `fallocate`.
(also `conv=notrunc` option was used with dd)

reason for do this is not fragmentation of large file, but **keep small free space for small file**.


----

effectivery `preallocate` is same thing but one line.

```
$ dd if=/dev/vg/lv_large_snapshot bs=4096 | \
  gzip | preallocate -l 60gb /mnt/backup/backup_2020_06_06.gz
```

`-l 60gb` is kind of **hint** to how many extents require. **no need to exactly**.


this is actual code without error handling.
```c
static int preallocate_io(int fd, off_t len, int opt_sync)
{
    // allocate
    fallocate(fd, 0, 0L, len);

    // read/write stream
    off_t wrote = do_read_write(fd, opt_sync);
 
    // truncate
    if (wrote < len) {
        ftruncate(fd, wrote);
    }
    return 0;
}
```

----

## `fiemap` script

`fiemap.py` spits out image of extents in device.

`$ python fiemap/fiemap.py <mount-point or dir or file>`

![](https://i.imgur.com/GLhrWX0.png)


## TLDR;
- keep small free space for small files
- file fragmentation is caused by free space fragmentation
- free space fragmentation is not easily avoidable, because people delete file.
- [delayed allocation](https://en.wikipedia.org/wiki/Allocate-on-flush) desen't much help when you dealing with single 100GiB file
- (for SSD, not much difference I guess)










# link
- [File system fragmentation - Wikipedia](https://en.wikipedia.org/wiki/File_system_fragmentation#Preventing_fragmentation)


- [Re: [RFC] fallocate utility](https://lists.gnu.org/archive/html/bug-coreutils/2009-07/msg00255.html)
  - it is already suggested this kind of things in coreutils. however ansower is obvious "Do one thing and do it well".
