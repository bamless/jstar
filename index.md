---
layout: default
title: Home
nav_order: 1
description: "J*: A lightweight embeddable scripting language"
permalink: /
---

# J* - A lightweight embeddable scripting language

**J\*** is a small, dynamic, embeddable scripting language designed to be as easy as possible to 
embed into another program. It arises from the need of having a modern scripting language with 
built-in support for object oriented programming whilst mantaning simplicity of use and a low memory
footprint.

It can be viewed as a middle ground between Python, a more complete scripting language with lots of 
features and libraries, and Lua, a small and compact language that is simple to embed but doesn't 
provide OOP functionalities out of the box.  


J* tries to take the best of both worlds, implementing a fully featured class system while 
maintaining a small standard library and employing the use of a stack based API for communication 
among the language and host program, rendering embedding simple.

**J\*** is:
 - **Small**. The implementation spans only a handful of files and the memory footprint is low
   thanks to a minimal standard library that provides only essential functionalities
 - **Easy to use**. The API is contained in a single header file and employs a stack based approach
   similar to the one of Lua, freeing the user from the burden of keeping track of memory owned by
   the language
 - **Fully object oriented**. Every entity, from numbers to class instances, is an object in **J\***
 - **Modular**. A fully fledged module system makes it easy to split your code across multiple files
 - **Easily extensible**. The language can be easily extended by creating C functions callable from
   **J\*** using the API, or by importing [C extensions](https://github.com/bamless/jsocket) 
   provided as dynamic libraries.

If you like what you hear, [let's get started!](getting-started)