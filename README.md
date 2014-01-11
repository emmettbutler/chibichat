Chibichat
========

This is a basic multiuser chat server. It is based on the chat server example
in [Beej's Network Programming Guide](http://beej.us/guide/bgnet/output/html/singlepage/bgnet.html).

It supports nicknames for all users.

Usage
=====

Compile the code with

    gcc chatserver.c -o chibichat

Run with

    ./chibichat

From another window or machine, connect with

    telnet <IP_OF_MACHINE_RUNNING_SERVER> 9034
