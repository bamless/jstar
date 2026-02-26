---
layout: default
title: Lists
nav_order: 3
description: "Lists"
parent: Documentation
permalink: docs/lists
---

# Lists
{: .no_toc }

## Table of contents
{: .no_toc .text-delta }

1. TOC
{:toc}

---

Lists are compound objects that hold a sequence of values. They are dynamic and resizable, and can
be created with a *list literal*:
```jstar
[1, "foo", false, null]
```

Lists can hold elements of any type, and are not limited to one type at a time like in other 
languages.

They are instances of the `List` class, and can be created by calling its constructor as well:
<pre class='runnable-snippet'>
// When the passed in argument is an Iterable object, 
// then a new List containing all of its element is returned
print(List([1, 2, 3]))

// When the first argument is a number, then a new list 
// containing that number of elements will be returned.
// The elements are inititalized with the second argument
print(List(5, 0))

// If the second argument is a Function, then for every element 
// of the list the function is called, and its return value is 
// used to initialize that slot in the list
print(List(5, |i| => i))
</pre>

We will define what is an Iterable object in the [iterable protocol](iterable-protocol) section.
 
The funny looking `|| =>` syntax is called a 'function literal'. We'll discuss more about it in the 
[functions](functions) section.

## Accessing elements

Elements in a list can be accessed using the subscript operator `[]`:
<pre class='runnable-snippet'>
var numbers = [1, 2, 3, 4, 5]
print(numbers[0])
print(numbers[1])
</pre>

In **J\*** all sequences index starting from `0`. Using an integer past the end or before the
beginning of the sequence will result in a runtime error:
<pre class='runnable-snippet'>
var list = ["foo", "bar"]
print(list[2]) // The list has only 2 elements, this will fail
</pre>

## Slicing a list

Lists can be 'sliced' by using this syntax:
<pre class='runnable-snippet'>
var list = ["foo", false, "bar", 49]
print(list[0, 2])
</pre>

Slicing a list returns a new list containing all elements that are between (n, m], where n and m are
the slice indeces. Same as element access, when one or both of the slice indeces are out of bounds
a runtime error is produced:
<pre class='runnable-snippet'>
var list = ["foo", false, "bar", 49]
print(list[0, 6])
</pre>

## Adding and changing elements

A list is a mutable sequence, and as such one can modify its elements or add new ones:
<pre class='runnable-snippet'>
var list = ["foo", false, "bar", 49]

// To assign an element, simply subscript the list
// and assign a new value using the `=` operator
list[0] = "bar"
print(list)

// `add` appends the given value to the end of the list
list.add("new element")
print(list)
</pre>

To add a new element at a specific index, you can use the `insert` method:
<pre class='runnable-snippet'>
var list = ["foo", false, "bar", 49]
list.insert(2, "new element")
print(list)
</pre>

In `insert`, the index passed as an argument is allowed to be one past the end of the list. In this
case, `insert` and `add` are equivalent:
<pre class='runnable-snippet'>
var l1 = ["foo"]
var l2 = ["foo"]

l1.add("bar")
l2.insert(1, "bar")

print(l1, l2)
</pre>

## Removing elements

Elements can be removed using the `removeAt` method:
<pre class='runnable-snippet'>
var list = ["foo", "bar"]
list.removeAt(0)
print(list)
</pre>

If you don't know the index of the value you want to remove, but you know its value, you can use the
`remove` method:
<pre class='runnable-snippet'>
var list = ["foo", "bar"]
list.remove("foo")
print(list)
</pre>

`remove` will walk the list and remove the first value that compares equal to the given one.  
In case of simple values like strings or numbers, it is obvious what 'two elements are equal' means,
but for more general types this is not always the case. We'll discuss more about this in the 
[Operators and overloads](operators-and-overloads) section.

## Other useful methods
Below we list a handful of useful methods that `List` implements:
```jstar
list.clear()                  // Removes all elements from the list
list.addAll(iterable)         // Adds all the elements of the given iterable to the list
list.insertAll(idx, iterable) // Similar to the above, but inserts the elements at index 'idx'
list.sort(comparator)         // Sort the list according to the provided comparator function
list.removeAll(iterable)      // Removes all elements that compare equal to those of 'iterable'
list.pop()                    // Removes and returns the last element of the list
```