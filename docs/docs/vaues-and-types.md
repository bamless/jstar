---
layout: default
title: Values and Types
nav_order: 2
description: "Values and Types"
parent: Documentation
permalink: docs/values-and-types
---

# Values and Types
{: .no_toc }

## Table of contents
{: .no_toc .text-delta }

1. TOC
{:toc}

---

**J\*** is a *dynamically* typed programming language. This means that variables do not have types,
instead values carry their own type with them.

Keep in mind that **J\*** is *dynamically* typed but not *weakly* typed. On the contrary, it's
actually a fairly *strongly* typed language, meaning that automatic conversions between types are
not performed. For example code like this
```jstar
"2" + 3
```
will fail with a type error, instead of converting the string into a number (or viceversa), like 
some other scripting languages do.

All values in **J\*** are first-class. This means that they can be assigned to variables, passed 
as arguments, returned as results and generally be operated on just like any other value.

**J\*** is deeply class based, and as such the *type* of a value is represented by its *class*. In 
fact, in **J\*** *class* and *type* are synonyms, as well as *value* and *instance*. Yes, even a 
simple number is an instance, and its class is *Number*:
<pre class='runnable-snippet'>
print(type(5)) // The type built-in function returns the class of the given value
</pre>

The language has 12 built-in value types:
*null*, *boolean*, *number*, *string*, *list*, *tuple*, *table*, *userdata*, *handle*,
*function*, *class* and *module*.

In the following we'll take a brief look at the most basic of them: *constant values*, while the
others will be explored during the rest of the documentation.

## Numbers

In **J\*** all numbers are stored as double-precision floating point values. Number literals are
pretty much what you'd expect:
```jstar
1
500
3.1415
1e-4   // Scientific notation
0xff   // Hexadecimal notation
04.51  // Zeroes in front are ignored
```
Numbers are instances of the `Number` class and can also be created by invoking its constructor:
<pre class='runnable-snippet'>
// If the given argument is a number, then that number is returned
print(Number(2))

// If the argument is a String, then it is parsed and its number representation returned
print(Number("23.4"))
</pre>

## Booleans

A boolean has only two possible values, `true` and `false`, representing truth and falsehood.  
Their class is `Boolean` and, just like numbers, can be created by invoking its constructor:
<pre class='runnable-snippet'>
// If the given argument is a boolean, then that boolean is returned
print(Boolean(false))

// If the argument is any other value, then its truth value is returned
print(Boolean(25))
</pre>
Every value in **J\*** has an intrisic truth value associated with it. We'll discuss more about this
in the [control flow](control-flow#truthiness-and-falsyness) section.

## Strings
Strings are immutable sequences of bytes. In **J\*** Strings are 8-bit clean, this means that they 
can contain arbitrary data and their encoding is not assumed.

They are created using string literals, with either single or double quotes:
```jstar
"String using double quotes"
'String using single quotes'
```

String literals can span multiple lines, and they mantain newlines characters if they do:
<pre class='runnable-snippet'>
print("Spanning
Multiple
Lines")
</pre>
All the usual escape characters are supported:
```jstar
"\0" // NUL byte
"\a" // Alert Beep
"\b" // Backspace
"\f" // Formfeed Page Break
"\n" // Newline
"\r" // Carriage return
"\t" // Horizontal tab
"\v" // Vertical tab
"\\" // Backslash
"\"" // Double quote (only in double-quoted strings)
'\'' // Single quote (only in single-quoted strings)
```

Strings are instances of the `String` class, and they can also be created by invoking its constructor:
<pre class='runnable-snippet'>
// If passed argument is a string, then that string is returned
print(String("ciao"))

// If any other value is passed, then the string representation of that value is returned
print(String(5))

// If multiple arguments are passed, then the returned string is the 
// concatenation of the string representation of all the arguments
print(String("foo", 49, "bar"))
</pre>

### Useful operators
Strings also support a handful of operators to perform useful tasks:
<pre class='runnable-snippet'>
// `[]` indexes into the string
print("foo"[0])

// `+` concatenates two strings
print("foo" + "bar")

// `%` is used for formatting.
// The `{...}` elements will be substituded with the 
// corresponding element of the right hand side
print("formatting {0} {1}" % ("foo", 49))
</pre>

## Null

null is the only instance of the `Null` class. Its defining property is that it's different from any
other value. It is often used to indicate the absence of a useful value.