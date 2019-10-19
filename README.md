# luksipc
luksipc is the LUKS in-place-conversion tool. It allows conversion of plain
volumes to LUKS-encrypted volumes.

## cryptsetup-reencrypt
luksipc was created before any alternative from dm-crypt/cryptsetup/LUKS side
was available. This is not the case anymore. Therefore I recommend switching to
cryptsetup-reencrypt, which is properly maintained and tested upstream even
when the format of the LUKS header changes (to my knowledge, this has at least
happened twice and can cause luksipc to catastrophically fail, i.e., destroy
all your data in the worst case).

## Documentation
All documentation is available at [https://johndoe31415.github.io/luksipc/](https://johndoe31415.github.io/luksipc/).

## License
GNU GPL-3.
