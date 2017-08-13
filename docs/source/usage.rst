Usage
=====
The following section will now cover the basic usage of luksipc. There are two
distinct cases, one is the conversion of a unencrypted (plain) volume to LUKS
and the other is the conversion of an encrypted (LUKS) volume to another LUKS
volume. Both will need the same preparation steps before performing them.


Checklist
---------
If you skip over everything else, **please** at least make sure you do these
steps before starting a conversion:

  - Resized file system size, shrunk size by at least 10 MiB
  - Unmounted file system
  - Laptop is connected to A/C power (if applicable)



.. _preparation:

Preparation
-----------
The first thing you need to do is resize your file system to accomodate for the
fact that the device is going to be a tiny bit smaller in the end (due to the
LUKS header). The LUKS header size is usually 2048 kiB (it was 1028 kiB for
previous versions of cryptsetup), but you can safely decrease the file system
size by more (like 100 MiB) to be on the safe side.  If you decrease the size
too much you have no drawbacks (and you can easily increase after the
conversion has been performed).

.. warning:: 
  Do not forget to shrink the file system before conversion

luksipc has no means of detecting wheter or not you have performed this step
and will not warn you if you haven't (it has no knowledge of the underlying
file system). This might lead to very weird file system errors in the case that
your volume ever wants to use the whole space and it might even render your
volume completely unmountable (depending on the check the file system driver
performs on the block device before allowing mounting).

For example, let's say you have a device at /dev/loop0 that has an ext4 file
system. You want to LUKSify it. We first resize our volume. For this we find
out how large the volume is currently::

    # tune2fs -l /dev/loop0
    tune2fs 1.42.9 (4-Feb-2014)
    Filesystem volume name:   <none>
    Last mounted on:          <not available>
    Filesystem UUID:          713cc62e-b2a2-406a-a82a-c4c1d01464e1
    Filesystem magic number:  0xEF53
    Filesystem revision #:    1 (dynamic)
    Filesystem features:      has_journal ext_attr resize_inode dir_index filetype extent flex_bg sparse_super large_file huge_file uninit_bg dir_nlink extra_isize
    Filesystem flags:         signed_directory_hash
    Default mount options:    user_xattr acl
    Filesystem state:         clean
    Errors behavior:          Continue
    Filesystem OS type:       Linux
    Inode count:              64000
    Block count:              256000
    Reserved block count:     12800
    Free blocks:              247562
    Free inodes:              63989
    First block:              0
    Block size:               4096
    [...]

So we now know that our device is 256000 blocks of 4096 bytes each, so exactly
1000 MiB. We verify this is correct (it is in this case). So we now want to
decrease the file system size to 900 MiB. 900 MiB = 900 * 1024 * 1024 bytes =
943718400 bytes. With a file system block size of 4096 bytes we arrive at
943718400 / 4096 = 230400 blocks for the file system with decreased size. So we
resize the file system::

    # resize2fs /dev/loop0 230400
    resize2fs 1.42.9 (4-Feb-2014)
    Resizing the filesystem on /dev/loop0 to 230400 (4k) blocks.
    The filesystem on /dev/loop0 is now 230400 blocks long.

That was successful. Perfect. Now (if you haven't already), umount the volume.

.. warning:: 
  Do not forget to unmount the file system before conversion


Plain to LUKS conversion
------------------------
After having done the preparation as described in the :ref:`preparation`
subsection, we can proceed to LUKSify the device. By default the initial
randomized key is read from /dev/urandom and written to
/root/initial_keyfile.bin. This is okay for us, we will remove the appropriate
keyslot for this random key anyways in the future.  It is only used for
bootstrapping. We start the conversion::

    # ./luksipc -d /dev/loop0
    WARNING! luksipc will perform the following actions:
       => Normal LUKSification of plain device /dev/loop0
       -> luksFormat will be performed on /dev/loop0
    
    Please confirm you have completed the checklist:
        [1] You have resized the contained filesystem(s) appropriately
        [2] You have unmounted any contained filesystem(s)
        [3] You will ensure secure storage of the keyfile that will be generated at /root/initial_keyfile.bin
        [4] Power conditions are satisfied (i.e. your laptop is not running off battery)
        [5] You have a backup of all important data on /dev/loop0
    
        /dev/loop0: 1024 MiB = 1.0 GiB
        Chunk size: 10485760 bytes = 10.0 MiB
        Keyfile: /root/initial_keyfile.bin
        LUKS format parameters: None given
    
    Are all these conditions satisfied, then answer uppercase yes:

Please, read the whole message thourougly. There is no going back from this. If
and only if you're 100% sure that all preconditions are satisfied, answer
"YES" and press return::

    Are all these conditions satisfied, then answer uppercase yes: YES
    [I]: Created raw device alias: /dev/loop0 -> /dev/mapper/alias_luksipc_raw_89ee2dc8
    [I]: Size of reading device /dev/loop0 is 1073741824 bytes (1024 MiB + 0 bytes)
    [I]: Backing up physical disk /dev/loop0 header to backup file header_backup.img
    [I]: Performing luksFormat of /dev/loop0
    [I]: Performing luksOpen of /dev/loop0 (opening as mapper name luksipc_7a6bfc08)
    [I]: Size of luksOpened writing device is 1071644672 bytes (1022 MiB + 0 bytes)
    [I]: Write disk smaller than read disk by 2097152 bytes (2048 kB + 0 bytes, occupied by LUKS header)
    [I]: Starting copying of data, read offset 10485760, write offset 0
    [I]:  0:00:  10.8%       110 MiB / 1022 MiB     0.0 MiB/s   Left:     912 MiB  0:00 h:m
    [I]:  0:00:  20.5%       210 MiB / 1022 MiB     0.0 MiB/s   Left:     812 MiB  0:00 h:m
    [I]:  0:00:  30.3%       310 MiB / 1022 MiB     0.0 MiB/s   Left:     712 MiB  0:00 h:m
    [I]:  0:00:  40.1%       410 MiB / 1022 MiB     0.0 MiB/s   Left:     612 MiB  0:00 h:m
    [I]:  0:00:  49.9%       510 MiB / 1022 MiB   412.0 MiB/s   Left:     512 MiB  0:00 h:m
    [I]:  0:00:  59.7%       610 MiB / 1022 MiB   402.4 MiB/s   Left:     412 MiB  0:00 h:m
    [I]:  0:00:  69.5%       710 MiB / 1022 MiB   401.5 MiB/s   Left:     312 MiB  0:00 h:m
    [I]:  0:00:  79.3%       810 MiB / 1022 MiB   360.4 MiB/s   Left:     212 MiB  0:00 h:m
    [I]:  0:00:  89.0%       910 MiB / 1022 MiB   350.0 MiB/s   Left:     112 MiB  0:00 h:m
    [I]:  0:00:  98.8%      1010 MiB / 1022 MiB   344.8 MiB/s   Left:      12 MiB  0:00 h:m
    [I]: Disk copy completed successfully.
    [I]: Synchronizing disk...
    [I]: Synchronizing of disk finished.

The volume was successfully converted! Now let's first add a passphrase that we
want to use for the volume (or any other method of key, your choice). You can
actually even do this while the copying process is running::

    # cryptsetup luksAddKey /dev/loop0 --key-file=/root/initial_keyfile.bin
    Enter new passphrase for key slot:
    Verify passphrase:

Let's check this worked::

    # cryptsetup luksDump /dev/loop0
    LUKS header information for /dev/loop0
    
    Version:        1
    Cipher name:    aes
    Cipher mode:    xts-plain64
    Hash spec:      sha1
    Payload offset: 4096
    MK bits:        256
    MK digest:      b2 34 b8 7b 70 e8 78 17 a4 12 00 41 dc a4 bc 70 a3 50 02 22
    MK salt:        ee 25 b4 f0 11 94 25 d1 2b 97 42 6c a6 ff 3d 1d
                    e7 6d 1e 15 dd a0 07 17 25 82 d1 f9 14 6c ab e9
    MK iterations:  50125
    UUID:           3e21bbe0-3d70-4189-8f19-04fb7d7c5bb9
    
    Key Slot 0: ENABLED
        Iterations:             201892
        Salt:                   9d b6 a1 f5 0f 91 ee 24 be 49 0e f7 f9 62 a2 06
                                aa 45 79 7f 1a 56 5c 8c a3 03 15 a0 d2 9e ca e5
        Key material offset:    8
        AF stripes:             4000
    Key Slot 1: ENABLED
        Iterations:             198756
        Salt:                   46 b4 21 fb e3 12 54 18 ff 8d 05 24 75 fc 3c 4b
                                3c 90 77 47 43 b6 0b 28 d9 b6 86 44 30 9e 20 d2
        Key material offset:    264
        AF stripes:             4000
    Key Slot 2: DISABLED
    Key Slot 3: DISABLED
    Key Slot 4: DISABLED
    Key Slot 5: DISABLED
    Key Slot 6: DISABLED
    Key Slot 7: DISABLED

You can see the initial keyfile (slot 0) and the passphrase we just added (slot
1). Let's scrub the initial keyslot so the initial keyfile becomes useless. We
do this by scrubbing slot 0. Don't worry, you cannot choose the wrong slot
here; cryptsetup won't permit you to remove the wrong slot since you must prove
that you still have at least access to one remaining slot (by entering your
passphrase)::

    # cryptsetup luksKillSlot /dev/loop0 0
    Enter any remaining passphrase:

And check again::

    # cryptsetup luksDump /dev/loop0
    LUKS header information for /dev/loop0
    
    Version:        1
    Cipher name:    aes
    Cipher mode:    xts-plain64
    Hash spec:      sha1
    Payload offset: 4096
    MK bits:        256
    MK digest:      b2 34 b8 7b 70 e8 78 17 a4 12 00 41 dc a4 bc 70 a3 50 02 22
    MK salt:        ee 25 b4 f0 11 94 25 d1 2b 97 42 6c a6 ff 3d 1d
                    e7 6d 1e 15 dd a0 07 17 25 82 d1 f9 14 6c ab e9
    MK iterations:  50125
    UUID:           3e21bbe0-3d70-4189-8f19-04fb7d7c5bb9
    
    Key Slot 0: DISABLED
    Key Slot 1: ENABLED
        Iterations:             198756
        Salt:                   46 b4 21 fb e3 12 54 18 ff 8d 05 24 75 fc 3c 4b
                                3c 90 77 47 43 b6 0b 28 d9 b6 86 44 30 9e 20 d2
        Key material offset:    264
        AF stripes:             4000
    Key Slot 2: DISABLED
    Key Slot 3: DISABLED
    Key Slot 4: DISABLED
    Key Slot 5: DISABLED
    Key Slot 6: DISABLED
    Key Slot 7: DISABLED

Perfect, only our slot 1 (passphrase) is left now, you can safely discard the
initial_keyfile.bin now.

Last step, resize the filesystem to its original size. For this we must first
mount the cryptographic file system and then call the resize2fs utility again::

    # cryptsetup luksOpen /dev/loop0 newcryptofs
    Enter passphrase for /dev/loop0:
    
    # resize2fs /dev/mapper/newcryptofs
    resize2fs 1.42.9 (4-Feb-2014)
    Resizing the filesystem on /dev/mapper/newcryptofs to 255488 (4k) blocks.
    The filesystem on /dev/mapper/newcryptofs is now 255488 blocks long.

You can see that the filesystem now occupies all available space (998 MiB).



LUKS to LUKS conversion
-----------------------
There are situations in which you might want to re-encrypt your LUKS device.
For example, let's say you have a cryptographic volume and multiple users have
access to it, each with their own keyslot. Now suppose you forfeit the rights
of one person to the volume. Technically you would do this by killing the
appropriate key slot of the key that was assigned to the user. This means the
user can from then on not unlock the volume using the LUKS keyheader.

But suppose the user you want whose access you want to revoke had -- while
still in possession of a valid key -- access to the file system container
itself. Then with that LUKS header he can still (even when the slot was killed)
derive the underlying cryptographic key that secures the data. The only way to
remedy this is to reencrypt the whole volume with a different bulk-encryption
key.

Another usecase are old LUKS volumes: the algorithms that were used at creation
may not be suitable anymore. For example, maybe you have switched to some other
hardware platform that has hardware support for specific algorithms and you can
only take advantage of those when you choose a specific encryption algorithm.
Or maybe the alignment that was adequate a couple of years back is not adquate
anymore for you. For example, older cryptsetup instances used 1028 kiB headers,
which is an odd size. Or maybe LUKS gained new features that you want to use.

In any case, there are numerous cases why you want to turn a LUKS volume into
another LUKS volume. This process is called "reLUKSification" within luksipc
and it is something that is supported from 0.03 onwards.

Let's say you have a partition called /dev/sdh2 which you want to reLUKSify.
First let's see what the used encryption parameters are::

    # cryptsetup luksDump /dev/sdh2
    LUKS header information for /dev/sdh2
    
    Version:        1
    Cipher name:    aes
    Cipher mode:    xts-plain64
    Hash spec:      sha1
    Payload offset: 4096
    MK bits:        256
    MK digest:      b1 44 6a 73 e3 06 27 27 a2 fe c2 59 e5 3a 39 2e 15 d7 d7 e0
    MK salt:        09 6d 6a 24 66 28 43 f7 f3 55 a9 9d 0a 40 77 58
                    e0 1f 7c 30 b9 63 96 eb 99 34 52 4f 72 ba 57 ac
    MK iterations:  49750
    UUID:           6495d24d-34ac-41f5-a594-c5058cc31ed3
    
    Key Slot 0: ENABLED
        Iterations:             206119
        Salt:                   99 c8 48 50 c3 a6 83 0d f9 39 a4 4d 0a 35 b0 ab
                                13 83 ee fd 9f 91 8d 92 a6 cf 42 50 9b 89 a6 be
        Key material offset:    8
        AF stripes:             4000
    Key Slot 1: DISABLED
    Key Slot 2: DISABLED
    Key Slot 3: DISABLED
    Key Slot 4: DISABLED
    Key Slot 5: DISABLED
    Key Slot 6: DISABLED
    Key Slot 7: DISABLED

We'll now open the device with our old key (a passphrase)::

    # cryptsetup luksOpen /dev/sdh2 oldluks

Just for demonstration purposes, we can calculate the MD5SUM over the whole
block device (you won't need to do that, it's just a demo)::

    # md5sum /dev/mapper/oldluks
    48d9763be76ddb4fb990367f8d6b8c22  /dev/mapper/oldluks

For reLUKSification to work, you need to supply the path to the unlocked device
(from where data will be read) as well as the path to the underlying raw device
(which will be luksFormatted).

You currently have your (raw) disk at /dev/sdh2 and your (unlocked) read disk
at /dev/mapper/oldluks. It may be possible that a new LUKS header is even
larger than the old header as now, which will lead to truncation of data at the
very end of the partition. This will be the case, for example, if you reLUKSify
volumes that have a 1028 kiB LUKS header and recreate with a recent version
which writes 2048 kiB LUKS headers. You need to take all measures to decrease
the size of the contained file system, as shown in :ref:`preparation`.  These
steps will not be repeated here, but you **must** perform them nevertheless if
you want to avoid losing data.

After the disk is unlocked, you call luksipc. In addition to the raw device
which you want to convert you will also now have to specify the block device
name of the unlocked device. The raw device is the one that luksFormat and
luksOpen will be called on and the read device is the device from which data
will be read during the copy procedure. Here's how the call to luksipc looks
like. We assume that we want to change the underlying hash function to SHA256::

    # luksipc --device /dev/sdh2 --readdev /dev/mapper/oldluks --luksparams='-h,sha256'
    WARNING! luksipc will perform the following actions:
       => reLUKSification of LUKS device /dev/sdh2
       -> Which has been unlocked at /dev/mapper/oldluks
       -> luksFormat will be performed on /dev/sdh2
    
    Please confirm you have completed the checklist:
        [1] You have resized the contained filesystem(s) appropriately
        [2] You have unmounted any contained filesystem(s)
        [3] You will ensure secure storage of the keyfile that will be generated at /root/initial_keyfile.bin
        [4] Power conditions are satisfied (i.e. your laptop is not running off battery)
        [5] You have a backup of all important data on /dev/sdh2
    
        /dev/sdh2: 2512 MiB = 2.5 GiB
        Chunk size: 10485760 bytes = 10.0 MiB
        Keyfile: /root/initial_keyfile.bin
        LUKS format parameters: -h,sha256
    
    Are all these conditions satisfied, then answer uppercase yes: YES
    [I]: Created raw device alias: /dev/sdh2 -> /dev/mapper/alias_luksipc_raw_60377226
    [I]: Size of reading device /dev/mapper/oldluks is 2631925760 bytes (2510 MiB + 0 bytes)
    [I]: Backing up physical disk /dev/sdh2 header to backup file header_backup.img
    [I]: Performing luksFormat of /dev/sdh2
    [I]: Performing luksOpen of /dev/sdh2 (opening as mapper name luksipc_dbb86eda)
    [I]: Size of luksOpened writing device is 2631925760 bytes (2510 MiB + 0 bytes)
    [I]: Write disk size equal to read disk size.
    [I]: Starting copying of data, read offset 10485760, write offset 0
    [I]:  0:00:   4.4%       110 MiB / 2510 MiB    43.5 MiB/s   Left:    2400 MiB  0:00 h:m
    [I]:  0:00:   8.4%       210 MiB / 2510 MiB    34.1 MiB/s   Left:    2300 MiB  0:01 h:m
    [I]:  0:00:  12.4%       310 MiB / 2510 MiB    21.9 MiB/s   Left:    2200 MiB  0:01 h:m
    [...]
    [I]:  0:02:  88.0%      2210 MiB / 2510 MiB    17.3 MiB/s   Left:     300 MiB  0:00 h:m
    [I]:  0:02:  92.0%      2310 MiB / 2510 MiB    17.6 MiB/s   Left:     200 MiB  0:00 h:m
    [I]:  0:02:  96.0%      2410 MiB / 2510 MiB    18.0 MiB/s   Left:     100 MiB  0:00 h:m
    [I]:  0:02: 100.0%      2510 MiB / 2510 MiB    18.2 MiB/s   Left:       0 MiB  0:00 h:m
    [I]: Disk copy completed successfully.
    [I]: Synchronizing disk...
    [I]: Synchronizing of disk finished.

After the process has finished, the old LUKS device /dev/mapper/oldluks will
still be open. Be very careful not to do anything with that device, however!
It's safe to close it::

    # cryptsetup luksClose oldluks

Then, let's open the device with the new key::

    # cryptsetup luksOpen /dev/sdh2 newluks -d /root/initial_keyfile.bin

And check that the conversion worked::

    # md5sum /dev/mapper/newluks
    48d9763be76ddb4fb990367f8d6b8c22  /dev/mapper/newluks

Which it did :-)
