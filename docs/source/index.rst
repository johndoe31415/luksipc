luksipc Documentation
=====================

.. warning:: luksipc was created before any alternative from dm-crypt/cryptsetup/LUKS side was available. This is not the case anymore. Therefore I recommend switching to cryptsetup-reencrypt, which is properly maintained and tested upstream even when the format of the LUKS header changes (to my knowledge, this has at least happened twice and can cause luksipc to catastrophically fail, i.e., destroy all your data in the worst case).

.. toctree::
   :maxdepth: 2
   :caption: luksipc Documentation

   introduction
   usage
   problems
   testing


