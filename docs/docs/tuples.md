---
layout: default
title: Tuples
nav_order: 4
description: "Tuples"
parent: Documentation
permalink: docs/tuples
---

# Tuples
{: .no_toc }

## Table of contents
{: .no_toc .text-delta }

1. TOC
{:toc}

---

Tuples are compound objects that hold a sequence of values. Differently from lists, tuples are
immutable. This means that once created, elements cannot be added or removed from a tuple. They can
be created using a *tuple literal*:
<pre class='runnable-snippet'>
var tuple = (1, "foo", false, "bar")
print(tuple)
</pre>

The syntax for a tuple literal is a bit unusual; they are not formed by the paranthesis, but by the
*comma operator*. Creating a tuple like this is thus legal:
<pre class='runnable-snippet'>
// Creating a tuple. The parenthesis aren't needed
var tuple1 = 1, "foo", false, "bar"
print(tuple1)

// Single element tuple. Note the use of the comma, which defines the element as a tuple
var tuple2 = "foo",
print(tuple2)

// A parenthesized expression without a comma is not a tuple!
// Parenthesis in this case denote grouping
var notATuple = ("foo")
print(notATuple)
</pre>

The only exception to this is the *empty tuple*, which is defined by a pair of empty parenthesis
<pre class='runnable-snippet'>
var empty = ()
print(empty)
</pre>

In fact, allowing unparenthesized 'nothing' in expressions would cause ambiguites in the language
grammar.

Note that the comma is an operator like any other, and thus has its own 
[precedence](syntax#operators-precedence). For example, if we want to create a list of tuples we
must group the tuple elements using parenthesis, because a list literal (`[expression, ...]`) binds 
more tightly than the comma operator:
<pre class='runnable-snippet'>
var listOfTuples = [(1, 2), (3, 4), (5, 6)]
print(listOfTuples)
</pre>

The class of tuples is `Tuple` and, just like other values, can be also created by invoking its 
constructor:
<pre class='runnable-snippet'>
// When the passed in argument is an Iterable object, 
// then a new Tuple containing all of its element is returned
var tuple = Tuple([1, 2, 3])
print(tuple)
</pre>

## Element access

Just like lists, tuple elements can be accessed by the subscript operator:
<pre class='runnable-snippet'>
var tuple = 1, "foo", false, "bar"
print(tuple[0])
</pre>

But, unlike lists, elements cannot be modified:
<pre class='runnable-snippet'>
var tuple = 1, "foo", false, "bar"
tuple[0] = "new element"
</pre>

## Slicing

Slicing works exactly like it does on lists:
<pre class='runnable-snippet'>
var tuple = 1, "foo", false, "bar"
print(tuple[0, 2])
</pre>