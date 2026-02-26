---
layout: default
title: Getting Started
nav_order: 2
description: "Getting started"
permalink: /getting-started
---

# Getting started
{: .no_toc }

## Table of contents
{: .no_toc .text-delta }

1. TOC
{:toc}

---

## Trying J\* from the Browser

An online version of the interpreter is provided [here](demo) if you want to hop straight in without
dealing with the installation process. While this is a pretty convinient way to get a feel for the 
language, installing it on your machine is still reccomended if you want to follow along with the 
[language guide](docs), as the online interpeter has some limitations.

## Installation

### Precompiled binaries

If you are on Windows or Linux, then you are in luck! Precompiled binaries are provided [here](https://github.com/bamless/jstar/releases)
for such platforms and the installation is as simple as unpacking the archive in case you chose to 
download the `zip` or `tar.gz` files, or double clicking on the installer program for the 
`-installer` packages.

In case you've downloaded the `zip` or `tar.gz` archive, moving its contents in a standard location 
on your system is reccomended in order to have easy access to the `jstar` and `jstarc` command line 
applications through the terminal.

For example, on Windows, a good place to put the `bin`, `lib` and `include` folders inside the 
archive is `C:\Program Files\jstar`. (Be sure to also add this folder to your `PATH` environment 
variable!)

For Linux, the reccomended path is `/usr/local`.

### Installing from source

If you use another operating system, do not fret!  
By its design, **J\*** is pretty minimal and has very few dependencies, which makes compiling it 
from source pretty easy. All you basically need is **cmake** and a **c**/**c++** compiler. 
You can get more info by reading the *compilation* section of the [README](https://github.com/bamless/jstar/blob/master/README.md)
file.

## The command line interface

If you decided to install **J\*** on you local machine, then the *cli* application will be your 
primary way of interacting with the language while learning it, so spending a few words on it will 
be beneficial.

**J\*** is an embeddable language first and foremost, but nonetheless an executable called `jstar` 
(`jstar.exe` on Windows) is provided to start using the language without first embedding it into 
another program.

Using the `jstar` command line interface is pretty easy if you followed the installation process
above; simply fire up a terminal and issue the `jstar` command. You will be greeted by a prompt and 
a blinking cursor.  This means that you entered the *repl*, or read-eval-print loop. This is the
state that the `jstar` application will enter when you invoke it witout arguments. In this state you
can simply write lines of J* code and the interpreter will execute them, printing any result that
the code produces.   
You can even use the *repl* as a simple calculator! In fact, the interpreter will assign the result 
of the last expression to the `_` (underscore) variable, so that you can chain calculations:
```jstar
J*>> 20 + 5
25
J*>> _ / 5
5
```

But what if you want to execute code written on some file? That's pretty easy aswell. Simply open
your favourite text editor, type in some J* code, save it on file and then exectue the `jstar`
program passing the path to the file as an argument:
`jstar path/to/file.jsr`. (`jsr` is the typical extension that we give to J* source files, but its
not required)

The `jstar` executable can also take in additional options that modify its behaviour, to see them
alongside a description simply pass the `-h` option to it.

Now you know all you need to start using **J\*** on your machine.