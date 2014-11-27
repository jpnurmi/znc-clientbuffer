znc-clientbuffer
================

### Overview

WIP: ZNC module for client specific buffers

### Configuration

    AutoClearChanBuffer = false
    AutoClearQueryBuffer = false

### Usage

Module commands to manage identified clients:

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
