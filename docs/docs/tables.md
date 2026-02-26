---
layout: default
title: Tables
nav_order: 5
description: "Tables"
parent: Documentation
permalink: docs/tables
---

# Tables
{: .no_toc }

## Table of contents
{: .no_toc .text-delta }

1. TOC
{:toc}

---

Tables implement an hash-table data structure. In other languages they are also called maps, 
dictionaries or associative arrays. They contain a set of keys that map to a value.

Tables can be created by using a *table literal*:
<pre class='runnable-snippet'>
var table = {'one' : 1, 'two' : 2, 'three' : 3, 'four' : 4}
print(table)
</pre>

Their class is `Table`, and can be also created by invoking its constructor:
<pre class='runnable-snippet'>
// If the supplied argument is a Table, then that table is returned
var table1 = Table({'foo' : 1, 'bar' : 2})
print(table1)

// If the supplied argument is an iterable object composed of 2-tuples,
// Then a new table is constructed by treating the tuples as key-value pairs
var table2 = Table([('one', 1), ('two', 2)])
print(table2)
</pre>

Being an implementation of hash-tables, tables do not preserve the insertion order of entries,
and they can't be ordered.

## Accessing elements

Elements of a table can be accessed using the subscript operator:
<pre class='runnable-snippet'>
var table = {'one' : 1, 'two' : 2, 'three' : 3, 'four' : 4}
print(table['one'])
</pre>

The argument of the operator is not limited on being an integer, but can be any J* value (with some
exceptions - see [adding elements](tables#adding-elements)). The table will then hash the argument,
search for it among its keys, and return its associated value. If the key cannot be found, then the
subscript will return `null`. This can be problematic if `null` is also a valid value that is
associated to some key. To disambiguate this case, the `contains` method can be used. It will return
`true` if the provided key is present in the table, `false` otherwise:
<pre class='runnable-snippet'>
var table = {'one' : 1, 'two' : 2, 'three' : 3, 'four' : 4}
print(table.contains('one'))
print(table.contains('ten'))
</pre>

## Adding elements

Elements can be added by subscripting the table and assigning a new value:
<pre class='runnable-snippet'>
var table = {}
table['new element'] = 49
print(table)
</pre>

Valid keys for a table are comprised of all **J\*** values, as long as they implement the `__hash__` 
and `__eq__` methods, and they aren't `null`. `__hash__` and `__eq__` will be discussed in the 
[operators and overloads](operators-and-overloads) section.
<pre class='runnable-snippet'>
var table = {}

// Using different value types as keys 
table[5] = 'five'
table['five'] = 5
table[false] = 'not true'

print(table)
</pre>

## Removing elements

To remove an entry from a table the `delete` method can be used:
<pre class='runnable-snippet'>
var table = {'one' : 1, 'two' : 2, 'three' : 3, 'four' : 4}

print(table.delete('one'))
print(table)

print(table.delete('nonexistent'))
</pre>

This method will return `true` if it finds an entry that matches the provided key, `false`
otherwise.

## Iterating on tables

We will delve into iteration in more detail in the [control flow](docs/control-flow) section, but,
being iteration a pretty tricky subject for tables, we will take a little detour here.

Why is it tricky you ask?  
Let's suppose we want to iterate over a table, the syntax for doing so is such:
```jstar
var table = {'one' : 1, 'two' : 2, 'three' : 3, 'four' : 4}
for var e in table
    print(e)
end
```

What should 'e' be? In other words, on what should we iterate on?  
One answer could be keys, but another valid one could be values or even 2-tuples containing both
the key and the value.

**J\*** chooses to return keys as iteration elements for tables. In other words, we iterate on the 
keys.  
This is so for three main reasons:
 1. It is unlikely that we want to iterate exclusively on the values of a table. If you need to 
    iterate over all of the values of a table frequently, then maybe a table is not the right data 
    tructure for the problem at hand. A list or tuple will be way more efficient.
 2. Returning a 2-tuple containing both a key and a value would be too inefficient. Creating a new
    object for every iteration of a loop would stress the garbage collector too much, making the
    iteration slow 
 3. By iterating on keys, we have actually easy access to the values as well. We can simply use the
    newly obtained key to index into the table. This will incur in a little overhead, but is much
    faster than option `2` and not a huge cost anyway.

So, iterating over a table will return its keys one by one:
<pre class='runnable-snippet'>
var table = {'one' : 1, 'two' : 2, 'three' : 3, 'four' : 4}
for var key in table
    print(key)
end
</pre>

Also note that the order of the keys in the iteration is unspecified.


## Using Tables as sets

**J\*** doesn't provide a built-in set data structure. Fortunately emulating such a structure using
tables is pretty easy:
<pre class='runnable-snippet'>
var keywords = {
    "return" : true,
    "class"  : true,
    "fun"    : true
}

print("Is `return` a keyword?", Boolean(keywords["return"]))
print("Is `foo` a keyword?", Boolean(keywords["foo"]))
</pre>

We simply assign true to the elements that are supposed to be contained in the set.  
As we briefly mentioned in the discussion of [Booleans](values-and-types#booleans), all values in 
**J\*** have an instric truth value. It just happens that the truth value of `null` is `false`, so 
when we try to look for an entry that isn't present in the set and we get `null` back, we can use it
like a `false` boolean. This spares us the need to explicitely assign `false` to all elements that
are not part of the set, because the subscript will return `null` if it doesn't find an entry.

# Other useful methods
```jstar
table.clear()  // Removes all entries in the table
table.keys()   // Retuns a list containing all the keys in the table
table.values() // Retuns a list containing all the values in the table
```
