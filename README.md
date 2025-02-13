# sawang
Proxy Library for Gonggo

![gonggo](https://github.com/user-attachments/assets/7f0c4448-fc64-4658-99cd-fc7f8ba5bf1c)

A proxy is needed by Gonggo to interract with a backend service. Sawang is a dynamic shared library to build proxy process. All you have to do is defining [5 callback functions](./gear/callback.h) :

1. ProxyPayloadParse: runs in proxychannel thread on client request parsing.
2. ProxyStart: runs in proxy proxycomm thread start. A function to initialize resources.
3. ProxyRun: runs in proxycomm loop.
4. ProxyMultiRespondClear: runs in proxycomm loop to clear a multirespond service.
5. ProxyStop: run in proxycomm thread stop. A function to destroy resources.

## Dependencies

Sawang is developed in C for efficient and fast message dispatching. It depends on libraries written in C: 
- [cJSON](https://github.com/DaveGamble/cJSON)

## Installation

Sawang can only be installed on Linux server. [How to install](INSTALL.md).

