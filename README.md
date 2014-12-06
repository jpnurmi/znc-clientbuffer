Client specific buffers for ZNC
===============================

The client buffer module maintains client specific buffers for identified clients.

### Configuration

In order to make it possible for the module to control client specific buffers, you must disable the `AutoClearQueryBuffer` and `AutoClearQueryBuffer` config options that are enabled by default. This can be done via controlpanel, webadmin, or znc.conf.

### Commands

    /msg *chanfilter addclient <identifier>
    /msg *chanfilter delclient <identifier>
    /msg *chanfilter listclients

### Identifiers

ZNC supports passing a client identifier in the password:

    username@identifier/network:password

or in the username:

    username@identifier/network

### Contact

Got questions? Contact jpnurmi@gmail.com or *jpnurmi* on Freenode.
