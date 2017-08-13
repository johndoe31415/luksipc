Conversion problems
===================
If anything goes wrong, you will find advice in this section. Again the two
distinct cases will be in different subsections. Errors with plain to LUKS
conversion will be discussed first and errors with LUKS to LUKS conversion
(reLUKSificiation) in a different subsection.


Problems during plain to LUKS conversion
----------------------------------------
You may find yourself here because a luksipc process has crashed mid-conversion
(accidental Ctrl-C or reboot) and you're panicing. Breathe. luksipc is designed
so that it is robust against these issues.

Basically, to be able to resume a luksipc process you need to have two things:

1. The data of the last overwritten block (there's always one "shadow" block
   that needs to be kept in memory, because usually the destination partition is
   smaller than the source partition because of the LUKS header)
2. The exact location of where the interruption occured.

luksipc stores exactly this (incredibly critical) information in a "resume
file" should the resume process be interrupted. It is usually called
"resume.bin". For example, say I interrupt the LUKS conversion of a disk, this
will be shown::

    # luksipc -d /dev/sdf1
    [...]
    [I]:  0:00:  32.1%       110 MiB / 343 MiB     6.4 MiB/s   Left:     233 MiB  0:00 h:m
    ^C[C]: Shutdown requested by user interrupt, please be patient...
    [I]: Gracefully shutting down.
    [I]: Synchronizing disk...
    [I]: Synchronizing of disk finished.

If you go into more detail (log level increase) here's what you'll see::

    # luksipc -d /dev/sdf1 -l4
    [...]
    [I]:  0:00:  32.1%       110 MiB / 343 MiB     6.2 MiB/s   Left:     233 MiB  0:00 h:m
    ^C[C]: Shutdown requested by user interrupt, please be patient...
    [I]: Gracefully shutting down.
    [D]: Wrote resume file: read pointer offset 136314880 write pointer offset 115343360, 10485760 bytes of data in active buffer.
    [D]: Closing read/write file descriptors 4 and 5.
    [I]: Synchronizing disk...
    [I]: Synchronizing of disk finished.
    [D]: Subprocess [PID 17857]: Will execute 'cryptsetup luksClose luksipc_f569b0bb'
    [D]: Subprocess [PID 17857]: cryptsetup returned 0
    [D]: Subprocess [PID 17860]: Will execute 'dmsetup remove /dev/mapper/alias_luksipc_raw_277f5e96'
    [D]: Subprocess [PID 17860]: dmsetup returned 0

You can see the exact location of the interruption: The read pointer was at
offset 136314880 (130 MiB), the write pointer was at offset 115343360 (110 MiB)
and there are currently 10 MiB of data in the shadow buffer. Everything was
saved to a resume file. Here's an illustration of what it looks like. Every
block is 10 MiB in size::

           100    110    120    130    140
            |      |      |      |      |
            v      v      v      v      v
        ----+------+------+------+------+----
         ...|      | BUF1 | BUF2 |      |...
        ----+------+------+------+------+----
                   ^             ^
                   |             |
                   W             R

At this point in time, luksipc has exactly two blocks in memory, BUF1 and BUF2.
This is why the read pointer is ahead two block sizes of the write pointer. Now
in the next step (if no interruption had occured) the BUF1 buffer would be
written to the LUKS device offset 110 MiB. This would overwrite some of the
plain data in BUF2, too (because the LUKS header means that there's an offset
between read- and write disk!). Therefore both have to be kept in memory.

But since the system was interrupted, it is fully sufficient to only save BUF1
to disk together with the write pointer location.

With the help of this resume file, you can continue the conversion process::

    # luksipc -d /dev/sdf1 --resume resume.bin
    [...]
    [I]: Starting copying of data, read offset 125829120, write offset 115343360
    [I]:  0:00:  64.1%       220 MiB / 343 MiB     6.6 MiB/s   Left:     123 MiB  0:00 h:m
    [I]:  0:00:  93.3%       320 MiB / 343 MiB     9.2 MiB/s   Left:      23 MiB  0:00 h:m
    [...]

Now we see that the process was resumed with the write pointer at the 110 MiB
mark and the read pointer at the 120 MiB mark. The next step would now be for
luksipc to read in BUF2 and we're exatly in the situation in which the abort
occured. Then from there on everything works like usual.

One thing you have to be very careful about is making copies of the resume
file. You have to be **very** careful about this. Let's say you copied the
resume file to some other location and accidently applied it twice. For
example, you run luksipc a first time and abort it. The resume file is written,
you copy it to resume2.bin. You resume the process (luksipc run 2) and let it
finish. Then you resume the process again with resume2.bin. What will happen is
that all data that was written in the resume run is encrypted **twice** and
will be unintelligible.  This can obviously be recovered, but it will require
very careful twiddling and lots of work. Just don't do it.

To prevent this sort of thing, luksipc truncates the resume file when resuming
only after everything else has worked (and I/O operation starts). This prevents
you from accidently applying a resume file twice to an interrupted conversion
process.



Problems during LUKS to LUKS conversion
---------------------------------------
When a reLUKSification process aborts unexpectedly (but gracefully), a resume
file is written just as it would have been during LUKSification. So resuming
just like above is easily possible.  But suppose the case is a tad bit more
complicated: Let's say that someone accidently issued a reboot command during
reLUKSification. The reboot command causes a SIGTERM to be issued to the
luksipc process.  luksipc catches the signal, writes the resume.bin file and
shuts down gracefully. Then the system reboots.

For reLUKSification to work you need to have access to the plain (unlocked)
source container. Here's the big "but": In order to unlock the original
container, you need to use cryptsetup luksOpen. But the LUKS header has been
overwritten by the destination (final) LUKS header already. Therefore you can't
unlock the source data anymore.

At least you couldn't if this situation wouldn't have been anticipated by
luksipc. Lucky for you, it has been. When first firing up luksipc, a backup of
the raw device header (typically 128 MiB in size) is done by luksipc in a file
usually called "header_backup.img". You can use this header together with the
raw parition to open the partition using the old key. When you have opened the
device with the old key, we can just resume the process as we normally would.

First, this is the reLUKSificiation process that aborts. We assume our
container is unlocked at /dev/mapper/oldluks. Let's check the MD5 of the
container first (to verify everything ran smoothly)::

    # md5sum /dev/mapper/oldluks
    41dc86251cba7992719bbc85de5628ab  /dev/mapper/oldluks

Alright, let's start the luksipc process (which will be interrupted)::

    # luksipc -d /dev/loop0 --readdev /dev/mapper/oldluks
    [...]
    [I]:  0:00:  10.8%       110 MiB / 1022 MiB     0.0 MiB/s   Left:     912 MiB  0:00 h:m
    ^C[C]: Shutdown requested by user interrupt, please be patient...
    [I]: Gracefully shutting down.
    [...]

Now let's say we've closed /dev/mapper/oldluks (e.g. by a system reboot). We
need to find a way to reopen it with the old header and old key in order to
successfully resume the proces. For this, we do::

    # cryptsetup luksOpen --header=header_backup.img /dev/loop0 oldluks

And then, finally, we're able to resume luksipc::

    # luksipc -d /dev/loop0 --readdev /dev/mapper/oldluks --resume resume.bin
    [...]
    [I]: Starting copying of data, read offset 220200960, write offset 209715200
    [I]:  0:00:  30.3%       310 MiB / 1022 MiB     0.0 MiB/s   Left:     712 MiB  0:00 h:m
    [I]:  0:00:  40.1%       410 MiB / 1022 MiB   147.9 MiB/s   Left:     612 MiB  0:00 h:m

Now after the process is run, let's do some cleanups::

    # dmsetup remove oldluks
    # dmsetup remove hybrid
    # losetup -d /dev/loop3

And open our successfully converted device::

    # cryptsetup luksOpen /dev/loop0 newluks -d /root/initial_keyfile.bin

But did it really work? We can check::

    # md5sum /dev/mapper/newluks
    41dc86251cba7992719bbc85de5628ab  /dev/mapper/newluks

Yes, it sure did :-)

Be aware that this is an absolute emergency recovery proedure that you'd only
use if everything else fails (i.e. the original source LUKS device was
accidently closed).  Any mistake whatsoever (e.g. wrong offsets) will cause you
to completely pulp your disk. So be very very careful with this and double
check everything.
